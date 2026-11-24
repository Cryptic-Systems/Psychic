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
#include <atomic>
#include <cstdint>

namespace dsp {

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "AACE telemetry requires lock-free 32-bit atomics");
static_assert(std::atomic<float>::is_always_lock_free,
              "AACE telemetry requires lock-free float atomics");

struct AdaptiveHeadroomBudget {
    float available_db = 0.f;
    float budget_peak_db = -120.f;
    float bass_db = 0.f;
    float preamp_db = 0.f;
    float gain_boost_db = 0.f;
};

/*
 * Per-block explanation of adaptive decisions. All payload fields are atomic
 * so readers can inspect a coherent snapshot without racing the audio thread.
 */
enum AdaptiveDecisionReason : uint32_t {
    kDecisionActive          = 1u << 0,
    kDecisionBudgetLimited   = 1u << 1,
    kDecisionSafetyHint      = 1u << 2,
    kDecisionTargetLimited   = 1u << 3,
    kDecisionSilenceRecovery = 1u << 4,
    kDecisionDisabledFade    = 1u << 5,
    kDecisionContentAware    = 1u << 6
};

struct AdaptiveDecisionSnapshot {
    uint32_t block_index = 0;
    uint32_t bass_reasons = 0;
    uint32_t preamp_reasons = 0;
    uint32_t gain_reasons = 0;
    uint32_t virtual_bass_reasons = 0;
    float source_peak_db = -120.f;
    float source_rms_db = -120.f;
    float budget_peak_db = -120.f;
    float control_rms_db = -120.f;
    float output_rms_db = -120.f;
    float available_headroom_db = 0.f;
    float bass_budget_db = 0.f;
    float preamp_budget_db = 0.f;
    float gain_budget_db = 0.f;
    float bass_target_db = 0.f;
    float bass_applied_db = 0.f;
    float preamp_target_db = 0.f;
    float preamp_applied_db = 0.f;
    float gain_target_db = 0.f;
    float gain_applied_db = 0.f;
    float virtual_bass_target = 0.f;
    float virtual_bass_applied = 0.f;
    float limiter_reduction_db = 0.f;
};

class AdaptiveDecisionTelemetry {
public:
    void publish(const AdaptiveDecisionSnapshot& s) noexcept {
        sequence_.fetch_add(1, std::memory_order_acq_rel); // writer active
        block_index_.store(s.block_index, std::memory_order_relaxed);
        bass_reasons_.store(s.bass_reasons, std::memory_order_relaxed);
        preamp_reasons_.store(s.preamp_reasons, std::memory_order_relaxed);
        gain_reasons_.store(s.gain_reasons, std::memory_order_relaxed);
        virtual_bass_reasons_.store(s.virtual_bass_reasons, std::memory_order_relaxed);
        source_peak_db_.store(s.source_peak_db, std::memory_order_relaxed);
        source_rms_db_.store(s.source_rms_db, std::memory_order_relaxed);
        budget_peak_db_.store(s.budget_peak_db, std::memory_order_relaxed);
        control_rms_db_.store(s.control_rms_db, std::memory_order_relaxed);
        output_rms_db_.store(s.output_rms_db, std::memory_order_relaxed);
        available_headroom_db_.store(s.available_headroom_db, std::memory_order_relaxed);
        bass_budget_db_.store(s.bass_budget_db, std::memory_order_relaxed);
        preamp_budget_db_.store(s.preamp_budget_db, std::memory_order_relaxed);
        gain_budget_db_.store(s.gain_budget_db, std::memory_order_relaxed);
        bass_target_db_.store(s.bass_target_db, std::memory_order_relaxed);
        bass_applied_db_.store(s.bass_applied_db, std::memory_order_relaxed);
        preamp_target_db_.store(s.preamp_target_db, std::memory_order_relaxed);
        preamp_applied_db_.store(s.preamp_applied_db, std::memory_order_relaxed);
        gain_target_db_.store(s.gain_target_db, std::memory_order_relaxed);
        gain_applied_db_.store(s.gain_applied_db, std::memory_order_relaxed);
        virtual_bass_target_.store(s.virtual_bass_target, std::memory_order_relaxed);
        virtual_bass_applied_.store(s.virtual_bass_applied, std::memory_order_relaxed);
        limiter_reduction_db_.store(s.limiter_reduction_db, std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release); // publish complete
    }

