/*
 * Offline AACE evaluation against decoded programme material.
 * Input: 48 kHz, stereo, IEEE float32 WAV (convert MP3 with ffmpeg first).
 * All profiles link to and run the real Psychic dsp_process() implementation.
 */
#include "dsp_engine.h"
#include "core/adaptive_controller.h"
#include "core/track_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

bool dsp_internal_read_adaptive_telemetry(
    DspEngine* eng, dsp::AdaptiveDecisionSnapshot* out) noexcept;

namespace {
struct WavInfo {
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    std::streamoff data_offset = 0;
    uint32_t data_bytes = 0;
};

bool read_wav_header(std::ifstream& f, WavInfo& w) {
    char riff[4], wave[4];
    uint32_t riff_size = 0;
    if (!f.read(riff, 4) || !f.read(reinterpret_cast<char*>(&riff_size), 4) ||
        !f.read(wave, 4) || std::memcmp(riff, "RIFF", 4) ||
        std::memcmp(wave, "WAVE", 4)) return false;
    bool have_fmt = false, have_data = false;
    while (f && !(have_fmt && have_data)) {
        char id[4]; uint32_t size = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&size), 4)) break;
        const auto start = f.tellg();
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t align = 0;
            uint32_t byte_rate = 0;
            if (size < 16 || !f.read(reinterpret_cast<char*>(&w.format), 2) ||
                !f.read(reinterpret_cast<char*>(&w.channels), 2) ||
                !f.read(reinterpret_cast<char*>(&w.sample_rate), 4) ||
                !f.read(reinterpret_cast<char*>(&byte_rate), 4) ||
                !f.read(reinterpret_cast<char*>(&align), 2) ||
                !f.read(reinterpret_cast<char*>(&w.bits), 2)) return false;
            have_fmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            w.data_offset = start;
            w.data_bytes = size;
            have_data = true;
        }
        f.seekg(start + static_cast<std::streamoff>(size + (size & 1u)));
    }
    return have_fmt && have_data;
}

float dbfs(double ms) {
    return static_cast<float>(10.0 * std::log10(std::max(1.0e-30, ms)));
}

