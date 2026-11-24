#include "core/adaptive_controller.h"

#include <cmath>

int main() {
    dsp::AdaptiveController controller;

    // The positive-gain budget is shared, weighted, and margin-aware.
    const auto budget = controller.allocate_headroom(
        -6.0f, -18.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    if (std::fabs(budget.available_db - 4.4f) >= 1.0e-3f) return 1;
    if (std::fabs((budget.bass_db + budget.preamp_db +
                   budget.gain_boost_db) - budget.available_db) >= 1.0e-3f) return 2;

    // One isolated high-crest peak is discounted instead of withdrawing
    // enhancement headroom; the limiter remains responsible for peak safety.
    const auto transient = controller.allocate_headroom(
        0.0f, -30.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    if (transient.available_db <= 0.0f) return 3;
    if (transient.available_db < budget.available_db) return 4;

    // Repeated high-crest blocks build pressure and eventually constrain the
    // budget; one isolated peak must not trigger the same response.
    dsp::AdaptiveController repeated;
    repeated.allocate_headroom(-6.0f, -18.0f, -0.1f, 1.0f,
                               0.5f, 0.2f, 0.3f, 48000.0f, 480);
    dsp::AdaptiveHeadroomBudget repeated_budget{};
    for (int i = 0; i < 100; ++i) {
        repeated_budget = repeated.allocate_headroom(
            0.0f, -30.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f,
            48000.0f, 480);
    }
    if (repeated_budget.available_db >= transient.available_db) return 18;
    if (repeated_budget.budget_peak_db <= transient.budget_peak_db) return 21;

    // Rolling track state distinguishes a rising section from a stable level.
    dsp::AdaptiveController track_a;
    dsp::AdaptiveController track_b;
    const auto initial_track = track_a.update_track_state(0.01, 0.001, 48000.f, 480);
    if (std::fabs(initial_track.short_rms_db - (-20.0f)) > 1.0e-3f) return 22;
    if (std::fabs(initial_track.long_rms_db - initial_track.short_rms_db) > 1.0e-4f) return 23;
    const auto rising_track = track_a.update_track_state(0.1, 0.01, 48000.f, 480);
    if (rising_track.loudness_trend_db <= 0.f) return 24;
    if (rising_track.bass_prominence_db >= 0.f) return 25;
    dsp::AdaptiveController track_large_block;
    dsp::AdaptiveController track_small_blocks;
    track_large_block.update_track_state(0.01, 0.001, 48000.f, 480);
    track_small_blocks.update_track_state(0.01, 0.001, 48000.f, 480);
    const auto block_track =
        track_large_block.update_track_state(0.1, 0.01, 48000.f, 4800);
    auto multi_block_track = track_small_blocks.update_track_state(
        0.1, 0.01, 48000.f, 480);
    for (int i = 1; i < 10; ++i)
        multi_block_track = track_small_blocks.update_track_state(
            0.1, 0.01, 48000.f, 480);
    if (std::fabs(block_track.short_rms_db - multi_block_track.short_rms_db) > 1.0e-3f ||
        std::fabs(block_track.long_rms_db - multi_block_track.long_rms_db) > 1.0e-3f ||
        std::fabs(block_track.bass_rms_db - multi_block_track.bass_rms_db) > 1.0e-3f)
        return 26;

    // Smoothing is based on elapsed audio time, not a fixed per-block step.
    dsp::AdaptiveController smooth_a;
    dsp::AdaptiveController smooth_b;
    float a = 0.0f;
    float b = 0.0f;
    for (int i = 0; i < 10; ++i)
        a = smooth_a.smooth_gain(a, -6.0f, 48000.0f, 480);
    b = smooth_b.smooth_gain(b, -6.0f, 48000.0f, 4800);
    if (std::fabs(a - b) >= 1.0e-4f) return 5;
    if (!(a < 0.0f && a > -6.0f)) return 6;

    // Recovery is smooth and heads toward unity without a discontinuity.
    const float recovered = smooth_a.smooth_gain(a, 0.0f, 48000.0f, 480);
    if (!(recovered > a && recovered < 0.0f)) return 7;

    // Bass/preamp parameter smoothing also respects elapsed audio time.
    dsp::AdaptiveController parameter_smoother;
    float parameter = 0.0f;
    for (int i = 0; i < 10; ++i)
        parameter = parameter_smoother.smooth_parameter(
            parameter, 6.0f, 48000.0f, 480, 0.35f, 0.18f);
    if (!(parameter > 0.0f && parameter < 6.0f)) return 8;
    const float reduced = parameter_smoother.smooth_parameter(
        parameter, 0.0f, 48000.0f, 480, 0.35f, 0.18f);
    if (!(reduced < parameter && reduced > 0.0f)) return 9;
    // Telemetry publishes a coherent, allocation-free latest-block snapshot.
    dsp::AdaptiveDecisionTelemetry telemetry;
    dsp::AdaptiveDecisionSnapshot observed{};
    if (telemetry.read(observed)) return 10;
    dsp::AdaptiveDecisionSnapshot published{};
    published.block_index = 42;
    published.bass_reasons = dsp::kDecisionActive | dsp::kDecisionBudgetLimited;
    published.source_peak_db = -3.0f;
    published.source_rms_db = -15.0f;
    published.track_short_rms_db = -12.0f;
    published.track_long_rms_db = -18.0f;
    published.track_bass_rms_db = -25.0f;
    published.track_loudness_trend_db = 6.0f;
    published.track_bass_prominence_db = -13.0f;
    published.budget_peak_db = -4.0f;
    published.bass_target_db = 4.0f;
    published.bass_applied_db = 2.5f;
    published.limiter_reduction_db = 1.25f;
    telemetry.publish(published);
    if (!telemetry.read(observed)) return 17;
    if (observed.block_index != 42) return 11;
    if (observed.bass_reasons != published.bass_reasons) return 12;
    if (std::fabs(observed.source_peak_db - published.source_peak_db) > 1.0e-6f) return 13;
    if (std::fabs(observed.source_rms_db - published.source_rms_db) > 1.0e-6f) return 19;
    if (std::fabs(observed.track_short_rms_db - published.track_short_rms_db) > 1.0e-6f) return 27;
    if (std::fabs(observed.track_long_rms_db - published.track_long_rms_db) > 1.0e-6f) return 28;
    if (std::fabs(observed.track_bass_rms_db - published.track_bass_rms_db) > 1.0e-6f) return 29;
    if (std::fabs(observed.track_loudness_trend_db - published.track_loudness_trend_db) > 1.0e-6f) return 30;
    if (std::fabs(observed.track_bass_prominence_db - published.track_bass_prominence_db) > 1.0e-6f) return 31;
    if (std::fabs(observed.budget_peak_db - published.budget_peak_db) > 1.0e-6f) return 20;
    if (std::fabs(observed.bass_target_db - 4.0f) > 1.0e-6f) return 14;
    if (std::fabs(observed.bass_applied_db - 2.5f) > 1.0e-6f) return 15;
    if (std::fabs(observed.limiter_reduction_db - 1.25f) > 1.0e-6f) return 16;
    return 0;
}
