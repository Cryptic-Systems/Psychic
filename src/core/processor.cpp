/**
 * DSP ENGINE — Core processor
 *
 * Pipeline: EQ → DVC → limiter (FAST / LOOKAHEAD / TRUEPEAK) → dither
 * Optional SRC via dsp_process_src then full pipeline.
 */

#include "dsp_engine.h"
#include "core/eq_biquad.h"
#include "core/dvc.h"
#include "core/resampler.h"
#include "utils/math_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#ifdef DSP_USE_DOUBLE
using sample_t = double;
#else
using sample_t = float;
#endif

namespace {

constexpr int kMaxChannels  = 8;
constexpr int kMaxEqBands   = 16;
constexpr int kMaxBlock     = 8192;
constexpr int kMaxLookahead = 2048;

} // anonymous

struct DspEngine {
    float sample_rate = 48000.f;
    int   channels    = 2;
    int   max_block   = 1024;

    bool eq_enabled      = false;
    bool limiter_enabled = false;
    bool dither_enabled  = false;

    DspDitherMode  dither_mode  = DSP_DITHER_TPDF;
    DspLimiterMode limiter_mode = DSP_LIMITER_FAST;

    float limiter_threshold_db = -1.0f;
    float limiter_release_ms   = 50.f;
    float limiter_ceiling_db   = -0.1f;
    float limiter_lookahead_ms = 2.0f;
    float limiter_attack_ms    = 0.1f;

    dsp::MultiBandEq eq;
    int eq_band_count = 0;
    DspEqBand eq_bands[kMaxEqBands]{};

    dsp::Dvc dvc;
    dsp::ResamplerState src;

    sample_t lim_env[kMaxChannels]{};
    sample_t lim_gain[kMaxChannels]{};
    sample_t lim_attack  = sample_t(0.001);
    sample_t lim_release = sample_t(0.05);
    sample_t lim_thresh  = sample_t(0.89);
    sample_t lim_ceiling = sample_t(0.9886);
    sample_t lim_gr      = sample_t(1);

    int lim_L      = 0;
    int delay_wr   = 0;
    int delay_fill = 0;
    int det_wr     = 0;
    int det_fill   = 0;

    alignas(32) sample_t delay_buf[kMaxChannels][kMaxLookahead]{};
    alignas(32) sample_t det_ring[kMaxLookahead + 1]{};
    sample_t tp_hist[kMaxChannels][4]{};

    uint32_t dither_state[kMaxChannels]{};

    alignas(32) sample_t temp[kMaxChannels][kMaxBlock]{};
    alignas(32) sample_t work[kMaxBlock * kMaxChannels]{};

    double* eq_ptrs[kMaxChannels]{};

    DspEngine() {
        for (int c = 0; c < kMaxChannels; ++c) {
            lim_gain[c] = sample_t(1);
            lim_env[c]  = sample_t(0);
            dither_state[c] = 0x12345678u ^ static_cast<uint32_t>(c * 0x9E3779B9u);
            eq_ptrs[c] = reinterpret_cast<double*>(temp[c]);
        }
    }

    void clear_lookahead_state() {
        delay_wr = 0;
        delay_fill = 0;
        det_wr = 0;
        det_fill = 0;
        lim_gr = sample_t(1);
        std::memset(delay_buf, 0, sizeof(delay_buf));
        std::memset(det_ring, 0, sizeof(det_ring));
        std::memset(tp_hist, 0, sizeof(tp_hist));
    }

    void update_limiter_coeffs() {
        const double atk_ms = limiter_attack_ms > 0.f
            ? static_cast<double>(limiter_attack_ms) : 0.05;
        lim_attack  = static_cast<sample_t>(
            1.0 - std::exp(-1.0 / (0.001 * atk_ms * sample_rate)));
        lim_release = static_cast<sample_t>(
            1.0 - std::exp(-1.0 / ((limiter_release_ms * 0.001) * sample_rate)));
        lim_thresh  = static_cast<sample_t>(dsp::db_to_linear(limiter_threshold_db));
        lim_ceiling = static_cast<sample_t>(dsp::db_to_linear(limiter_ceiling_db));

        if ((limiter_mode == DSP_LIMITER_LOOKAHEAD ||
             limiter_mode == DSP_LIMITER_TRUEPEAK) &&
            sample_rate > 0.f) {
            float ms = limiter_lookahead_ms > 0.f ? limiter_lookahead_ms : 2.0f;
            int L = static_cast<int>(std::lround(0.001 * static_cast<double>(ms) * sample_rate));
            if (L < 1) L = 1;
            if (L > kMaxLookahead) L = kMaxLookahead;
            lim_L = L;
        } else {
            lim_L = 0;
        }
    }