void configure_auto_gain(DspEngine* eng, float window_ms, float max_cut_db) {
    dsp_auto_gain_set_target_rms_db(eng, -18.0f);
    dsp_auto_gain_set_max_boost_db(eng, 6.0f);
    dsp_auto_gain_set_max_cut_db(eng, max_cut_db);
    dsp_auto_gain_set_window_ms(eng, window_ms);
    dsp_auto_gain_enable(eng, 1);
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: adaptive_audio_evaluation input.wav output.csv\n");
        return 2;
    }
    std::ifstream wav(argv[1], std::ios::binary);
    WavInfo wi{};
    if (!wav || !read_wav_header(wav, wi) || wi.format != 3 ||
        wi.bits != 32 || wi.channels != 2 || wi.sample_rate != 48000) {
        std::fprintf(stderr, "Expected stereo 48 kHz IEEE-float32 WAV: %s\n", argv[1]);
        return 2;
    }
    const size_t samples = wi.data_bytes / sizeof(float);
    std::vector<float> input(samples);
    wav.seekg(wi.data_offset);
    if (!wav.read(reinterpret_cast<char*>(input.data()),
                  static_cast<std::streamsize>(samples * sizeof(float)))) {
        std::fprintf(stderr, "Could not read WAV samples\n");
        return 2;
    }

    DspConfig cfg{};
    cfg.sample_rate = 48000.f;
    cfg.channels = 2;
    cfg.max_block = 480;
    cfg.enable_eq = 0;
    cfg.enable_dvc = 1;
    cfg.enable_limiter = 1;
    cfg.enable_dither = 0;
    cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -1.0f;
    cfg.limiter_release_ms = 50.f;
    cfg.limiter_mode = DSP_LIMITER_TRUEPEAK;
    cfg.limiter_ceiling_db = -0.1f;
    cfg.limiter_attack_ms = 0.1f;
    cfg.limiter_lookahead_ms = 2.0f;

    enum { kBaseline, kGain400Cut12, kGain800Cut12, kGain400Cut6, kFullAdaptive, kProfileCount };
    DspEngine* engines[kProfileCount]{};
    for (int i = 0; i < kProfileCount; ++i) {
        engines[i] = dsp_create(&cfg);
        if (!engines[i]) {
            std::fprintf(stderr, "dsp_create failed for profile %d\n", i);
            for (auto* e : engines) if (e) dsp_destroy(e);
            return 3;
        }
    }
    configure_auto_gain(engines[kGain400Cut12], 400.f, 12.f);
    configure_auto_gain(engines[kGain800Cut12], 800.f, 12.f);
    configure_auto_gain(engines[kGain400Cut6], 400.f, 6.f);

    DspEngine* full = engines[kFullAdaptive];
    dsp_auto_bass_enable(full, 1);
    dsp_auto_bass_set_target_db(full, 6.0f);
    dsp_auto_bass_set_margin_db(full, 1.0f);
    dsp_auto_preamp_enable(full, 1);
    dsp_auto_preamp_set_target_db(full, 3.0f);
    configure_auto_gain(full, 400.f, 6.f);
    dsp_headroom_budget_set(full, 0.50f, 0.20f, 0.30f);

    std::ofstream csv(argv[2]);
    if (!csv) { for (auto* e : engines) dsp_destroy(e); return 4; }
    csv << "time_s,input_rms_db,baseline_output_rms_db,gain400_cut12_rms_db,"
           "gain800_cut12_rms_db,gain400_cut6_rms_db,full_adaptive_output_rms_db,"
           "gain400_cut12_minus_baseline_db,gain800_cut12_minus_baseline_db,"
           "gain400_cut6_minus_baseline_db,full_adaptive_minus_baseline_db,"
           "source_peak_db,track_short_rms_db,track_long_rms_db,trend_db,"
           "transition,confidence,hint_db,bass_target_db,bass_applied_db,"
           "preamp_target_db,preamp_applied_db,gain_target_db,gain_applied_db,"
           "limiter_gr_db,available_headroom_db,bass_reasons,preamp_reasons,gain_reasons\n";

    constexpr int block = 480;
    std::vector<float> output[kProfileCount];
    for (auto& v : output) v.resize(static_cast<size_t>(block) * 2);
    double total_sq[kProfileCount]{};
    uint64_t total_n = 0;
    float min_gain = 100.f, max_gain = -100.f, max_gr = 0.f, max_hint = 0.f;
    uint64_t rising = 0, falling = 0, steady = 0, rows = 0;
    float min_delta[kProfileCount]{}, max_delta[kProfileCount]{};
    uint64_t active_blocks[kProfileCount]{};
    uint64_t below_3db_blocks[kProfileCount]{}, below_6db_blocks[kProfileCount]{};
    uint64_t current_attenuation_run[kProfileCount]{}, max_attenuation_run[kProfileCount]{};
    dsp::AdaptiveTrackAnalyzer track_analyzer;
    for (int i = 0; i < kProfileCount; ++i) {
        min_delta[i] = 100.f;
        max_delta[i] = -100.f;
    }

    for (size_t offset = 0; offset < samples / 2; offset += block) {
        const int frames = static_cast<int>(std::min<size_t>(block, samples / 2 - offset));
        const float* in = input.data() + offset * 2;
        for (int p = 0; p < kProfileCount; ++p) {
            if (dsp_process(engines[p], in, output[p].data(), frames) != DSP_OK) {
                std::fprintf(stderr, "dsp_process failed at %.3f s, profile %d\n",
                    static_cast<double>(offset) / wi.sample_rate, p);
                for (auto* e : engines) dsp_destroy(e);
                return 5;
            }
        }

        double in_ms = 0.0;
        double block_ms[kProfileCount]{};
        for (int i = 0; i < frames * 2; ++i) {
            in_ms += static_cast<double>(in[i]) * in[i];
            for (int p = 0; p < kProfileCount; ++p)
                block_ms[p] += static_cast<double>(output[p][static_cast<size_t>(i)]) *
                               output[p][static_cast<size_t>(i)];
        }
        in_ms /= frames * 2;
        const float input_db = dbfs(in_ms);
        float output_levels[kProfileCount]{};
        for (int p = 0; p < kProfileCount; ++p) {
            block_ms[p] /= frames * 2;
            total_sq[p] += block_ms[p] * frames * 2;
            output_levels[p] = dbfs(block_ms[p]);
            const float delta = output_levels[p] - dbfs(block_ms[kBaseline]);
            min_delta[p] = std::min(min_delta[p], delta);
            max_delta[p] = std::max(max_delta[p], delta);
            if (p != kBaseline && input_db > -60.0f) {
                ++active_blocks[p];
                if (delta < -3.0f) ++below_3db_blocks[p];
                if (delta < -6.0f) ++below_6db_blocks[p];
                if (delta < -3.0f) {
                    ++current_attenuation_run[p];
                    max_attenuation_run[p] = std::max(
                        max_attenuation_run[p], current_attenuation_run[p]);
                } else {
                    current_attenuation_run[p] = 0;
                }
            }
        }
        total_n += frames * 2;

        dsp::AdaptiveDecisionSnapshot t{};
        if (!dsp_internal_read_adaptive_telemetry(full, &t)) continue;
        track_analyzer.add_block(input_db, t.source_peak_db,
            t.track_bass_rms_db, static_cast<double>(frames) / wi.sample_rate,
            t.section_transition, t.section_transition_confidence);
        const float gr = dsp_limiter_get_gr_db(full);
        min_gain = std::min(min_gain, t.gain_applied_db);
        max_gain = std::max(max_gain, t.gain_applied_db);
        max_gr = std::max(max_gr, gr);
        max_hint = std::max(max_hint, std::fabs(t.anticipatory_gain_hint_db));
        if (t.section_transition == dsp::kSectionRising) ++rising;
        else if (t.section_transition == dsp::kSectionFalling) ++falling;
        else ++steady;

        csv << (static_cast<double>(offset) / wi.sample_rate) << ',' << input_db;
        for (int p = 0; p < kProfileCount; ++p) csv << ',' << output_levels[p];
        for (int p = 1; p < kProfileCount; ++p)
            csv << ',' << (output_levels[p] - output_levels[kBaseline]);
        csv << ',' << t.source_peak_db << ',' << t.track_short_rms_db << ','
            << t.track_long_rms_db << ',' << t.track_loudness_trend_db << ','
            << t.section_transition << ',' << t.section_transition_confidence << ','
            << t.anticipatory_gain_hint_db << ',' << t.bass_target_db << ','
            << t.bass_applied_db << ',' << t.preamp_target_db << ','
            << t.preamp_applied_db << ',' << t.gain_target_db << ','
            << t.gain_applied_db << ',' << gr << ',' << t.available_headroom_db << ','
            << t.bass_reasons << ',' << t.preamp_reasons << ',' << t.gain_reasons << '\n';
        ++rows;
    }

    float overall[kProfileCount]{};
    for (int p = 0; p < kProfileCount; ++p)
        overall[p] = dbfs(total_sq[p] / std::max<uint64_t>(1, total_n));
    std::printf("Psychic full-engine evaluation: %s\\n", argv[1]);
    std::printf("duration_s=%.3f blocks=%llu\\n",
        static_cast<double>(samples / 2) / wi.sample_rate,
        static_cast<unsigned long long>(rows));
    std::printf("baseline=%.2f gain400_cut12=%.2f gain800_cut12=%.2f gain400_cut6=%.2f full_adaptive=%.2f dB\\n",
        overall[kBaseline], overall[kGain400Cut12], overall[kGain800Cut12],
        overall[kGain400Cut6], overall[kFullAdaptive]);
    std::printf("delta_vs_baseline: gain400_cut12=%.2f gain800_cut12=%.2f gain400_cut6=%.2f full=%.2f dB\\n",
        overall[kGain400Cut12] - overall[kBaseline],
        overall[kGain800Cut12] - overall[kBaseline],
        overall[kGain400Cut6] - overall[kBaseline],
        overall[kFullAdaptive] - overall[kBaseline]);
    std::printf("per_block_delta_ranges: gain400_cut12=%.2f..%.2f gain800_cut12=%.2f..%.2f gain400_cut6=%.2f..%.2f full=%.2f..%.2f dB\\n",
        min_delta[kGain400Cut12], max_delta[kGain400Cut12],
        min_delta[kGain800Cut12], max_delta[kGain800Cut12],
        min_delta[kGain400Cut6], max_delta[kGain400Cut6],
        min_delta[kFullAdaptive], max_delta[kFullAdaptive]);
    std::printf("gain_applied_range_db=%.2f..%.2f limiter_gr_peak_db=%.2f\n",
        min_gain, max_gain, max_gr);
    for (int p = 1; p < kProfileCount; ++p) {
        const double active = static_cast<double>(std::max<uint64_t>(1, active_blocks[p]));
        std::printf("profile_%d_attenuation: below_minus3db=%.1f%% below_minus6db=%.1f%% max_contiguous_below_minus3db=%.2fs\\n",
            p, 100.0 * below_3db_blocks[p] / active,
            100.0 * below_6db_blocks[p] / active,
            static_cast<double>(max_attenuation_run[p]) * block / wi.sample_rate);
    }
    std::printf("transition_blocks rising=%llu falling=%llu steady=%llu hint_abs_max_db=%.2f\n",
        static_cast<unsigned long long>(rising), static_cast<unsigned long long>(falling),
        static_cast<unsigned long long>(steady), max_hint);
    const auto track = track_analyzer.summary();
    std::printf("track_profile: duration=%.2fs active=%.2fs silence=%.2fs\n",
        track.duration_seconds, track.active_seconds, track.silence_seconds);
    std::printf("track_loudness: integrated=%.2fdBFS p10=%.2f p50=%.2f p95=%.2f range=%.2fdB\n",
        track.integrated_rms_db, track.loudness_p10_db, track.loudness_p50_db,
        track.loudness_p95_db, track.loudness_range_db);
    std::printf("track_spectrum_balance: bass_rms=%.2fdBFS bass_prominence=%.2fdB\n",
        track.bass_rms_db, track.bass_prominence_db);
    std::printf("track_peaks: peak=%.2fdBFS mean_crest=%.2fdB max_crest=%.2fdB\n",
        track.sample_peak_db, track.mean_crest_db, track.max_crest_db);
    std::printf("track_transitions: rising=%.2fs falling=%.2fs total=%.2fs\n",
        track.rising_seconds, track.falling_seconds, track.transition_seconds);
    std::printf("csv=%s\n", argv[2]);
    for (auto* e : engines) dsp_destroy(e);
    return 0;
}
