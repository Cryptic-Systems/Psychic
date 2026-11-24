#include "core/adaptive_controller.h"

#include <cmath>
#include <cstdio>

int main() {
    dsp::AdaptiveController controller;

    // The positive-gain budget is shared, weighted, and margin-aware.
    const auto budget = controller.allocate_headroom(
        -6.0f, -18.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    if (std::fabs(budget.available_db - 4.4f) >= 1.0e-3f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 1); return 1; } while (false);
    if (std::fabs((budget.bass_db + budget.preamp_db +
                   budget.gain_boost_db) - budget.available_db) >= 1.0e-3f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 2); return 2; } while (false);

    // One isolated high-crest peak is discounted instead of withdrawing
    // enhancement headroom; the limiter remains responsible for peak safety.
    const auto transient = controller.allocate_headroom(
        0.0f, -30.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    if (transient.available_db <= 0.0f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 3); return 3; } while (false);
    if (transient.available_db < budget.available_db) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 4); return 4; } while (false);

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
    if (repeated_budget.available_db >= transient.available_db) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 18); return 18; } while (false);
    if (repeated_budget.budget_peak_db <= transient.budget_peak_db) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 21); return 21; } while (false);

    // Rolling track state distinguishes a rising section from a stable level.
    dsp::AdaptiveController track_a;
    const auto initial_track = track_a.update_track_state(0.01, 0.001, 48000.f, 480);
    if (std::fabs(initial_track.short_rms_db - (-20.0f)) > 1.0e-3f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 22); return 22; } while (false);
    if (std::fabs(initial_track.long_rms_db - initial_track.short_rms_db) > 1.0e-4f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 23); return 23; } while (false);
    const auto rising_track = track_a.update_track_state(0.1, 0.01, 48000.f, 480);
    if (rising_track.loudness_trend_db <= 0.f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 24); return 24; } while (false);
    if (rising_track.bass_prominence_db >= 0.f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 25); return 25; } while (false);
    // Sustained rising and falling sections gain confidence; a one-block
    // transient does not become a section transition.
    dsp::AdaptiveController rising_section;
    rising_section.update_track_state(0.01, 0.001, 48000.f, 480);
    dsp::AdaptiveTrackState rising_state{};
    for (int i = 0; i < 120; ++i)
        rising_state = rising_section.update_track_state(0.1, 0.01, 48000.f, 480);
    if (rising_state.section_transition != dsp::kSectionRising) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 32); return 32; } while (false);
    if (rising_state.section_transition_confidence < 0.18f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 33); return 33; } while (false);
    if (rising_state.anticipatory_gain_hint_db >= 0.f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 45); return 45; } while (false);
    if (rising_state.anticipatory_gain_hint_db < -1.0f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 46); return 46; } while (false);
    dsp::AdaptiveController falling_section;
    falling_section.update_track_state(0.1, 0.01, 48000.f, 480);
    dsp::AdaptiveTrackState falling_state{};
    for (int i = 0; i < 120; ++i)
        falling_state = falling_section.update_track_state(0.01, 0.001, 48000.f, 480);
    if (falling_state.section_transition != dsp::kSectionFalling) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 34); return 34; } while (false);
    if (falling_state.anticipatory_gain_hint_db <= 0.f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 47); return 47; } while (false);
    if (falling_state.anticipatory_gain_hint_db > 1.0f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 48); return 48; } while (false);
    // Reversing a confirmed rise must not inherit its confidence and
    // immediately report a fall. The opposite direction must earn confidence.
    dsp::AdaptiveController reversing_section;
    reversing_section.update_track_state(0.01, 0.001, 48000.f, 480);
    dsp::AdaptiveTrackState reversal_state{};
    for (int i = 0; i < 120; ++i)
        reversal_state = reversing_section.update_track_state(
            0.1, 0.01, 48000.f, 480);
    if (reversal_state.section_transition != dsp::kSectionRising) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 42); return 42; } while (false);
    reversal_state = reversing_section.update_track_state(
        0.001, 0.0001, 48000.f, 480);
    if (reversal_state.section_transition == dsp::kSectionFalling) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 43); return 43; } while (false);
    for (int i = 0; i < 120; ++i)
        reversal_state = reversing_section.update_track_state(
            0.001, 0.0001, 48000.f, 480);
    if (reversal_state.section_transition != dsp::kSectionFalling) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 44); return 44; } while (false);
    dsp::AdaptiveController isolated_transient;
    isolated_transient.update_track_state(0.01, 0.001, 48000.f, 480);
    isolated_transient.update_track_state(1.0, 0.1, 48000.f, 480);
    dsp::AdaptiveTrackState after_transient{};
    for (int i = 0; i < 30; ++i)
        after_transient = isolated_transient.update_track_state(
            0.01, 0.001, 48000.f, 480);
    if (after_transient.section_transition != dsp::kSectionSteady) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 35); return 35; } while (false);
    if (after_transient.section_transition_confidence > 0.18f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 36); return 36; } while (false);
    // Transition confidence is elapsed-time based and remains stable across
    // callback block sizes.
    dsp::AdaptiveController transition_small;
    dsp::AdaptiveController transition_large;
    transition_small.update_track_state(0.01, 0.001, 48000.f, 480);
    transition_large.update_track_state(0.01, 0.001, 48000.f, 480);
    dsp::AdaptiveTrackState small_transition{}, large_transition{};
    for (int i = 0; i < 120; ++i)
        small_transition = transition_small.update_track_state(
            0.1, 0.01, 48000.f, 480);
    for (int i = 0; i < 12; ++i)
        large_transition = transition_large.update_track_state(
            0.1, 0.01, 48000.f, 4800);
    if (small_transition.section_transition != large_transition.section_transition)
        do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 39); return 39; } while (false);
    if (std::fabs(small_transition.section_transition_confidence -
                  large_transition.section_transition_confidence) > 1.0e-3f)
        do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 40); return 40; } while (false);

    // Small, rapidly varying level fluctuations should not look like a section
    // change after the rolling envelopes settle.
    dsp::AdaptiveController noisy_section;
    dsp::AdaptiveTrackState noisy_state{};
    for (int i = 0; i < 800; ++i) {
        const double modulation = 1.0 + 0.15 * std::sin(i * 0.37);
        noisy_state = noisy_section.update_track_state(
            0.01 * modulation, 0.001 * modulation, 48000.f, 480);
    }
    if (noisy_state.section_transition != dsp::kSectionSteady) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 41); return 41; } while (false);
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
        do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 26); return 26; } while (false);

    // Smoothing is based on elapsed audio time, not a fixed per-block step.
    dsp::AdaptiveController smooth_a;
    dsp::AdaptiveController smooth_b;
    float a = 0.0f;
    float b = 0.0f;
    for (int i = 0; i < 10; ++i)
        a = smooth_a.smooth_gain(a, -6.0f, 48000.0f, 480);
    b = smooth_b.smooth_gain(b, -6.0f, 48000.0f, 4800);
    if (std::fabs(a - b) >= 1.0e-4f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 5); return 5; } while (false);
    if (!(a < 0.0f && a > -6.0f)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 6); return 6; } while (false);

    // Recovery is smooth and heads toward unity without a discontinuity.
    const float recovered = smooth_a.smooth_gain(a, 0.0f, 48000.0f, 480);
    if (!(recovered > a && recovered < 0.0f)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 7); return 7; } while (false);

    // Bass/preamp parameter smoothing also respects elapsed audio time.
    dsp::AdaptiveController parameter_smoother;
    float parameter = 0.0f;
    for (int i = 0; i < 10; ++i)
        parameter = parameter_smoother.smooth_parameter(
            parameter, 6.0f, 48000.0f, 480, 0.35f, 0.18f);
    if (!(parameter > 0.0f && parameter < 6.0f)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 8); return 8; } while (false);
    const float reduced = parameter_smoother.smooth_parameter(
        parameter, 0.0f, 48000.0f, 480, 0.35f, 0.18f);
    if (!(reduced < parameter && reduced > 0.0f)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 9); return 9; } while (false);
    // Telemetry publishes a coherent, allocation-free latest-block snapshot.
    dsp::AdaptiveDecisionTelemetry telemetry;
    dsp::AdaptiveDecisionSnapshot observed{};
    if (telemetry.read(observed)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 10); return 10; } while (false);
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
    published.section_transition = dsp::kSectionRising;
    published.section_transition_confidence = 0.72f;
    published.anticipatory_gain_hint_db = -0.45f;
    published.budget_peak_db = -4.0f;
    published.bass_target_db = 4.0f;
    published.bass_applied_db = 2.5f;
    published.limiter_reduction_db = 1.25f;
    telemetry.publish(published);
    if (!telemetry.read(observed)) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 17); return 17; } while (false);
    if (observed.block_index != 42) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 11); return 11; } while (false);
    if (observed.bass_reasons != published.bass_reasons) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 12); return 12; } while (false);
    if (std::fabs(observed.source_peak_db - published.source_peak_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 13); return 13; } while (false);
    if (std::fabs(observed.source_rms_db - published.source_rms_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 19); return 19; } while (false);
    if (std::fabs(observed.track_short_rms_db - published.track_short_rms_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 27); return 27; } while (false);
    if (std::fabs(observed.track_long_rms_db - published.track_long_rms_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 28); return 28; } while (false);
    if (std::fabs(observed.track_bass_rms_db - published.track_bass_rms_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 29); return 29; } while (false);
    if (std::fabs(observed.track_loudness_trend_db - published.track_loudness_trend_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 30); return 30; } while (false);
    if (std::fabs(observed.track_bass_prominence_db - published.track_bass_prominence_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 31); return 31; } while (false);
    if (observed.section_transition != published.section_transition) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 37); return 37; } while (false);
    if (std::fabs(observed.section_transition_confidence - published.section_transition_confidence) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 38); return 38; } while (false);
    if (std::fabs(observed.anticipatory_gain_hint_db - published.anticipatory_gain_hint_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 49); return 49; } while (false);
    if (std::fabs(observed.budget_peak_db - published.budget_peak_db) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 20); return 20; } while (false);
    if (std::fabs(observed.bass_target_db - 4.0f) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 14); return 14; } while (false);
    if (std::fabs(observed.bass_applied_db - 2.5f) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 15); return 15; } while (false);
    if (std::fabs(observed.limiter_reduction_db - 1.25f) > 1.0e-6f) do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 16); return 16; } while (false);
    do { std::fprintf(stderr, "adaptive_controller_tests failure: %d\\n", 0); return 0; } while (false);
}