    void rebuild_eq() {
        eq.init(sample_rate, channels);
        eq.set_band_count(eq_band_count);
        for (int i = 0; i < eq_band_count && i < kMaxEqBands; ++i) {
            const DspEqBand& b = eq_bands[i];
            eq.set_band(i, b.freq_hz, b.gain_db, b.q > 0.f ? b.q : 1.0, b.type);
        }
        eq.set_enabled(eq_enabled);
    }
};

static sample_t true_peak_pair(sample_t a, sample_t b) {
    sample_t peak = a >= 0 ? a : -a;
    sample_t ab = b >= 0 ? b : -b;
    if (ab > peak) peak = ab;
    sample_t m = sample_t(0.5) * (a + b);
    sample_t am = m >= 0 ? m : -m;
    if (am > peak) peak = am;
    sample_t q1 = a * sample_t(0.75) + b * sample_t(0.25);
    sample_t q3 = a * sample_t(0.25) + b * sample_t(0.75);
    sample_t aq1 = q1 >= 0 ? q1 : -q1;
    sample_t aq3 = q3 >= 0 ? q3 : -q3;
    if (aq1 > peak) peak = aq1;
    if (aq3 > peak) peak = aq3;
    return peak;
}

static void limiter_fast(DspEngine* eng, int frames) {
    const int ch = eng->channels;
    const sample_t thr = eng->lim_thresh;
    const sample_t atk = eng->lim_attack;
    const sample_t rel = eng->lim_release;
    for (int i = 0; i < frames; ++i) {
        sample_t peak = sample_t(0);
        for (int c = 0; c < ch; ++c) {
            sample_t x = eng->temp[c][i];
            sample_t ax = x >= 0 ? x : -x;
            if (ax > peak) peak = ax;
            if (i + 1 < frames) {
                sample_t tp = true_peak_pair(eng->temp[c][i], eng->temp[c][i + 1]);
                if (tp > peak) peak = tp;
            }
        }
        sample_t env = eng->lim_env[0];
        sample_t gain = eng->lim_gain[0];
        if (peak > env) env += atk * (peak - env);
        else env += rel * (peak - env);

        sample_t target = (env > thr) ? (thr / (env + sample_t(1e-12))) : sample_t(1);
        if (target < gain) gain += atk * (target - gain);
        else gain += rel * (target - gain);

        eng->lim_env[0]  = env;
        eng->lim_gain[0] = gain;
        eng->lim_gr      = gain;
        for (int c = 0; c < ch; ++c)
            eng->temp[c][i] *= gain;
    }
    for (int c = 1; c < ch; ++c) {
        eng->lim_env[c]  = eng->lim_env[0];
        eng->lim_gain[c] = eng->lim_gain[0];
    }
}

static void limiter_lookahead(DspEngine* eng, int frames) {
    const int ch = eng->channels;
    const int L  = eng->lim_L;
    const sample_t C   = eng->lim_ceiling;
    const sample_t atk = eng->lim_attack;
    const sample_t rel = eng->lim_release;
    const int det_cap  = L + 1;

    sample_t gain = eng->lim_gain[0];

    for (int i = 0; i < frames; ++i) {
        sample_t peak = sample_t(0);
        for (int c = 0; c < ch; ++c) {
            sample_t x = eng->temp[c][i];
            sample_t ax = x >= 0 ? x : -x;
            if (ax > peak) peak = ax;
            if (i + 1 < frames) {
                sample_t tp = true_peak_pair(eng->temp[c][i], eng->temp[c][i + 1]);
                if (tp > peak) peak = tp;
            }
        }

        eng->det_ring[eng->det_wr] = peak;
        eng->det_wr = (eng->det_wr + 1) % det_cap;
        if (eng->det_fill < det_cap) eng->det_fill++;

        sample_t E = sample_t(0);
        const int n_det = eng->det_fill;
        for (int k = 0; k < n_det; ++k) {
            sample_t d = eng->det_ring[k];
            if (d > E) E = d;
        }

        sample_t g_tgt = (E > C) ? (C / (E + sample_t(1e-12))) : sample_t(1);
        if (g_tgt < gain) gain += atk * (g_tgt - gain);
        else              gain += rel * (g_tgt - gain);

        eng->lim_gr = gain;

        const int wr = eng->delay_wr;
        for (int c = 0; c < ch; ++c) {
            sample_t delayed = sample_t(0);
            if (eng->delay_fill >= L) {
                int rd = wr - L;
                if (rd < 0) rd += kMaxLookahead;
                delayed = eng->delay_buf[c][rd];
            }
            eng->delay_buf[c][wr] = eng->temp[c][i];
            sample_t y = delayed * gain;
            if (y > C) y = C;
            if (y < -C) y = -C;
            eng->temp[c][i] = y;
        }
        eng->delay_wr = (wr + 1) % kMaxLookahead;
        if (eng->delay_fill < kMaxLookahead) eng->delay_fill++;
    }

    eng->lim_gain[0] = gain;
    for (int c = 1; c < ch; ++c)
        eng->lim_gain[c] = gain;
}