    bool read(AdaptiveDecisionSnapshot& out) const noexcept {
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint32_t before = sequence_.load(std::memory_order_acquire);
            if (before == 0 || (before & 1u)) continue;
            out.block_index = block_index_.load(std::memory_order_relaxed);
            out.bass_reasons = bass_reasons_.load(std::memory_order_relaxed);
            out.preamp_reasons = preamp_reasons_.load(std::memory_order_relaxed);
            out.gain_reasons = gain_reasons_.load(std::memory_order_relaxed);
            out.virtual_bass_reasons = virtual_bass_reasons_.load(std::memory_order_relaxed);
            out.source_peak_db = source_peak_db_.load(std::memory_order_relaxed);
            out.source_rms_db = source_rms_db_.load(std::memory_order_relaxed);
            out.budget_peak_db = budget_peak_db_.load(std::memory_order_relaxed);
            out.control_rms_db = control_rms_db_.load(std::memory_order_relaxed);
            out.output_rms_db = output_rms_db_.load(std::memory_order_relaxed);
            out.available_headroom_db = available_headroom_db_.load(std::memory_order_relaxed);
            out.bass_budget_db = bass_budget_db_.load(std::memory_order_relaxed);
            out.preamp_budget_db = preamp_budget_db_.load(std::memory_order_relaxed);
            out.gain_budget_db = gain_budget_db_.load(std::memory_order_relaxed);
            out.bass_target_db = bass_target_db_.load(std::memory_order_relaxed);
            out.bass_applied_db = bass_applied_db_.load(std::memory_order_relaxed);
            out.preamp_target_db = preamp_target_db_.load(std::memory_order_relaxed);
            out.preamp_applied_db = preamp_applied_db_.load(std::memory_order_relaxed);
            out.gain_target_db = gain_target_db_.load(std::memory_order_relaxed);
            out.gain_applied_db = gain_applied_db_.load(std::memory_order_relaxed);
            out.virtual_bass_target = virtual_bass_target_.load(std::memory_order_relaxed);
            out.virtual_bass_applied = virtual_bass_applied_.load(std::memory_order_relaxed);
            out.limiter_reduction_db = limiter_reduction_db_.load(std::memory_order_relaxed);
            const uint32_t after = sequence_.load(std::memory_order_acquire);
            if (before == after) return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> sequence_{0};
    std::atomic<uint32_t> block_index_{0};
    std::atomic<uint32_t> bass_reasons_{0}, preamp_reasons_{0}, gain_reasons_{0};
    std::atomic<uint32_t> virtual_bass_reasons_{0};
    std::atomic<float> source_peak_db_{-120.f}, source_rms_db_{-120.f};
    std::atomic<float> budget_peak_db_{-120.f};
    std::atomic<float> control_rms_db_{-120.f}, output_rms_db_{-120.f};
    std::atomic<float> available_headroom_db_{0.f}, bass_budget_db_{0.f};
    std::atomic<float> preamp_budget_db_{0.f}, gain_budget_db_{0.f};
    std::atomic<float> bass_target_db_{0.f}, bass_applied_db_{0.f};
    std::atomic<float> preamp_target_db_{0.f}, preamp_applied_db_{0.f};
    std::atomic<float> gain_target_db_{0.f}, gain_applied_db_{0.f};
    std::atomic<float> virtual_bass_target_{0.f}, virtual_bass_applied_{0.f};
    std::atomic<float> limiter_reduction_db_{0.f};
};

class AdaptiveController {
public:
    void reset() noexcept {
        smoothed_headroom_db_ = 0.f;
        transient_density_ = 0.f;
        initialized_ = false;
    }

    /*
     * Allocate positive gain headroom from post-user-EQ peak and RMS. Isolated
     * high-crest events are discounted until recurring events build density.
     * The limiter remains the final safety boundary; this budget governs
     * slower enhancements and never replaces per-sample peak protection.
     */
    AdaptiveHeadroomBudget allocate_headroom(
        float source_peak_db,
        float source_rms_db,
        float ceiling_db,
        float safety_margin_db,
        float bass_weight,
        float preamp_weight,
        float gain_weight,
        float sample_rate,
        int frames) noexcept {

        const float dt = (sample_rate > 0.f && frames > 0)
            ? static_cast<float>(frames) / sample_rate : 0.01f;
        /*
         * High peak-to-RMS crest is initially treated as an isolated transient,
         * not sustained headroom pressure. Repeated high-crest blocks build
         * transient density over time. The limiter protects every sample.
         */
        const float crest_db = source_peak_db - source_rms_db;
        const float transient_excess = std::max(0.f, crest_db - kTransientCrestThresholdDb);
        const float density_tau = transient_excess > 0.f
            ? kTransientDensityAttackSeconds : kTransientDensityReleaseSeconds;
        const float density_alpha = 1.f - std::exp(-dt / density_tau);
        transient_density_ += density_alpha * ((transient_excess > 0.f ? 1.f : 0.f) -
                                                transient_density_);
        transient_density_ = std::max(0.f, std::min(1.f, transient_density_));
        const float budget_peak_db =
            source_peak_db - transient_excess * (1.f - transient_density_);

        const float reserve = std::max(0.f, safety_margin_db) + kTransientReserveDb;
        const float raw_headroom = ceiling_db - budget_peak_db - reserve;

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
        out.budget_peak_db = budget_peak_db;
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
    static constexpr float kTransientCrestThresholdDb = 12.f;
    static constexpr float kTransientDensityAttackSeconds = 0.35f;
    static constexpr float kTransientDensityReleaseSeconds = 0.80f;
    static constexpr float kHeadroomAttackSeconds = 0.12f;
    static constexpr float kHeadroomReleaseSeconds = 0.45f;
    static constexpr float kGainReductionSeconds = 0.22f;
    static constexpr float kGainRecoverySeconds = 0.38f;

    float smoothed_headroom_db_ = 0.f;
    float transient_density_ = 0.f;
    bool initialized_ = false;
};

} // namespace dsp
