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
    if (std::fabs(observed.budget_peak_db - published.budget_peak_db) > 1.0e-6f) return 20;
    if (std::fabs(observed.bass_target_db - 4.0f) > 1.0e-6f) return 14;
    if (std::fabs(observed.bass_applied_db - 2.5f) > 1.0e-6f) return 15;
    if (std::fabs(observed.limiter_reduction_db - 1.25f) > 1.0e-6f) return 16;
    return 0;
}
