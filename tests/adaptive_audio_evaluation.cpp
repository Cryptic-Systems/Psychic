/*
 * Offline AACE evaluation against decoded programme material.
 * Input: 48 kHz, stereo, IEEE float32 WAV (convert MP3 with ffmpeg first).
 * This links to the real Psychic engine and records per-block telemetry.
 */
#include "dsp_engine.h"
#include "core/adaptive_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
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
            if (size < 16 || !f.read(reinterpret_cast<char*>(&w.format), 2) ||
                !f.read(reinterpret_cast<char*>(&w.channels), 2) ||
                !f.read(reinterpret_cast<char*>(&w.sample_rate), 4)) return false;
            uint32_t byte_rate = 0;
            if (!f.read(reinterpret_cast<char*>(&byte_rate), 4) ||
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

    DspEngine* eng = dsp_create(&cfg);
    if (!eng) { std::fprintf(stderr, "dsp_create failed\n"); return 3; }
    dsp_auto_bass_enable(eng, 1);
    dsp_auto_bass_set_target_db(eng, 6.0f);
    dsp_auto_bass_set_margin_db(eng, 1.0f);
    dsp_auto_preamp_enable(eng, 1);
    dsp_auto_preamp_set_target_db(eng, 3.0f);
    dsp_auto_gain_set_target_rms_db(eng, -18.0f);
    dsp_auto_gain_set_max_boost_db(eng, 6.0f);
    dsp_auto_gain_set_max_cut_db(eng, 12.0f);
    dsp_auto_gain_set_window_ms(eng, 400.0f);
    dsp_auto_gain_enable(eng, 1);
    dsp_headroom_budget_set(eng, 0.50f, 0.20f, 0.30f);

    std::ofstream csv(argv[2]);
    if (!csv) { dsp_destroy(eng); return 4; }
    csv << "time_s,input_rms_db,output_rms_db,source_peak_db,track_short_rms_db,"
           "track_long_rms_db,trend_db,transition,confidence,hint_db,"
           "bass_target_db,bass_applied_db,preamp_target_db,preamp_applied_db,"
           "gain_target_db,gain_applied_db,limiter_gr_db,available_headroom_db,"
           "bass_reasons,preamp_reasons,gain_reasons\n";

    constexpr int block = 480;
    std::vector<float> output(static_cast<size_t>(block) * 2);
    double in_sq_sum = 0.0, out_sq_sum = 0.0;
    uint64_t in_n = 0, out_n = 0;
    float min_gain = 100.f, max_gain = -100.f, max_gr = 0.f, max_hint = 0.f;
    uint64_t rising = 0, falling = 0, steady = 0, rows = 0;
    float min_output_delta = 100.f, max_output_delta = -100.f;
    for (size_t offset = 0; offset < samples / 2; offset += block) {
        const int frames = static_cast<int>(std::min<size_t>(block, samples / 2 - offset));
        const float* in = input.data() + offset * 2;
        if (dsp_process(eng, in, output.data(), frames) != DSP_OK) {
            std::fprintf(stderr, "dsp_process failed at %.3f s\n",
                         static_cast<double>(offset) / wi.sample_rate);
            dsp_destroy(eng); return 5;
        }
        double in_ms = 0.0, out_ms = 0.0;
        for (int i = 0; i < frames * 2; ++i) {
            in_ms += static_cast<double>(in[i]) * in[i];
            out_ms += static_cast<double>(output[static_cast<size_t>(i)]) *
                      output[static_cast<size_t>(i)];
        }
        in_ms /= frames * 2;
        out_ms /= frames * 2;
        in_sq_sum += in_ms * frames * 2;
        out_sq_sum += out_ms * frames * 2;
        in_n += frames * 2; out_n += frames * 2;
        const float input_db = dbfs(in_ms), output_db = dbfs(out_ms);
        const float delta = output_db - input_db;
        min_output_delta = std::min(min_output_delta, delta);
        max_output_delta = std::max(max_output_delta, delta);

        dsp::AdaptiveDecisionSnapshot t{};
        if (!dsp_internal_read_adaptive_telemetry(eng, &t)) continue;
        const float gr = dsp_limiter_get_gr_db(eng);
        min_gain = std::min(min_gain, t.gain_applied_db);
        max_gain = std::max(max_gain, t.gain_applied_db);
        max_gr = std::max(max_gr, gr);
        max_hint = std::max(max_hint, std::fabs(t.anticipatory_gain_hint_db));
        if (t.section_transition == dsp::kSectionRising) ++rising;
        else if (t.section_transition == dsp::kSectionFalling) ++falling;
        else ++steady;
        csv << (static_cast<double>(offset) / wi.sample_rate) << ','
            << input_db << ',' << output_db << ',' << t.source_peak_db << ','
            << t.track_short_rms_db << ',' << t.track_long_rms_db << ','
            << t.track_loudness_trend_db << ',' << t.section_transition << ','
            << t.section_transition_confidence << ',' << t.anticipatory_gain_hint_db << ','
            << t.bass_target_db << ',' << t.bass_applied_db << ','
            << t.preamp_target_db << ',' << t.preamp_applied_db << ','
            << t.gain_target_db << ',' << t.gain_applied_db << ','
            << gr << ',' << t.available_headroom_db << ','
            << t.bass_reasons << ',' << t.preamp_reasons << ',' << t.gain_reasons << '\n';
        ++rows;
    }
    const float overall_in = dbfs(in_sq_sum / std::max<uint64_t>(1, in_n));
    const float overall_out = dbfs(out_sq_sum / std::max<uint64_t>(1, out_n));
    std::printf("Psychic full-engine evaluation: %s\n", argv[1]);
    std::printf("duration_s=%.3f blocks=%llu\n",
        static_cast<double>(samples / 2) / wi.sample_rate,
        static_cast<unsigned long long>(rows));
    std::printf("input_rms_db=%.2f output_rms_db=%.2f net_level_change_db=%.2f\n",
        overall_in, overall_out, overall_out - overall_in);
    std::printf("gain_applied_range_db=%.2f..%.2f limiter_gr_peak_db=%.2f\n",
        min_gain, max_gain, max_gr);
    std::printf("transition_blocks rising=%llu falling=%llu steady=%llu hint_abs_max_db=%.2f\n",
        static_cast<unsigned long long>(rising), static_cast<unsigned long long>(falling),
        static_cast<unsigned long long>(steady), max_hint);
    std::printf("per_block_output_delta_range_db=%.2f..%.2f\n",
        min_output_delta, max_output_delta);
    std::printf("csv=%s\n", argv[2]);
    dsp_destroy(eng);
    return 0;
}
