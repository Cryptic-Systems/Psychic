/*
 * Psychic offline playback simulator.
 *
 * Mimics a simple host/player without a platform audio device:
 * decode WAV -> scan track -> apply selected analysis hint -> dsp_process ->
 * write float32 WAV and per-block CSV.
 *
 * Supported input: RIFF/WAVE PCM 16/24/32-bit or IEEE float32, 1-8 channels.
 * This is an offline test harness, not a real-time player.
 */
#include "dsp_engine.h"
#include "core/track_scan_session.h"
#include "core/track_playback_hint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
struct WavInfo {
    uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
    uint32_t sample_rate = 0, data_bytes = 0;
    std::streamoff data_offset = 0;
};

uint16_t u16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}
bool read_exact(std::ifstream& f, void* p, std::size_t n) {
    return static_cast<bool>(f.read(static_cast<char*>(p), static_cast<std::streamsize>(n)));
}

bool read_header(std::ifstream& f, WavInfo& w) {
    unsigned char head[12]{};
    if (!read_exact(f, head, sizeof(head)) ||
        std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4))
        return false;

    // Honor the RIFF boundary and reject chunks that extend past it. This
    // prevents malformed chunk sizes from wrapping seeks or reading past EOF.
    const std::streamoff file_end = [&]() {
        f.seekg(0, std::ios::end);
        const auto p = f.tellg();
        return p < 0 ? std::streamoff{-1} : static_cast<std::streamoff>(p);
    }();
    const uint32_t riff_size = u32(head + 4);
    if (file_end < 12 || riff_size < 4) return false;
    const std::streamoff riff_end = 8 + static_cast<std::streamoff>(riff_size);
    if (riff_end < 12 || riff_end > file_end) return false;
    f.seekg(12);

    bool have_fmt = false, have_data = false;
    while (f && !(have_fmt && have_data)) {
        unsigned char chunk[8]{};
        if (!read_exact(f, chunk, sizeof(chunk))) break;
        const uint32_t size = u32(chunk + 4);
        const auto start = f.tellg();
        if (start < 0) return false;
        const std::streamoff chunk_end = static_cast<std::streamoff>(start) +
                                         static_cast<std::streamoff>(size);
        if (chunk_end < start || chunk_end > riff_end) return false;
        const std::streamoff next = chunk_end + static_cast<std::streamoff>(size & 1u);
        if (next < chunk_end || next > riff_end) return false;

        if (!std::memcmp(chunk, "fmt ", 4)) {
            if (size < 16) return false;
            unsigned char fmt[16]{};
            if (!read_exact(f, fmt, sizeof(fmt))) return false;
            w.format = u16(fmt);
            w.channels = u16(fmt + 2);
            w.sample_rate = u32(fmt + 4);
            w.block_align = u16(fmt + 12);
            w.bits = u16(fmt + 14);
            if (w.format == 0xfffe && size >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: subformat tag is the first WORD of GUID.
                f.seekg(static_cast<std::streamoff>(start) + 24);
                unsigned char sub[2]{};
                if (!read_exact(f, sub, 2)) return false;
                w.format = u16(sub);
            }
            have_fmt = true;
        } else if (!std::memcmp(chunk, "data", 4)) {
            w.data_offset = start;
            w.data_bytes = size;
            have_data = true;
        }
        f.seekg(next);
    }
    if (!have_fmt || !have_data || w.channels < 1 || w.channels > 8 ||
        w.sample_rate < 8000 || w.sample_rate > 384000 || !w.block_align)
        return false;
    if (w.format == 1 && (w.bits != 16 && w.bits != 24 && w.bits != 32))
        return false;
    if (w.format == 3 && w.bits != 32) return false;
    if (w.format != 1 && w.format != 3) return false;
    return w.block_align == w.channels * (w.bits / 8);
}

