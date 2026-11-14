/**
 * Unit tests for DSP ENGINE v0.3.2
 */

#include "test_framework.h"
#include "dsp_engine.h"
#include "audio/ring_buffer.h"
#include "utils/math_utils.h"
#include "core/resampler.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <limits>

using namespace dsp_test;

static DspEngine* create_engine(int channels = 2, float rate = 48000.f) {
    DspConfig cfg{};
    cfg.sample_rate = rate;
    cfg.channels = channels;
    cfg.max_block = 1024;
    cfg.enable_eq = 1;
    cfg.enable_dvc = 1;
    cfg.enable_limiter = 1;
    cfg.enable_dither = 0;
    cfg.dither_mode = DSP_DITHER_TPDF;
    cfg.limiter_threshold_db = -1.0f;
    cfg.limiter_release_ms = 50.f;
    return dsp_create(&cfg);
}

TEST(version) {
    const char* v = dsp_version();
    EXPECT_TRUE(v != nullptr);
    EXPECT_TRUE(std::strstr(v, "0.3") != nullptr);
}

TEST(lifecycle) {
    DspEngine* eng = create_engine();
    EXPECT_TRUE(eng != nullptr);
    EXPECT_EQ(dsp_reset(eng), DSP_OK);
    dsp_destroy(eng);
}

TEST(invalid_create) {
    DspConfig cfg{};
    cfg.channels = 0;
    cfg.max_block = 1024;
    EXPECT_TRUE(dsp_create(&cfg) == nullptr);
}

TEST(dvc_gain) {
    DspEngine* eng = create_engine();
    /* Default create enables ~15 ms smoothing; disable for instant-gain check. */
    EXPECT_EQ(dsp_dvc_set_smoothing(eng, 0, 0.f), DSP_OK);
    EXPECT_EQ(dsp_dvc_set_gain_db(eng, -6.0f), DSP_OK);
    EXPECT_NEAR(dsp_dvc_get_gain_db(eng), -6.0f, 1e-5);
    const int frames = 256;
    std::vector<float> in(frames * 2, 0.5f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_NEAR(out[0], 0.5f * 0.501187f, 0.01f);
    dsp_destroy(eng);
}

TEST(dvc_smoothing) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_dvc_set_smoothing(eng, 1, 20.f), DSP_OK);
    EXPECT_EQ(dsp_dvc_get_smoothing(eng), 1);
    EXPECT_NEAR(dsp_dvc_get_smoothing_ms(eng), 20.f, 1e-3);
    EXPECT_EQ(dsp_dvc_set_gain_db(eng, -6.0f), DSP_OK);
    /* ~960 samples ≈ 20 ms @ 48 kHz; after one tau gain ≈ 0.63 toward target. */
    const int frames = 1024;
    std::vector<float> in(frames * 2, 1.0f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_GT(out[0], 0.95f);
    EXPECT_LT(out[(frames - 1) * 2], 0.75f);
    EXPECT_GT(out[(frames - 1) * 2], 0.55f);
    dsp_destroy(eng);
}

TEST(eq_bands) {
    DspEngine* eng = create_engine();
    DspEqBand bands[2] = {{1000.f, 6.0f, 1.0f, 0}, {5000.f, -3.0f, 0.7f, 0}};
    EXPECT_EQ(dsp_eq_set_bands(eng, bands, 2), DSP_OK);
    const int frames = 512;
    std::vector<float> in(frames * 2, 0.1f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_TRUE(std::isfinite(out[0]));
    dsp_destroy(eng);
}

TEST(limiter) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_limiter_enable(eng, 1), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_threshold(eng, -6.0f), DSP_OK);
    dsp_eq_enable(eng, 0);
    dsp_dvc_set_gain_db(eng, 0.f);
    const int frames = 1024;
    std::vector<float> in(frames * 2, 0.9f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    float peak = 0.f;
    for (float s : out) { float a = std::fabs(s); if (a > peak) peak = a; }
    EXPECT_LT(peak, 0.65f);
    dsp_destroy(eng);
}

