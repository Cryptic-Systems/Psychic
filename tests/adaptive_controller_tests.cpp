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
    const auto initial_track = track_a.update_track_state(0.01, 0.001, 48000.f, 480);
    if (std::fabs(initial_track.short_rms_db - (-20.0f)) > 1.0e-3f) return 22;
    if (std::fabs(initial_track.long_rms_db - initial_track.short_rms_db) > 1.0e-4f) return 23;
    const auto rising_track = track_a.update_track_state(0.1, 0.01, 48000.f, 480);
    if (rising_track.loudness_trend_db <= 0.f) return 24;
    if (rising_track.bass_prominence_db >= 0.f) return 25;
    // Sustained rising and falling sections gain confidence; a one-block
    // transient does not become a section transition.
    dsp::AdaptiveController rising_section;
    rising_section.update_track_state(0.01, 0.001, 48000.f, 480);
    dsp::AdaptiveTrackState rising_state{};
    for (int i = 0; i < 120; ++i)
        rising_state = rising_section.update_track_state(0.1, 0.01, 48000.f, 480);
    if (rising_state.section_transition != dsp::kSectionRising) return 32;
    if (rising_state.section_transition_confidence < 0.18f) return 33;
    if (rising_state.anticipatory_gain_hint_db >= 0.f) return 45;
    if (rising_state.anticipatory_gain_hint_db < -1.0f) return 46;
    dsp::AdaptiveController falling_section;
    falling_section.update_track_state(0.1, 0.01, 48000.f, 480);
    dsp::AdaptiveTrackState falling_state{};
    for (int i = 0; i < 120; ++i)
        falling_state = falling_section.update_track_state(0.01, 0.001, 48000.f, 480);
    if (falling_state.section_transition != dsp::kSectionFalling) return 34;
    if (falling_state.anticipatory_gain_hint_db <= 0.f) return 47;
    if (falling_state.anticipatory_gain_hint_db > 1.0f) return 48;
    // Reversing a confirmed rise must not inherit its confidence and
    // immediately report a fall. The opposite direction must earn confidence.
    dsp::AdaptiveController reversing_section;
    reversing_section.update_track_state(0.01, 0.001, 48000.f, 480);
    dsp::AdaptiveTrackState reversal_state{};
    for (int i = 0; i < 120; ++i)
        reversal_state = reversing_section.update_track_state(
            0.1, 0.01, 48000.f, 480);
    if (reversal_state.section_transition != dsp::kSectionRising) return 42;
    reversal_state = reversing_section.update_track_state(
        0.001, 0.0001, 48000.f, 480);
    if (reversal_state.section_transition == dsp::kSectionFalling) return 43;
    for (int i = 0; i < 120; ++i)
        reversal_state = reversing_section.update_track_state(
            0.001, 0.0001, 48000.f, 480);
    if (reversal_state.section_transition != dsp::kSectionFalling) return 44;
    dsp::AdaptiveController isolated_transient;
    isolated_transient.update_track_state(0.01, 0.001, 48000.f, 480);
    isolated_transient.update_track_state(1.0, 0.1, 48000.f, 480);
    dsp::AdaptiveTrackState after_transient{};
    for (int i = 0; i < 30; ++i)
        after_transient = isolated_transient.update_track_state(
            0.01, 0.001, 48000.f, 480);
    if (after_transient.section_transition != dsp::kSectionSteady) return 35;
    if (after_transient.section_transition_confidence > 0.18f) return 36;
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
        return 39;
    if (std::fabs(small_transition.section_transition_confidence -
                  large_transition.section_transition_confidence) > 2.0e-3f)
        return 40;

    // Abrupt level steps should qualify after persistence, then return to
    // steady once the rolling fast/slow envelopes converge.
    dsp::AdaptiveController stepped_section;
    dsp::AdaptiveTrackState stepped_state{};
    for (int i = 0; i < 500; ++i)
        stepped_state = stepped_section.update_track_state(0.001, 0.0001, 48000.f, 480);
    for (int i = 0; i < 100; ++i)
        stepped_state = stepped_section.update_track_state(0.1, 0.01, 48000.f, 480);
    if (stepped_state.section_transition != dsp::kSectionRising) return 50;
    if (stepped_state.anticipatory_gain_hint_db >= 0.f) return 51;
    for (int i = 0; i < 800; ++i)
        stepped_state = stepped_section.update_track_state(0.1, 0.01, 48000.f, 480);
    if (stepped_state.section_transition != dsp::kSectionSteady) return 52;
    for (int i = 0; i < 100; ++i)
        stepped_state = stepped_section.update_track_state(0.001, 0.0001, 48000.f, 480);
    if (stepped_state.section_transition != dsp::kSectionFalling) return 53;
    if (stepped_state.anticipatory_gain_hint_db <= 0.f) return 54;

    // A gradual fade should still be recognized, without requiring a hard step.
    dsp::AdaptiveController fade_section;
    dsp::AdaptiveTrackState fade_state{};
    for (int i = 0; i < 500; ++i)
        fade_state = fade_section.update_track_state(0.001, 0.0001, 48000.f, 480);
    bool fade_qualified = false;
    for (int i = 0; i < 800; ++i) {
        const double level_db = -30.0 + 24.0 * static_cast<double>(i) / 799.0;
        const double power = std::pow(10.0, level_db / 10.0);
        fade_state = fade_section.update_track_state(power, power * 0.1, 48000.f, 480);
        if (fade_state.section_transition == dsp::kSectionRising) {
            fade_qualified = true;
            if (fade_state.anticipatory_gain_hint_db >= 0.f) return 55;
        }
    }
    if (!fade_qualified) return 56;

    // A bass-only balance change must move bass prominence without being
    // misclassified as a full-band loudness section transition.
    dsp::AdaptiveController bass_change;
    dsp::AdaptiveTrackState bass_state{};
    for (int i = 0; i < 600; ++i)
        bass_state = bass_change.update_track_state(0.01, 0.0001, 48000.f, 480);
    const float bass_before = bass_state.bass_rms_db;
    for (int i = 0; i < 400; ++i)
        bass_state = bass_change.update_track_state(0.01, 0.008, 48000.f, 480);
    if (bass_state.bass_rms_db < bass_before + 12.0f) return 57;
    if (bass_state.section_transition != dsp::kSectionSteady) return 58;
    if (std::fabs(bass_state.loudness_trend_db) > 0.1f) return 59;

    // Small, rapidly varying level fluctuations should not look like a section
    // change after the rolling envelopes settle.
    dsp::AdaptiveController noisy_section;
    dsp::AdaptiveTrackState noisy_state{};
    for (int i = 0; i < 800; ++i) {
        const double modulation = 1.0 + 0.15 * std::sin(i * 0.37);
        noisy_state = noisy_section.update_track_state(
            0.01 * modulation, 0.001 * modulation, 48000.f, 480);
    }
    if (noisy_state.section_transition != dsp::kSectionSteady) return 41;
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

    // Gain cuts require sustained control-level excess. Quiet material gets
    // no cut budget; a newly loud section is protected while its transition
    // is developing; a stable loud passage earns the configured cut budget.
    const double quiet_power = std::pow(10.0, -30.0 / 10.0);
    const double loud_power = std::pow(10.0, -6.0 / 10.0);
    dsp::AdaptiveController gain_policy;
    float quiet_cut_confidence = 0.f;
    for (int i = 0; i < 1000; ++i) {
        const auto state = gain_policy.update_track_state(
            quiet_power, quiet_power * 0.1, 48000.f, 480);
        quiet_cut_confidence = gain_policy.update_gain_cut_confidence(
            -30.f, -18.f, state.section_transition_confidence, 48000.f, 480);
    }
    if (quiet_cut_confidence > 0.01f) return 60;

    float transition_cut_confidence = 0.f;
    for (int i = 0; i < 100; ++i) {
        const auto state = gain_policy.update_track_state(
            loud_power, loud_power * 0.1, 48000.f, 480);
        transition_cut_confidence = gain_policy.update_gain_cut_confidence(
            -6.f, -18.f, state.section_transition_confidence, 48000.f, 480);
    }
    dsp::AdaptiveController stable_loud_policy;
    float stable_cut_confidence = 0.f;
    for (int i = 0; i < 1200; ++i) {
        const auto state = stable_loud_policy.update_track_state(
            loud_power, loud_power * 0.1, 48000.f, 480);
        stable_cut_confidence = stable_loud_policy.update_gain_cut_confidence(
            -6.f, -18.f, state.section_transition_confidence, 48000.f, 480);
    }
    if (!(transition_cut_confidence < stable_cut_confidence)) return 61;
    if (stable_cut_confidence < 0.90f) return 62;

    // The persistence reference is elapsed-time based, not callback-count based.
    dsp::AdaptiveController cut_small, cut_large;
    cut_small.update_gain_cut_confidence(-6.f, -18.f, 0.f, 48000.f, 480);
    cut_large.update_gain_cut_confidence(-6.f, -18.f, 0.f, 48000.f, 480);
    float small_cut = 0.f, large_cut = 0.f;
    for (int i = 0; i < 100; ++i)
        small_cut = cut_small.update_gain_cut_confidence(
            -6.f, -18.f, 0.f, 48000.f, 480);
    large_cut = cut_large.update_gain_cut_confidence(
        -6.f, -18.f, 0.f, 48000.f, 48000);
    if (std::fabs(small_cut - large_cut) > 2.0e-3f) return 63;

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
    published.section_transition = dsp::kSectionRising;
    published.section_transition_confidence = 0.72f;
    published.anticipatory_gain_hint_db = -0.45f;
    published.gain_cut_confidence = 0.65f;
    published.gain_cut_limit_db = 1.95f;
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
    if (observed.section_transition != published.section_transition) return 37;
    if (std::fabs(observed.section_transition_confidence - published.section_transition_confidence) > 1.0e-6f) return 38;
    if (std::fabs(observed.anticipatory_gain_hint_db - published.anticipatory_gain_hint_db) > 1.0e-6f) return 49;
    if (std::fabs(observed.gain_cut_confidence - published.gain_cut_confidence) > 1.0e-6f) return 64;
    if (std::fabs(observed.gain_cut_limit_db - published.gain_cut_limit_db) > 1.0e-6f) return 65;
    if (std::fabs(observed.budget_peak_db - published.budget_peak_db) > 1.0e-6f) return 20;
    if (std::fabs(observed.bass_target_db - 4.0f) > 1.0e-6f) return 14;
    if (std::fabs(observed.bass_applied_db - 2.5f) > 1.0e-6f) return 15;
    if (std::fabs(observed.limiter_reduction_db - 1.25f) > 1.0e-6f) return 16;
    return 0;
}