static sample_t cubic_tp(sample_t ym1, sample_t y0, sample_t y1, sample_t y2, sample_t t) {
    const sample_t t2 = t * t;
    const sample_t t3 = t2 * t;
    return sample_t(0.5) * (
        (sample_t(2) * y0) +
        (-ym1 + y1) * t +
        (sample_t(2) * ym1 - sample_t(5) * y0 + sample_t(4) * y1 - y2) * t2 +
        (-ym1 + sample_t(3) * y0 - sample_t(3) * y1 + y2) * t3);
}

static void limiter_truepeak(DspEngine* eng, int frames) {
    const int ch = eng->channels;
    int L = eng->lim_L;
    if (L < 1) L = 1;
    const sample_t C   = eng->lim_ceiling;
    const sample_t atk = eng->lim_attack;
    const sample_t rel = eng->lim_release;
    const int det_cap  = L + 1;

    sample_t gain = eng->lim_gain[0];

    for (int i = 0; i < frames; ++i) {
        sample_t peak = sample_t(0);
        for (int c = 0; c < ch; ++c) {
            sample_t ym1, y0, y1, y2;
            if (i >= 1) ym1 = eng->temp[c][i - 1];
            else        ym1 = eng->tp_hist[c][2];
            y0 = eng->temp[c][i];
            if (i + 1 < frames) y1 = eng->temp[c][i + 1];
            else                y1 = y0;
            if (i + 2 < frames) y2 = eng->temp[c][i + 2];
            else                y2 = y1;

            for (int s = 0; s < 4; ++s) {
                sample_t t = static_cast<sample_t>(s) * sample_t(0.25);
                sample_t os = cubic_tp(ym1, y0, y1, y2, t);
                sample_t a = os >= 0 ? os : -os;
                if (a > peak) peak = a;
            }
            sample_t ax = y0 >= 0 ? y0 : -y0;
            if (ax > peak) peak = ax;
        }

        eng->det_ring[eng->det_wr] = peak;
        eng->det_wr = (eng->det_wr + 1) % det_cap;
        if (eng->det_fill < det_cap) eng->det_fill++;

        sample_t E = sample_t(0);
        const int n_det = eng->det_fill;
        for (int k = 0; k < n_det; ++k) {
            sample_t d = eng->det_ring[k];
            if (d > E) E = d;
        }

        sample_t g_tgt = (E > C) ? (C / (E + sample_t(1e-12))) : sample_t(1);
        if (g_tgt < gain) gain += atk * (g_tgt - gain);
        else              gain += rel * (g_tgt - gain);
        eng->lim_gr = gain;

        const int wr = eng->delay_wr;
        for (int c = 0; c < ch; ++c) {
            sample_t delayed = sample_t(0);
            if (eng->delay_fill >= L) {
                int rd = wr - L;
                if (rd < 0) rd += kMaxLookahead;
                delayed = eng->delay_buf[c][rd];
            }
            eng->delay_buf[c][wr] = eng->temp[c][i];
            sample_t y = delayed * gain;
            if (y > C) y = C;
            if (y < -C) y = -C;
            eng->temp[c][i] = y;
        }
        eng->delay_wr = (wr + 1) % kMaxLookahead;
        if (eng->delay_fill < kMaxLookahead) eng->delay_fill++;
    }

    for (int c = 0; c < ch; ++c) {
        for (int k = 0; k < 3; ++k) {
            int back = 3 - k;
            if (eng->delay_fill >= back) {
                int rd = eng->delay_wr - back;
                if (rd < 0) rd += kMaxLookahead;
                eng->tp_hist[c][k + 1] = eng->delay_buf[c][rd];
            }
        }
        eng->tp_hist[c][0] = eng->tp_hist[c][1];
    }

    eng->lim_gain[0] = gain;
    for (int c = 1; c < ch; ++c)
        eng->lim_gain[c] = gain;
}