TEST(dither) {
    DspEngine* eng = create_engine();
    dsp_eq_enable(eng, 0); dsp_limiter_enable(eng, 0); dsp_dvc_set_gain_db(eng, 0.f);
    EXPECT_EQ(dsp_dither_enable(eng, 1), DSP_OK);
    const int frames = 256;
    std::vector<float> in(frames * 2, 0.25f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_NEAR(out[0], 0.25f, 3.0f / 32768.f);
    dsp_destroy(eng);
}

TEST(src_linear) {
    DspEngine* eng = create_engine();
    const int in_frames = 128;
    std::vector<float> in(in_frames * 2, 0.2f), out(256 * 2);
    int out_frames = 256;
    EXPECT_EQ(dsp_process_src(eng, in.data(), in_frames, out.data(), &out_frames, 0.5f, DSP_SRC_LINEAR), DSP_OK);
    EXPECT_GT(out_frames, 0);
    dsp_destroy(eng);
}

TEST(src_cubic) {
    DspEngine* eng = create_engine();
    const int in_frames = 128;
    std::vector<float> in(in_frames * 2, 0.2f), out(256 * 2);
    int out_frames = 256;
    EXPECT_EQ(dsp_process_src(eng, in.data(), in_frames, out.data(), &out_frames, 1.5f, DSP_SRC_CUBIC), DSP_OK);
    EXPECT_GT(out_frames, 0);
    dsp_destroy(eng);
}

TEST(src_pipeline) {
    DspEngine* eng = create_engine();
    dsp_eq_enable(eng, 0); dsp_limiter_enable(eng, 0); dsp_dither_enable(eng, 0);
    dsp_dvc_enable(eng, 1);
    EXPECT_EQ(dsp_dvc_set_gain_db(eng, -6.0f), DSP_OK);
    const int in_frames = 64;
    std::vector<float> in(in_frames * 2, 0.5f), out(128 * 2);
    int out_frames = 128;
    EXPECT_EQ(dsp_process_src(eng, in.data(), in_frames, out.data(), &out_frames, 1.0f, DSP_SRC_LINEAR), DSP_OK);
    EXPECT_GT(out_frames, 0);
    dsp_destroy(eng);
}

TEST(limiter_lookahead_mode_api) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_limiter_set_mode(eng, DSP_LIMITER_LOOKAHEAD), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_ceiling(eng, -1.0f), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_lookahead_ms(eng, 2.0f), DSP_OK);
    EXPECT_GT(dsp_limiter_get_latency_samples(eng), 0);
    dsp_destroy(eng);
}

TEST(limiter_lookahead_brickwall) {
    DspConfig cfg{};
    cfg.sample_rate = 48000.f; cfg.channels = 2; cfg.max_block = 1024;
    cfg.enable_limiter = 1; cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -1.0f; cfg.limiter_release_ms = 50.f;
    DspEngine* eng = dsp_create(&cfg);
    EXPECT_EQ(dsp_limiter_set_mode(eng, DSP_LIMITER_LOOKAHEAD), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_ceiling(eng, -1.0f), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_lookahead_ms(eng, 2.0f), DSP_OK);
    dsp_limiter_enable(eng, 1);
    const int frames = 512;
    std::vector<float> in(frames * 2, 0.f), out(frames * 2, 0.f);
    in[100 * 2] = 2.0f; in[100 * 2 + 1] = 2.0f;
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    const float ceil_lin = std::pow(10.0f, -1.0f / 20.0f);
    float peak = 0.f;
    for (float s : out) { float a = std::fabs(s); if (a > peak) peak = a; }
    EXPECT_LT(peak, ceil_lin + 0.02f);
    dsp_destroy(eng);
}

TEST(limiter_lookahead_latency) {
    DspEngine* eng = create_engine();
    dsp_limiter_enable(eng, 1);
    EXPECT_EQ(dsp_limiter_set_mode(eng, DSP_LIMITER_LOOKAHEAD), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_lookahead_ms(eng, 1.0f), DSP_OK);
    int L = dsp_limiter_get_latency_samples(eng);
    EXPECT_GE(L, 40); EXPECT_LE(L, 56);
    dsp_destroy(eng);
}

