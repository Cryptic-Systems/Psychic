#include "core/adaptive_controller.h"

#include <cassert>
#include <cmath>

int main() {
    dsp::AdaptiveController controller;

    // The positive-gain budget is shared, weighted, and margin-aware.
    const auto budget = controller.allocate_headroom(
        -6.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    assert(std::fabs(budget.available_db - 4.4f) < 1.0e-3f);
    assert(std::fabs((budget.bass_db + budget.preamp_db +
                      budget.gain_boost_db) - budget.available_db) < 1.0e-3f);

    // A sudden peak contracts the available budget progressively rather than
    // instantly withdrawing all enhancement headroom for one block.
    const auto transient = controller.allocate_headroom(
        0.0f, -0.1f, 1.0f, 0.5f, 0.2f, 0.3f, 48000.0f, 480);
    assert(transient.available_db > 0.0f);
    assert(transient.available_db < budget.available_db);

    // Smoothing is based on elapsed audio time, not a fixed per-block step.
    dsp::AdaptiveController smooth_a;
    dsp::AdaptiveController smooth_b;
    float a = 0.0f;
    float b = 0.0f;
    for (int i = 0; i < 10; ++i)
        a = smooth_a.smooth_gain(a, -6.0f, 48000.0f, 480);
    b = smooth_b.smooth_gain(b, -6.0f, 48000.0f, 4800);
    assert(std::fabs(a - b) < 1.0e-4f);
    assert(a < 0.0f && a > -6.0f);

    // Recovery is smooth and heads toward unity without a discontinuity.
    const float recovered = smooth_a.smooth_gain(a, 0.0f, 48000.0f, 480);
    assert(recovered > a && recovered < 0.0f);
    return 0;
}