static void process_block(DspEngine* eng, sample_t* interleaved, int frames) {
    const int ch = eng->channels;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
            eng->temp[c][i] = interleaved[i * ch + c];

#ifdef DSP_USE_DOUBLE
    if (eng->eq_enabled) {
        for (int c = 0; c < ch; ++c) eng->eq_ptrs[c] = eng->temp[c];
        eng->eq.process(eng->eq_ptrs, frames);
    }
#else
    if (eng->eq_enabled) {
        static thread_local double dbuf[kMaxChannels][kMaxBlock];
        double* ptrs[kMaxChannels];
        for (int c = 0; c < ch; ++c) {
            for (int i = 0; i < frames; ++i)
                dbuf[c][i] = static_cast<double>(eng->temp[c][i]);
            ptrs[c] = dbuf[c];
        }
        eng->eq.process(ptrs, frames);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < frames; ++i)
                eng->temp[c][i] = static_cast<sample_t>(dbuf[c][i]);
    }
#endif

    if (eng->dvc.enabled()) {
        sample_t* ptrs[kMaxChannels];
        for (int c = 0; c < ch; ++c)
            ptrs[c] = eng->temp[c];
        eng->dvc.process_planar(ptrs, frames, ch);
    }

    if (eng->limiter_enabled) {
        if (eng->limiter_mode == DSP_LIMITER_TRUEPEAK)
            limiter_truepeak(eng, frames);
        else if (eng->limiter_mode == DSP_LIMITER_LOOKAHEAD && eng->lim_L > 0)
            limiter_lookahead(eng, frames);
        else
            limiter_fast(eng, frames);
    }

    if (eng->dither_enabled && eng->dither_mode != DSP_DITHER_NONE) {
        constexpr float kScale = 32768.f;
        constexpr float kInv = 1.f / 32768.f;
        for (int c = 0; c < ch; ++c) {
            uint32_t s = eng->dither_state[c];
            float err = 0.f;
            for (int i = 0; i < frames; ++i) {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                float r1 = (s & 0xFFFFFF) / float(0x1000000) - 0.5f;
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                float r2 = (s & 0xFFFFFF) / float(0x1000000) - 0.5f;
                float noise = r1 + r2;
                float x = static_cast<float>(eng->temp[c][i]);
                if (eng->dither_mode == DSP_DITHER_SHAPED) x += err;
                float scaled = x * kScale + noise;
                float q = std::floor(scaled + 0.5f);
                if (q > 32767.f) q = 32767.f;
                if (q < -32768.f) q = -32768.f;
                if (eng->dither_mode == DSP_DITHER_SHAPED) err = (scaled - q) * 0.5f;
                eng->temp[c][i] = static_cast<sample_t>(q * kInv);
            }
            eng->dither_state[c] = s;
        }
    }

    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
            interleaved[i * ch + c] = eng->temp[c][i];
}

extern "C" {

const char* dsp_version(void) { return "0.3.2"; }

DspEngine* dsp_create(const DspConfig* cfg) {
    if (!cfg || cfg->channels < 1 || cfg->channels > kMaxChannels ||
        cfg->max_block < 16 || cfg->max_block > kMaxBlock)
        return nullptr;
    auto* eng = new (std::nothrow) DspEngine();
    if (!eng) return nullptr;
    eng->sample_rate = cfg->sample_rate > 0 ? cfg->sample_rate : 48000.f;
    eng->channels    = cfg->channels;
    eng->max_block   = cfg->max_block;
    eng->eq_enabled      = cfg->enable_eq != 0;
    eng->dvc.set_enabled(cfg->enable_dvc != 0);
    eng->limiter_enabled = cfg->enable_limiter != 0;
    eng->dither_enabled  = cfg->enable_dither != 0;
    eng->dither_mode     = cfg->dither_mode;
    eng->limiter_threshold_db = cfg->limiter_threshold_db;
    eng->limiter_release_ms   = cfg->limiter_release_ms > 0 ? cfg->limiter_release_ms : 50.f;
    eng->update_limiter_coeffs();
    eng->eq.init(eng->sample_rate, eng->channels);
    eng->eq.set_enabled(eng->eq_enabled);
    return eng;
}

void dsp_destroy(DspEngine* eng) { delete eng; }

DspError dsp_reset(DspEngine* eng) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->eq.reset_state();
    for (int c = 0; c < eng->channels; ++c) {
        eng->lim_env[c]  = sample_t(0);
        eng->lim_gain[c] = sample_t(1);
    }
    eng->src.reset();
    eng->dvc.reset();
    eng->clear_lookahead_state();
    return DSP_OK;
}