float decode_sample(const unsigned char* p, uint16_t format, uint16_t bits) {
    if (format == 3) {
        float v = 0.f;
        std::memcpy(&v, p, sizeof(v));
        return std::isfinite(v) ? v : 0.f;
    }
    if (bits == 16) {
        const int16_t v = static_cast<int16_t>(u16(p));
        return static_cast<float>(v) / 32768.f;
    }
    if (bits == 24) {
        int32_t v = static_cast<int32_t>(p[0]) |
            (static_cast<int32_t>(p[1]) << 8) |
            (static_cast<int32_t>(p[2]) << 16);
        if (v & 0x800000) v |= static_cast<int32_t>(0xff000000);
        return static_cast<float>(v) / 8388608.f;
    }
    const int32_t v = static_cast<int32_t>(u32(p));
    return static_cast<float>(static_cast<double>(v) / 2147483648.0);
}

bool load_wav(const char* path, WavInfo& w, std::vector<float>& pcm) {
    std::ifstream f(path, std::ios::binary);
    if (!f || !read_header(f, w)) return false;
    const std::size_t bytes_per_sample = w.bits / 8;
    if (w.data_bytes % w.block_align) return false;
    const std::size_t frames = w.data_bytes / w.block_align;
    if (frames > std::numeric_limits<std::size_t>::max() / w.channels) return false;
    std::vector<unsigned char> raw(w.data_bytes);
    f.seekg(w.data_offset);
    if (!read_exact(f, raw.data(), raw.size())) return false;
    pcm.resize(frames * w.channels);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = decode_sample(raw.data() + i * bytes_per_sample, w.format, w.bits);
    return true;
}

bool write_float_wav(const char* path, uint32_t rate, uint16_t channels,
                     const std::vector<float>& pcm) {
    if (pcm.size() > (std::numeric_limits<uint32_t>::max() - 36u) / 4u)
        return false;
    const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(float));
    unsigned char header[44]{};
    std::memcpy(header, "RIFF", 4);
    const uint32_t riff_size = 36u + data_bytes;
    std::memcpy(header + 4, &riff_size, 4);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    const uint32_t fmt_size = 16;
    std::memcpy(header + 16, &fmt_size, 4);
    const uint16_t format = 3, bits = 32;
    std::memcpy(header + 20, &format, 2);
    std::memcpy(header + 22, &channels, 2);
    std::memcpy(header + 24, &rate, 4);
    const uint32_t byte_rate = rate * channels * 4u;
    const uint16_t align = channels * 4u;
    std::memcpy(header + 28, &byte_rate, 4);
    std::memcpy(header + 32, &align, 2);
    std::memcpy(header + 34, &bits, 2);
    std::memcpy(header + 36, "data", 4);
    std::memcpy(header + 40, &data_bytes, 4);
    std::ofstream out(path, std::ios::binary);
    return out && static_cast<bool>(out.write(reinterpret_cast<const char*>(header), sizeof(header))) &&
        static_cast<bool>(out.write(reinterpret_cast<const char*>(pcm.data()),
                                    static_cast<std::streamsize>(data_bytes)));
}