TEST(limiter_truepeak_mode) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_limiter_set_mode(eng, DSP_LIMITER_TRUEPEAK), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_ceiling(eng, -1.0f), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_lookahead_ms(eng, 2.0f), DSP_OK);
    dsp_limiter_enable(eng, 1);
    const int frames = 512;
    std::vector<float> in(frames * 2, 0.f), out(frames * 2, 0.f);
    for (int i = 0; i < frames; ++i) {
        float s = (i % 2 == 0) ? 0.95f : -0.95f;
        in[i * 2] = s; in[i * 2 + 1] = s;
    }
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    const float ceil_lin = std::pow(10.0f, -1.0f / 20.0f);
    float peak = 0.f;
    for (float s : out) { float a = std::fabs(s); if (a > peak) peak = a; }
    EXPECT_LT(peak, ceil_lin + 0.05f);
    dsp_destroy(eng);
}

TEST(limiter_soft_knee_and_ceiling) {
    DspEngine* eng = create_engine();
    dsp_eq_enable(eng, 0); dsp_dvc_set_gain_db(eng, 0.f);
    dsp_limiter_enable(eng, 1);
    EXPECT_EQ(dsp_limiter_set_threshold(eng, -6.0f), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_ceiling(eng, -1.0f), DSP_OK);
    EXPECT_EQ(dsp_limiter_set_knee_db(eng, 6.0f), DSP_OK);
    const int frames = 1024;
    std::vector<float> in(frames * 2, 0.9f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    float peak = 0.f;
    for (float s : out) { float a = std::fabs(s); if (a > peak) peak = a; }
    const float ceil_lin = std::pow(10.0f, -1.0f / 20.0f);
    EXPECT_LT(peak, ceil_lin + 0.05f);
    dsp_destroy(eng);
}

TEST(limiter_config_create) {
    DspConfig cfg{};
    cfg.sample_rate = 48000.f; cfg.channels = 2; cfg.max_block = 512;
    cfg.enable_limiter = 1; cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -3.0f; cfg.limiter_release_ms = 80.f;
    cfg.limiter_mode = DSP_LIMITER_TRUEPEAK;
    cfg.limiter_ceiling_db = -1.0f; cfg.limiter_knee_db = 4.0f;
    cfg.limiter_attack_ms = 0.2f; cfg.limiter_lookahead_ms = 3.0f;
    DspEngine* eng = dsp_create(&cfg);
    EXPECT_TRUE(eng != nullptr);
    EXPECT_EQ(dsp_limiter_get_mode(eng), DSP_LIMITER_TRUEPEAK);
    EXPECT_NEAR(dsp_limiter_get_attack_ms(eng), 0.2f, 1e-5);
    EXPECT_NEAR(dsp_limiter_get_knee_db(eng), 4.0f, 1e-5);
    dsp_destroy(eng);
}

TEST(limiter_meter_gr) {
    DspConfig cfg{};
    cfg.sample_rate = 48000.f; cfg.channels = 2; cfg.max_block = 512;
    cfg.enable_limiter = 1; cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -12.0f; cfg.limiter_release_ms = 50.f;
    cfg.limiter_ceiling_db = -1.0f; cfg.limiter_mode = DSP_LIMITER_FAST;
    DspEngine* eng = dsp_create(&cfg);
    EXPECT_TRUE(eng != nullptr);
    DspLimiterMeter m{};
    EXPECT_EQ(dsp_limiter_get_meter(eng, &m), DSP_OK);
    const int frames = 512;
    std::vector<float> in(frames * 2, 0.9f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_GT(dsp_limiter_get_gr_db(eng), 1.0f);
    EXPECT_EQ(dsp_limiter_reset_meters(eng), DSP_OK);
    dsp_destroy(eng);
}

TEST(ceiling_default_zero_init) {
    DspConfig cfg{};
    cfg.sample_rate = 48000.f;
    cfg.channels = 2;
    cfg.max_block = 256;
    cfg.enable_limiter = 1;
    cfg.limiter_threshold_db = -1.f;
    cfg.limiter_release_ms = 50.f;
    DspEngine* eng = dsp_create(&cfg);
    EXPECT_TRUE(eng != nullptr);
    EXPECT_NEAR(dsp_limiter_get_ceiling(eng), -0.1f, 1e-5);
    dsp_destroy(eng);
}

TEST(process_nan_safe) {
    DspEngine* eng = create_engine();
    dsp_eq_enable(eng, 0);
    dsp_dvc_set_gain_db(eng, 0.f);
    dsp_limiter_enable(eng, 1);
    const int frames = 64;
    std::vector<float> in(frames * 2, 0.1f), out(frames * 2, 0.f);
    in[10] = std::nanf("");
    in[11] = std::numeric_limits<float>::infinity();
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    for (float s : out) EXPECT_TRUE(std::isfinite(s));
    dsp_destroy(eng);
}

TEST(limiter_getters) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_limiter_set_threshold(eng, -3.0f), DSP_OK);
    EXPECT_NEAR(dsp_limiter_get_threshold(eng), -3.0f, 1e-5);
    dsp_destroy(eng);
}