DspError dsp_set_sample_rate(DspEngine* eng, float rate) {
    if (!eng || rate < 8000.f || rate > 384000.f) return DSP_ERR_INVALID_ARG;
    eng->sample_rate = rate;
    eng->update_limiter_coeffs();
    eng->clear_lookahead_state();
    eng->rebuild_eq();
    return DSP_OK;
}

DspError dsp_set_channels(DspEngine* eng, int channels) {
    if (!eng || channels < 1 || channels > kMaxChannels) return DSP_ERR_INVALID_ARG;
    eng->channels = channels;
    eng->clear_lookahead_state();
    eng->rebuild_eq();
    return DSP_OK;
}

DspError dsp_process(DspEngine* eng, const float* in, float* out, int frames) {
    if (!eng || !in || !out || frames < 1)
        return DSP_ERR_INVALID_ARG;

    const int ch = eng->channels;
    const int maxb = eng->max_block;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (n > maxb) n = maxb;
        const int samples = n * ch;
        const int base = done * ch;
        for (int i = 0; i < samples; ++i)
            eng->work[i] = static_cast<sample_t>(in[base + i]);
        process_block(eng, eng->work, n);
        for (int i = 0; i < samples; ++i)
            out[base + i] = static_cast<float>(eng->work[i]);
        done += n;
    }
    return DSP_OK;
}

DspError dsp_process_src(DspEngine* eng, const float* in, int in_frames,
                         float* out, int* out_frames, float ratio, DspSrcQuality quality) {
    if (!eng || !in || !out || !out_frames || in_frames < 1 || ratio <= 0.f)
        return DSP_ERR_INVALID_ARG;

    const int ch = eng->channels;
    DspError err = dsp::resample_interleaved(
        eng->src, in, in_frames, out, out_frames, ch, ratio, quality);
    if (err != DSP_OK) return err;

    const int written = *out_frames;
    int done = 0;
    while (done < written) {
        int n = written - done;
        if (n > eng->max_block) n = eng->max_block;
        const int samples = n * ch;
        for (int i = 0; i < samples; ++i)
            eng->work[i] = static_cast<sample_t>(out[done * ch + i]);
        process_block(eng, eng->work, n);
        for (int i = 0; i < samples; ++i)
            out[done * ch + i] = static_cast<float>(eng->work[i]);
        done += n;
    }
    return DSP_OK;
}

DspError dsp_eq_enable(DspEngine* eng, int enable) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->eq_enabled = enable != 0;
    eng->eq.set_enabled(eng->eq_enabled);
    return DSP_OK;
}

DspError dsp_eq_set_bands(DspEngine* eng, const DspEqBand* bands, int count) {
    if (!eng || (!bands && count > 0) || count < 0 || count > kMaxEqBands)
        return DSP_ERR_INVALID_ARG;
    eng->eq_band_count = count;
    if (count > 0)
        std::memcpy(eng->eq_bands, bands, static_cast<size_t>(count) * sizeof(DspEqBand));
    eng->rebuild_eq();
    return DSP_OK;
}

DspError dsp_eq_set_band(DspEngine* eng, int index, const DspEqBand* band) {
    if (!eng || !band || index < 0 || index >= kMaxEqBands) return DSP_ERR_INVALID_ARG;
    if (index >= eng->eq_band_count) eng->eq_band_count = index + 1;
    eng->eq_bands[index] = *band;
    eng->rebuild_eq();
    return DSP_OK;
}

int dsp_eq_band_count(const DspEngine* eng) { return eng ? eng->eq_band_count : 0; }

DspError dsp_dvc_set_gain_db(DspEngine* eng, float gain_db) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->dvc.set_gain_db(gain_db);
    return DSP_OK;
}

float dsp_dvc_get_gain_db(const DspEngine* eng) {
    return eng ? eng->dvc.gain_db() : 0.f;
}

