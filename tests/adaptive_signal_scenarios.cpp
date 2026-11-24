#include "core/adaptive_controller.h"

#include <cmath>
#include <cstdint>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 48000;
constexpr int kBlockFrames = 480;

struct ScenarioRunner {
    dsp::AdaptiveController controller;
    double bass_z = 0.0;
    double bass_a = std::exp(-2.0 * kPi * 120.0 / kSampleRate);
    int sample_index = 0;

    template <typename Generator>
    dsp::AdaptiveTrackState block(Generator generate) {
        double source_sum = 0.0;
        double bass_sum = 0.0;
        for (int i = 0; i < kBlockFrames; ++i, ++sample_index) {
            const double x = generate(sample_index);
            source_sum += x * x;
            bass_z = (1.0 - bass_a) * x + bass_a * bass_z;
            bass_sum += bass_z * bass_z;
        }
        return controller.update_track_state(
            source_sum / kBlockFrames, bass_sum / kBlockFrames,
            static_cast<float>(kSampleRate), kBlockFrames);
    }
};

double tone(double sample, double frequency, double amplitude) {
    return amplitude * std::sin(2.0 * kPi * frequency * sample / kSampleRate);
}

} // namespace

int main() {
    // A heavily controlled/compressed-style multitone with small cyclic gain
    // movement should settle as steady rather than continually predicting a
    // new section.
    ScenarioRunner compressed;
    dsp::AdaptiveTrackState compressed_state{};
    for (int block = 0; block < 900; ++block) {
        compressed_state = compressed.block([block](int n) {
            const double modulation_db = 0.65 * std::sin(2.0 * kPi * 1.7 * n / kSampleRate);
            const double gain = std::pow(10.0, modulation_db / 20.0);
            return gain * (tone(n, 55.0, 0.12) + tone(n, 440.0, 0.08) +
                           tone(n, 1800.0, 0.06) + tone(n, 6200.0, 0.025));
        });
    }
    if (compressed_state.section_transition != dsp::kSectionSteady) return 1;
    if (std::fabs(compressed_state.anticipatory_gain_hint_db) > 1.0e-6f) return 2;

    // Percussive transients at 100 ms intervals sit over a continuous bed.
    // Their sparse energy should not be mistaken for a sustained section rise.
    ScenarioRunner percussion;
    dsp::AdaptiveTrackState percussion_state{};
    for (int block = 0; block < 900; ++block) {
        percussion_state = percussion.block([](int n) {
            const double bed = tone(n, 110.0, 0.045) + tone(n, 880.0, 0.035);
            const bool hit = (n % 4800) < 2;
            return bed + (hit ? 0.95 : 0.0);
        });
    }
    if (percussion_state.section_transition != dsp::kSectionSteady) return 3;

    // A clipped/limited-style static master should not create a false trend
    // after its rolling envelopes have settled.
    ScenarioRunner clipped;
    dsp::AdaptiveTrackState clipped_state{};
    for (int block = 0; block < 900; ++block) {
        clipped_state = clipped.block([](int n) {
            const double x = 0.9 * (tone(n, 60.0, 0.65) +
                                    tone(n, 700.0, 0.55) +
                                    tone(n, 5000.0, 0.25));
            return x > 0.72 ? 0.72 : (x < -0.72 ? -0.72 : x);
        });
    }
    if (clipped_state.section_transition != dsp::kSectionSteady) return 4;

    // A genuine gradual programme-level fade should be detected with the
    // correct hint polarity. The gain envelope rises from -30 to -6 dB.
    ScenarioRunner fade_in;
    dsp::AdaptiveTrackState fade_state{};
    bool rising_seen = false;
    for (int block = 0; block < 1200; ++block) {
        fade_state = fade_in.block([block](int n) {
            const double elapsed = static_cast<double>(n) / kSampleRate;
            const double progress = std::fmin(1.0, elapsed / 8.0);
            const double gain = std::pow(10.0, (-30.0 + 24.0 * progress) / 20.0);
            return gain * (tone(n, 55.0, 0.12) + tone(n, 440.0, 0.08) +
                           tone(n, 1800.0, 0.06) + tone(n, 6200.0, 0.025));
        });
        if (fade_state.section_transition == dsp::kSectionRising) {
            rising_seen = true;
            if (fade_state.anticipatory_gain_hint_db >= 0.0f) return 5;
        }
    }
    if (!rising_seen) return 6;

    // A matching fade-out must produce a positive recovery hint.
    ScenarioRunner fade_out;
    dsp::AdaptiveTrackState falling_state{};
    bool falling_seen = false;
    for (int block = 0; block < 1200; ++block) {
        falling_state = fade_out.block([block](int n) {
            const double elapsed = static_cast<double>(n) / kSampleRate;
            const double progress = std::fmin(1.0, elapsed / 8.0);
            const double gain = std::pow(10.0, (-6.0 - 24.0 * progress) / 20.0);
            return gain * (tone(n, 55.0, 0.12) + tone(n, 440.0, 0.08) +
                           tone(n, 1800.0, 0.06) + tone(n, 6200.0, 0.025));
        });
        if (falling_state.section_transition == dsp::kSectionFalling) {
            falling_seen = true;
            if (falling_state.anticipatory_gain_hint_db <= 0.0f) return 7;
        }
    }
    if (!falling_seen) return 8;

    return 0;
}