float rms_db(const float* x, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(10.0 * std::log10(std::max(1.0e-30, sum / std::max<std::size_t>(1, n))));
}
float peak_db(const float* x, std::size_t n) {
    float peak = 0.f;
    for (std::size_t i = 0; i < n; ++i) peak = std::max(peak, std::fabs(x[i]));
    return peak > 1.0e-12f ? 20.f * std::log10(peak) : -120.f;
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "Usage: psychic_playback_simulator input.wav output.wav [none|bass|crest|both]\n");
        return 2;
    }
    const std::string mode = argc == 4 ? argv[3] : "both";
    if (mode != "none" && mode != "bass" && mode != "crest" && mode != "both") {
        std::fprintf(stderr, "Hint mode must be none, bass, crest, or both.\n");
        return 2;
    }

    WavInfo wav{};
    std::vector<float> input;
    if (!load_wav(argv[1], wav, input)) {
        std::fprintf(stderr, "Could not read supported WAV (PCM 16/24/32 or float32, 1-8 channels): %s\n", argv[1]);
        return 2;
    }
    const std::size_t frames = input.size() / wav.channels;
    constexpr int kBlock = 480;

    // Player-style background scan, completed before playback begins.
    dsp::AdaptiveTrackScanSession scan;
    if (!scan.begin(static_cast<double>(frames) / wav.sample_rate)) {
        std::fprintf(stderr, "Scan begin failed.\n");
        return 3;
    }
    for (std::size_t offset = 0; offset < frames; offset += kBlock) {
        const std::size_t n = std::min<std::size_t>(kBlock, frames - offset);
        if (!scan.add_audio_block(input.data() + offset * wav.channels, n,
                wav.channels, wav.sample_rate)) {
            std::fprintf(stderr, "Track scan failed at frame %zu.\n", offset);
            return 3;
        }
    }
    if (!scan.finalize()) {
        std::fprintf(stderr, "Track scan finalize failed.\n");
        return 3;
    }
    const auto snapshot = scan.snapshot();
    dsp::TrackPlaybackHint full_hint{};
    if (!dsp::make_track_playback_hint(snapshot, &full_hint)) {
        std::fprintf(stderr, "Track report is insufficient for a playback hint. Try a longer active track.\n");
        return 3;
    }

    DspConfig cfg{};
    cfg.sample_rate = static_cast<float>(wav.sample_rate);
    cfg.channels = wav.channels;
    cfg.max_block = kBlock;
    cfg.enable_eq = 0;
    cfg.enable_dvc = 1;
    cfg.enable_limiter = 1;
    cfg.enable_speaker = 1;
    cfg.virtual_bass = 0.35f;
    cfg.enable_dither = 0;
    cfg.dither_mode = DSP_DITHER_NONE;
    cfg.limiter_threshold_db = -1.0f;
    cfg.limiter_release_ms = 50.f;
    cfg.limiter_mode = DSP_LIMITER_TRUEPEAK;
    cfg.limiter_ceiling_db = -0.1f;
    cfg.limiter_attack_ms = 0.1f;
    cfg.limiter_lookahead_ms = 2.0f;
    DspEngine* engine = dsp_create(&cfg);
    if (!engine) {
        std::fprintf(stderr, "dsp_create failed.\n");
        return 4;
    }

    // Enable a modest bass enhancement so the bass hint path can be observed.
    dsp_auto_bass_set_target_db(engine, 4.f);
    dsp_auto_bass_set_margin_db(engine, 1.f);
    dsp_auto_bass_enable(engine, 1);
    dsp_auto_vb_set_target(engine, 0.35f);
    dsp_auto_vb_enable(engine, 1);

    if (mode != "none") {
        DspAnalysisResult hint{};
        if (mode == "bass" || mode == "both")
            hint.bass_energy_db = full_hint.analysis.bass_energy_db;
        else
            hint.bass_energy_db = -120.f;
        if (mode == "crest" || mode == "both")
            hint.crest_db = full_hint.analysis.crest_db;
        const float safe_bass = mode == "both" || mode == "bass"
            ? full_hint.max_safe_bass_db : 24.f;
        if (dsp_apply_analysis_hint(engine, &hint, safe_bass) != DSP_OK) {
            dsp_destroy(engine);
            std::fprintf(stderr, "Applying analysis hint failed.\n");
            return 4;
        }
    }

    std::vector<float> output(input.size());
    const std::string csv_path = std::string(argv[2]) + ".csv";
    std::ofstream csv(csv_path);
    if (!csv) {
        dsp_destroy(engine);
        std::fprintf(stderr, "Could not open CSV output: %s\n", csv_path.c_str());
        return 5;
    }
    csv << "time_s,input_rms_db,output_rms_db,input_peak_db,output_peak_db,limiter_gr_db,limiter_gr_peak_db,auto_bass_applied_db,crest_hint_db\n";
    std::vector<float> block_out(static_cast<std::size_t>(kBlock) * wav.channels);
    double in_sq = 0.0, out_sq = 0.0;
    float in_peak = 0.f, out_peak = 0.f, max_gr = 0.f;
    std::size_t total_samples = 0;

    for (std::size_t offset = 0; offset < frames; offset += kBlock) {
        const int n = static_cast<int>(std::min<std::size_t>(kBlock, frames - offset));
        const float* in = input.data() + offset * wav.channels;
        float* out = block_out.data();
        if (dsp_process(engine, in, out, n) != DSP_OK) {
            dsp_destroy(engine);
            std::fprintf(stderr, "dsp_process failed at %.3f s\n", static_cast<double>(offset) / wav.sample_rate);
            return 6;
        }
        const std::size_t count = static_cast<std::size_t>(n) * wav.channels;
        std::copy(out, out + count, output.data() + offset * wav.channels);
        const float ir = rms_db(in, count), orms = rms_db(out, count);
        const float ip = peak_db(in, count), op = peak_db(out, count);
        DspLimiterMeter meter{};
        dsp_limiter_get_meter(engine, &meter);
        max_gr = std::max(max_gr, meter.gr_peak_db);
        in_peak = std::max(in_peak, ip);
        out_peak = std::max(out_peak, op);
        for (std::size_t i = 0; i < count; ++i) {
            in_sq += static_cast<double>(in[i]) * in[i];
            out_sq += static_cast<double>(out[i]) * out[i];
        }
        total_samples += count;
        csv << static_cast<double>(offset) / wav.sample_rate << ',' << ir << ',' << orms
            << ',' << ip << ',' << op << ',' << meter.gr_db << ',' << meter.gr_peak_db
            << ',' << dsp_auto_bass_get_applied_db(engine) << ','
            << dsp_limiter_get_crest_hint(engine) << '\n';
    }

    if (!write_float_wav(argv[2], wav.sample_rate, wav.channels, output)) {
        dsp_destroy(engine);
        std::fprintf(stderr, "Could not write output WAV (RIFF limit may have been exceeded).\n");
        return 7;
    }
    const auto& a = snapshot.analysis;
    std::printf("Psychic offline player complete\ninput=%s\noutput=%s\ncsv=%s\nhint_mode=%s\n",
        argv[1], argv[2], csv_path.c_str(), mode.c_str());
    std::printf("track: duration=%.2fs active=%.2fs integrated_rms=%.2fdBFS peak=%.2fdBFS bass=%.2fdBFS mean_crest=%.2fdB\n",
        a.duration_seconds, a.active_seconds, a.integrated_rms_db, a.sample_peak_db,
        a.bass_rms_db, a.mean_crest_db);
    std::printf("intelligence: sufficient=%d findings=%zu recommendations=%zu\n",
        snapshot.intelligence.sufficient_data ? 1 : 0,
        snapshot.intelligence.finding_count, snapshot.intelligence.recommendation_count);
    for (std::size_t i = 0; i < snapshot.intelligence.finding_count; ++i) {
        const auto& f = snapshot.intelligence.findings[i];
        std::printf("  %s confidence=%.2f\n",
            dsp::AdaptiveTrackIntelligence::trait_name(f.trait), f.confidence);
    }
    std::printf("playback: input_rms=%.2fdBFS output_rms=%.2fdBFS input_peak=%.2fdBFS output_peak=%.2fdBFS max_limiter_gr=%.2fdB bass_applied=%.2fdB crest_hint=%.2fdB\n",
        rms_db(input.data(), total_samples), rms_db(output.data(), total_samples),
        in_peak, out_peak, max_gr, dsp_auto_bass_get_applied_db(engine),
        dsp_limiter_get_crest_hint(engine));
    dsp_destroy(engine);
    return 0;
}