DspError dsp_dvc_enable(DspEngine* eng, int enable) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->dvc.set_enabled(enable != 0);
    return DSP_OK;
}

DspError dsp_limiter_enable(DspEngine* eng, int enable) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->limiter_enabled = enable != 0;
    return DSP_OK;
}

DspError dsp_limiter_set_threshold(DspEngine* eng, float threshold_db) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->limiter_threshold_db = threshold_db;
    eng->update_limiter_coeffs();
    return DSP_OK;
}

DspError dsp_limiter_set_release(DspEngine* eng, float release_ms) {
    if (!eng || release_ms <= 0.f) return DSP_ERR_INVALID_ARG;
    eng->limiter_release_ms = release_ms;
    eng->update_limiter_coeffs();
    return DSP_OK;
}

DspError dsp_limiter_set_mode(DspEngine* eng, DspLimiterMode mode) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    if (mode != DSP_LIMITER_FAST && mode != DSP_LIMITER_LOOKAHEAD &&
        mode != DSP_LIMITER_TRUEPEAK)
        return DSP_ERR_INVALID_ARG;
    eng->limiter_mode = mode;
    eng->update_limiter_coeffs();
    eng->clear_lookahead_state();
    return DSP_OK;
}

DspError dsp_limiter_set_ceiling(DspEngine* eng, float ceiling_db) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->limiter_ceiling_db = ceiling_db;
    eng->update_limiter_coeffs();
    return DSP_OK;
}

DspError dsp_limiter_set_lookahead_ms(DspEngine* eng, float lookahead_ms) {
    if (!eng || lookahead_ms < 0.f) return DSP_ERR_INVALID_ARG;
    eng->limiter_lookahead_ms = lookahead_ms;
    eng->update_limiter_coeffs();
    eng->clear_lookahead_state();
    return DSP_OK;
}

DspError dsp_limiter_set_attack_ms(DspEngine* eng, float attack_ms) {
    if (!eng || attack_ms <= 0.f) return DSP_ERR_INVALID_ARG;
    eng->limiter_attack_ms = attack_ms;
    eng->update_limiter_coeffs();
    return DSP_OK;
}

float dsp_limiter_get_threshold(const DspEngine* eng) {
    return eng ? eng->limiter_threshold_db : 0.f;
}

float dsp_limiter_get_release(const DspEngine* eng) {
    return eng ? eng->limiter_release_ms : 0.f;
}

float dsp_limiter_get_ceiling(const DspEngine* eng) {
    return eng ? eng->limiter_ceiling_db : 0.f;
}

float dsp_limiter_get_lookahead_ms(const DspEngine* eng) {
    return eng ? eng->limiter_lookahead_ms : 0.f;
}

float dsp_limiter_get_attack_ms(const DspEngine* eng) {
    return eng ? eng->limiter_attack_ms : 0.f;
}

DspLimiterMode dsp_limiter_get_mode(const DspEngine* eng) {
    return eng ? eng->limiter_mode : DSP_LIMITER_FAST;
}

int dsp_limiter_is_enabled(const DspEngine* eng) {
    return eng && eng->limiter_enabled ? 1 : 0;
}

int dsp_limiter_get_latency_samples(const DspEngine* eng) {
    if (!eng || !eng->limiter_enabled) return 0;
    if (eng->limiter_mode != DSP_LIMITER_LOOKAHEAD &&
        eng->limiter_mode != DSP_LIMITER_TRUEPEAK)
        return 0;
    return eng->lim_L > 0 ? eng->lim_L : 0;
}

float dsp_limiter_get_gr_db(const DspEngine* eng) {
    if (!eng) return 0.f;
    float g = static_cast<float>(eng->lim_gr);
    if (g <= 1e-12f) return 120.f;
    if (g >= 1.f) return 0.f;
    return -20.f * std::log10(g);
}

DspError dsp_dither_enable(DspEngine* eng, int enable) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->dither_enabled = enable != 0;
    return DSP_OK;
}

DspError dsp_dither_set_mode(DspEngine* eng, DspDitherMode mode) {
    if (!eng) return DSP_ERR_INVALID_ARG;
    eng->dither_mode = mode;
    return DSP_OK;
}

DspDitherMode dsp_dither_get_mode(const DspEngine* eng) {
    return eng ? eng->dither_mode : DSP_DITHER_NONE;
}

int dsp_dither_is_enabled(const DspEngine* eng) {
    return eng && eng->dither_enabled ? 1 : 0;
}

} // extern "C"
