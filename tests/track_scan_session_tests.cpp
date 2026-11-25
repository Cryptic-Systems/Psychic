#include "core/track_scan_session.h"

#include <cmath>
#include <limits>
#include <vector>

int main() {
    dsp::AdaptiveTrackScanSession scan;
    if (scan.snapshot().state != dsp::TrackScanState::Idle) return 1;
    if (!scan.begin(10.0)) return 2;
    if (scan.begin(10.0)) return 3;
    if (!scan.add_block(-12.f, -1.f, -18.f, 4.0)) return 4;

    auto progress = scan.snapshot();
    if (progress.state != dsp::TrackScanState::Scanning ||
        progress.report_ready || std::fabs(progress.progress - 0.4f) > 0.001f)
        return 5;
    if (scan.add_block(-12.f, -1.f, -18.f, 0.0)) return 6;

    if (!scan.add_block(-12.f, -1.f, -18.f, 6.0)) return 7;
    if (!scan.finalize()) return 8;
    if (scan.finalize()) return 9;

    const auto complete = scan.snapshot();
    if (complete.state != dsp::TrackScanState::Completed ||
        !complete.report_ready || complete.progress != 1.f)
        return 10;
    if (std::fabs(complete.analysis.duration_seconds - 10.0) > 1.0e-6)
        return 11;
    if (complete.intelligence.sufficient_data != true) return 12;
    if (scan.add_block(-12.f, -1.f, -18.f, 1.0)) return 13;
    if (scan.cancel()) return 14;

    // Cancellation retains partial measurements but never publishes a report.
    if (!scan.begin(20.0)) return 15;
    if (!scan.add_block(-30.f, -12.f, -35.f, 2.0)) return 16;
    if (!scan.cancel()) return 17;
    const auto cancelled = scan.snapshot();
    if (cancelled.state != dsp::TrackScanState::Cancelled ||
        cancelled.report_ready || cancelled.progress > 0.101f ||
        std::fabs(cancelled.analysis.duration_seconds - 2.0) > 1.0e-6)
        return 18;
    if (scan.add_block(-30.f, -12.f, -35.f, 1.0)) return 19;
    if (scan.finalize()) return 20;

    // Unknown duration is supported; progress is indeterminate until finalize.
    if (!scan.begin()) return 21;
    if (!scan.add_block(-30.f, -12.f, -35.f, 6.0)) return 22;
    if (scan.snapshot().progress != 0.f) return 23;
    if (!scan.finalize()) return 24;
    if (scan.snapshot().progress != 1.f) return 25;

    if (scan.begin(-1.0)) return 26;
    if (scan.begin(INFINITY)) return 27;

    // PCM ingestion computes stable full-band, peak, and low-frequency metrics.
    constexpr std::size_t frames = 48000;
    std::vector<float> stereo(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const float sample = 0.25f * std::sin(
            2.0 * 3.14159265358979323846 * 60.0 *
            static_cast<double>(i) / 48000.0);
        stereo[i * 2] = sample;
        stereo[i * 2 + 1] = sample;
    }

    if (!scan.begin(1.0)) return 28;
    if (!scan.add_audio_block(stereo.data(), frames, 2, 48000.0)) return 29;
    auto pcm_result = scan.snapshot();
    if (std::fabs(pcm_result.analysis.duration_seconds - 1.0) > 1.0e-6)
        return 30;
    if (pcm_result.analysis.integrated_rms_db > -14.5f ||
        pcm_result.analysis.integrated_rms_db < -15.7f) return 31;
    if (pcm_result.analysis.sample_peak_db > -11.5f ||
        pcm_result.analysis.sample_peak_db < -12.6f) return 32;
    if (pcm_result.analysis.bass_prominence_db > 1.5f ||
        pcm_result.analysis.bass_prominence_db < -1.5f) return 33;
    if (!scan.finalize()) return 34;

    if (!scan.begin()) return 35;
    if (scan.add_audio_block(nullptr, frames, 2, 48000.0)) return 36;
    if (scan.add_audio_block(stereo.data(), 0, 2, 48000.0)) return 37;
    if (scan.add_audio_block(stereo.data(), frames, 9, 48000.0)) return 38;
    if (scan.add_audio_block(stereo.data(), frames, 2, 1000.0)) return 39;
    if (scan.add_audio_block(stereo.data(), frames, 2,
                             std::numeric_limits<double>::infinity())) return 40;
    if (!scan.cancel()) return 41;

    // Non-finite samples are treated as silence rather than poisoning the scan.
    stereo[0] = std::numeric_limits<float>::quiet_NaN();
    if (!scan.begin()) return 42;
    if (!scan.add_audio_block(stereo.data(), frames, 2, 48000.0)) return 43;
    const auto sanitized = scan.snapshot();
    if (!std::isfinite(sanitized.analysis.integrated_rms_db) ||
        !std::isfinite(sanitized.analysis.sample_peak_db)) return 44;
    return 0;
}
