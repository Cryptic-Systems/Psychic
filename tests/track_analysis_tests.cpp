#include "core/track_analyzer.h"
#include "core/adaptive_controller.h"

#include <cmath>
#include <cstdio>

int main() {
    dsp::AdaptiveTrackAnalyzer analyzer;
    for (int i = 0; i < 1000; ++i) {
        analyzer.add_block(-30.f, -12.f, -35.f, 0.01);
        analyzer.add_block(-12.f, -1.f, -17.f, 0.01,
            i < 100 ? dsp::kSectionRising : 0u,
            i < 100 ? 0.8f : 0.f);
    }
    // A single isolated high peak must affect peak/crest diagnostics without
    // materially moving the time-weighted loudness distribution.
    analyzer.add_block(-24.f, -0.5f, -30.f, 0.01);
    for (int i = 0; i < 100; ++i)
        analyzer.add_block(-120.f, -120.f, -120.f, 0.01);

    const auto result = analyzer.summary();
    std::fprintf(stderr, "profile dur=%.5f active=%.5f silence=%.5f rms=%.5f p10=%.2f p50=%.2f p95=%.2f range=%.2f peak=%.2f crest=%.2f rising=%.5f transition=%.5f bassprom=%.2f\\n", result.duration_seconds, result.active_seconds, result.silence_seconds, result.integrated_rms_db, result.loudness_p10_db, result.loudness_p50_db, result.loudness_p95_db, result.loudness_range_db, result.sample_peak_db, result.max_crest_db, result.rising_seconds, result.transition_seconds, result.bass_prominence_db);
    if (std::fabs(result.duration_seconds - 21.01) > 1.0e-6) return 1;
    if (std::fabs(result.active_seconds - 20.01) > 1.0e-6) return 2;
    if (std::fabs(result.silence_seconds - 1.0) > 1.0e-6) return 3;
    if (result.integrated_rms_db < -16.f || result.integrated_rms_db > -14.f) return 4;
    if (result.loudness_p10_db > -29.f || result.loudness_p10_db < -31.f) return 5;
    if (result.loudness_p50_db > -11.f || result.loudness_p50_db < -13.f) return 6;
    if (result.loudness_p95_db > -11.f || result.loudness_p95_db < -13.f) return 7;
    if (result.loudness_range_db < 16.f || result.loudness_range_db > 20.f) return 8;
    if (result.sample_peak_db > -0.4f || result.sample_peak_db < -0.6f) return 9;
    if (result.max_crest_db < 23.f) return 10;
    if (result.rising_seconds < 0.99 || result.transition_seconds < 0.99) return 11;
    if (result.bass_prominence_db > -3.f || result.bass_prominence_db < -8.f) return 12;

    analyzer.reset();
    const auto empty = analyzer.summary();
    if (empty.active_seconds != 0.0 || empty.integrated_rms_db != -120.f) return 13;

    // Invalid/zero-duration observations are ignored; non-finite levels are
    // sanitized instead of contaminating a whole-track result.
    analyzer.add_block(0.f, 0.f, 0.f, 0.0);
    analyzer.add_block(NAN, INFINITY, NAN, 0.1);
    const auto sanitized = analyzer.summary();
    std::fprintf(stderr, "sanitized dur=%.5f active=%.5f silence=%.5f rms=%.2f peak=%.2f\\n", sanitized.duration_seconds, sanitized.active_seconds, sanitized.silence_seconds, sanitized.integrated_rms_db, sanitized.sample_peak_db);
    if (sanitized.duration_seconds < 0.099 ||
        sanitized.duration_seconds > 0.101) return 14;
    if (!std::isfinite(sanitized.integrated_rms_db) ||
        !std::isfinite(sanitized.sample_peak_db)) return 15;
    return 0;
}