TEST(dither_getters) {
    DspEngine* eng = create_engine();
    EXPECT_EQ(dsp_dither_set_mode(eng, DSP_DITHER_SHAPED), DSP_OK);
    EXPECT_EQ(dsp_dither_get_mode(eng), DSP_DITHER_SHAPED);
    dsp_destroy(eng);
}

TEST(math_db) {
    EXPECT_NEAR(dsp::db_to_linear(0.0), 1.0, 1e-9);
}

TEST(ring_buffer_spsc) {
    dsp::RingBuffer rb(64, 2);
    float in[32]; for (int i = 0; i < 32; ++i) in[i] = float(i);
    EXPECT_EQ(rb.write(in, 16), 16u);
    float out[32] = {};
    EXPECT_EQ(rb.read(out, 16), 16u);
}

TEST(resampler_module) {
    dsp::ResamplerState st;
    float in[8]; for (int i = 0; i < 8; ++i) in[i] = 0.5f;
    float out[16]; int out_frames = 8;
    EXPECT_EQ(dsp::resample_interleaved(st, in, 4, out, &out_frames, 2, 1.0f, DSP_SRC_LINEAR), DSP_OK);
}

TEST(backend_create) {
    DspBackend* be = dsp_backend_create();
    if (be) dsp_backend_destroy(be);
}

TEST(process_silence) {
    DspEngine* eng = create_engine();
    const int frames = 64;
    std::vector<float> in(frames * 2, 0.f), out(frames * 2, 1.f);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    for (float s : out) EXPECT_NEAR(s, 0.f, 1e-6);
    dsp_destroy(eng);
}

TEST(process_large_block) {
    DspConfig cfg{};
    cfg.sample_rate = 48000.f; cfg.channels = 2; cfg.max_block = 128;
    cfg.enable_dvc = 1; cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -1.f; cfg.limiter_release_ms = 50.f;
    DspEngine* eng = dsp_create(&cfg);
    dsp_dvc_set_gain_db(eng, 0.f);
    const int frames = 500;
    std::vector<float> in(frames * 2, 0.25f), out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    EXPECT_NEAR(out[0], 0.25f, 1e-5);
    dsp_destroy(eng);
}

TEST(process_identity) {
    DspEngine* eng = create_engine();
    dsp_eq_enable(eng, 0); dsp_dvc_set_gain_db(eng, 0.f);
    dsp_limiter_enable(eng, 0); dsp_dither_enable(eng, 0);
    const int frames = 128;
    std::vector<float> in(frames * 2);
    for (int i = 0; i < frames * 2; ++i) in[i] = (i % 17) * 0.01f;
    std::vector<float> out(frames * 2);
    EXPECT_EQ(dsp_process(eng, in.data(), out.data(), frames), DSP_OK);
    for (int i = 0; i < frames * 2; ++i) EXPECT_NEAR(out[i], in[i], 1e-5);
    dsp_destroy(eng);
}

int main() { return run_all(); }
