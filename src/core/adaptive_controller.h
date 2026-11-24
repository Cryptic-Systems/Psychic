/*
 * Psychic Adaptive Audio Control Engine (AACE)
 *
 * Coordinates automatic gain/headroom decisions without doing audio analysis
 * or touching audio buffers. The caller supplies block measurements and
 * applies the returned targets on the audio thread.
 *
 * Header-only by design: fixed-size state, no allocation, locks, or callbacks.
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace dsp {

struct AdaptiveHeadroomBudget {
    float available_db = 0.f;
    float bass_db = 0.f;
    float preamp_db = 0.f;
    float gain_boost_db = 0.f;
};

class AdaptiveController {
public:
    void reset() noexcept {
        smoothed_headroom_db_ = 0.f;
        initialized_ = false;
    }

    /*
     * Allocate positive gain headroom from the current post-user-EQ peak.
     * The limiter remains the final safety boundary; this budget governs
     * slower tonal/loudness enhancements and deliberately ignores transient
     * limiter GR so a single peak cannot collapse every adaptive subsystem.
     */
    AdaptiveHeadroomBudget allocate_headroom(
        float source_peak_db,
        float ceiling_db,
        float safety_margin_db,
        float bass_weight,
        float preamp_weight,
        float gain_weight,
        float sample_rate,
        int frames) noexcept {

        const float dt = (sample_rate > 0.f && frames > 0)
            ? static_cast<float>(frames) / sample_rate : 0.01f;
        const float reserve = std::max(0.f, safety_margin_db) + kTransientReserveDb;
        const float raw_headroom = ceiling_db - source_peak_db - reserve;

        if (!initialized_) {
            smoothed_headroom_db_ = raw_headroom;
            initialized_ = true;
        } else {
            // Headroom contracts promptly but recovers gradually. This keeps
            // isolated transients from making bass/preamp controls pump.
            const float tau = raw_headroom < smoothed_headroom_db_
                ? kHeadroomAttackSeconds : kHeadroomReleaseSeconds;
            const float alpha = 1.f - std::exp(-dt / tau);
            smoothed_headroom_db_ += alpha * (raw_headroom - smoothed_headroom_db_);
        }

        AdaptiveHeadroomBudget out{};
        out.available_db = std::max(0.f, smoothed_headroom_db_);
        float bw = std::max(0.f, bass_weight);
        float pw = std::max(0.f, preamp_weight);
        float gw = std::max(0.f, gain_weight);
        float sum = bw + pw + gw;
        if (sum < 1.0e-6f) {
            bw = 0.5f; pw = 0.2f; gw = 0.3f; sum = 1.f;
        }
        out.bass_db = out.available_db * (bw / sum);
        out.preamp_db = out.available_db * (pw / sum);
        out.gain_boost_db = out.available_db * (gw / sum);
        return out;
    }

    /*
     * Smooth global auto-gain asymmetrically. Cuts are controlled quickly
     * enough to prevent overload, while recovery is deliberately gentler
     * to avoid audible level pumping. Time constants are independent of
     * callback block size (unlike a fixed per-block coefficient).
     */
    float smooth_gain(float current_db, float target_db,
                      float sample_rate, int frames) const noexcept {
        if (sample_rate <= 0.f || frames <= 0) return target_db;
        const float dt = static_cast<float>(frames) / sample_rate;
        const float tau = target_db < current_db
            ? kGainReductionSeconds : kGainRecoverySeconds;
        const float alpha = 1.f - std::exp(-dt / tau);
        return current_db + alpha * (target_db - current_db);
    }

    // Generic block-size-independent smoothing for bass, preamp and VB.
    // Reductions use the shorter time constant; increases ease in more slowly.
    float smooth_parameter(float current, float target, float sample_rate,
                           int frames, float increase_tau,
                           float decrease_tau) const noexcept {
        if (sample_rate <= 0.f || frames <= 0) return target;
        const float dt = static_cast<float>(frames) / sample_rate;
        const float tau = target < current ? decrease_tau : increase_tau;
        const float safe_tau = std::max(0.01f, tau);
        const float alpha = 1.f - std::exp(-dt / safe_tau);
        return current + alpha * (target - current);
    }

private:
    static constexpr float kTransientReserveDb = 0.5f;
    static constexpr float kHeadroomAttackSeconds = 0.12f;
    static constexpr float kHeadroomReleaseSeconds = 0.45f;
    static constexpr float kGainReductionSeconds = 0.22f;
    static constexpr float kGainRecoverySeconds = 0.38f;

    float smoothed_headroom_db_ = 0.f;
    bool initialized_ = false;
};

} // namespace dsp
