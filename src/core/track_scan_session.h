/*
 * Psychic host-side incremental track scan session.
 *
 * Owns no decoded audio. A host decoder feeds measurements from a worker
 * thread; snapshots may be read concurrently by UI/host code. Never call this
 * from the real-time DSP callback.
 */
#pragma once

#include "track_analyzer.h"
#include "track_intelligence.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace dsp {

enum class TrackScanState : uint32_t {
    Idle = 0,
    Scanning,
    Completed,
    Cancelled
};

struct TrackScanSnapshot {
    TrackScanState state = TrackScanState::Idle;
    double expected_duration_seconds = 0.0;
    double processed_duration_seconds = 0.0;
    float progress = 0.f;
    bool report_ready = false;
    TrackAnalysisSummary analysis{};
    TrackIntelligenceReport intelligence{};
};

/*
 * Lifecycle:
 *   begin(expected_duration) -> add_block(...) zero or more times
 *   -> finalize() OR cancel().
 *
 * expected_duration_seconds may be zero when the decoder cannot know duration;
 * progress then remains zero until completion. finalize() publishes the
 * advisory report. Cancellation preserves a partial analysis snapshot but
 * never marks that report as complete/ready.
 *
 * Each public method is synchronized. A single producer should call
 * add_block(); any number of host/UI readers may call snapshot(). The mutex is
 * deliberately acceptable here because this class is background-only.
 */
class AdaptiveTrackScanSession {
public:
    bool begin(double expected_duration_seconds = 0.0) noexcept {
        if (!std::isfinite(expected_duration_seconds) ||
            expected_duration_seconds < 0.0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == TrackScanState::Scanning) return false;
        analyzer_.reset();
        expected_duration_seconds_ = expected_duration_seconds;
        processed_duration_seconds_ = 0.0;
        intelligence_ = TrackIntelligenceReport{};
        state_ = TrackScanState::Scanning;
        return true;
    }

    bool add_block(float rms_db, float peak_db, float bass_rms_db,
                   double duration_seconds, uint32_t transition = 0,
                   float transition_confidence = 0.f) noexcept {
        if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != TrackScanState::Scanning) return false;
        analyzer_.add_block(rms_db, peak_db, bass_rms_db, duration_seconds,
                            transition, transition_confidence);
        processed_duration_seconds_ += duration_seconds;
        return true;
    }

    bool finalize() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != TrackScanState::Scanning) return false;
        intelligence_ = AdaptiveTrackIntelligence::analyze(analyzer_.summary());
        state_ = TrackScanState::Completed;
        return true;
    }

    bool cancel() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != TrackScanState::Scanning) return false;
        state_ = TrackScanState::Cancelled;
        intelligence_ = TrackIntelligenceReport{};
        return true;
    }

    TrackScanSnapshot snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        TrackScanSnapshot out{};
        out.state = state_;
        out.expected_duration_seconds = expected_duration_seconds_;
        out.processed_duration_seconds = processed_duration_seconds_;
        if (expected_duration_seconds_ > 0.0) {
            const double ratio = processed_duration_seconds_ /
                                 expected_duration_seconds_;
            out.progress = state_ == TrackScanState::Completed ? 1.f :
                static_cast<float>(std::max(0.0, std::min(0.999, ratio)));
        } else if (state_ == TrackScanState::Completed) {
            out.progress = 1.f;
        }
        out.report_ready = state_ == TrackScanState::Completed;
        out.analysis = analyzer_.summary();
        if (out.report_ready) out.intelligence = intelligence_;
        return out;
    }

private:
    mutable std::mutex mutex_;
    AdaptiveTrackAnalyzer analyzer_{};
    TrackIntelligenceReport intelligence_{};
    TrackScanState state_ = TrackScanState::Idle;
    double expected_duration_seconds_ = 0.0;
    double processed_duration_seconds_ = 0.0;
};

} // namespace dsp
