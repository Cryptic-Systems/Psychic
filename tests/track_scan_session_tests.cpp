#include "core/track_scan_session.h"

#include <cmath>

int main() {
    dsp::AdaptiveTrackScanSession scan;
    if (scan.snapshot().state != dsp::TrackScanState::Idle) return 1;
    if (!scan.begin(10.0)) return 2;
    if (scan.begin(10.0)) return 3;
    if (!scan.add_block(-12.f, -1.f, -18.f, 4.0)) return 4;

    auto progress = scan.snapshot();
    if (progress.state != dsp::TrackScanState::Scanning ||
        progress.report_ready || std::fabs(progress.progress - 0.4f) > 0.001f)
        return 5;
    if (scan.add_block(-12.f, -1.f, -18.f, 0.0)) return 6;

    if (!scan.add_block(-12.f, -1.f, -18.f, 6.0)) return 7;
    if (!scan.finalize()) return 8;
    if (scan.finalize()) return 9;

    const auto complete = scan.snapshot();
    if (complete.state != dsp::TrackScanState::Completed ||
        !complete.report_ready || complete.progress != 1.f)
        return 10;
    if (std::fabs(complete.analysis.duration_seconds - 10.0) > 1.0e-6)
        return 11;
    if (complete.intelligence.sufficient_data != true) return 12;
    if (scan.add_block(-12.f, -1.f, -18.f, 1.0)) return 13;
    if (scan.cancel()) return 14;

    // Cancellation retains partial measurements but never publishes a report.
    if (!scan.begin(20.0)) return 15;
    if (!scan.add_block(-30.f, -12.f, -35.f, 2.0)) return 16;
    if (!scan.cancel()) return 17;
    const auto cancelled = scan.snapshot();
    if (cancelled.state != dsp::TrackScanState::Cancelled ||
        cancelled.report_ready || cancelled.progress > 0.101f ||
        std::fabs(cancelled.analysis.duration_seconds - 2.0) > 1.0e-6)
        return 18;
    if (scan.add_block(-30.f, -12.f, -35.f, 1.0)) return 19;
    if (scan.finalize()) return 20;

    // Unknown duration is supported; progress is indeterminate until finalize.
    if (!scan.begin()) return 21;
    if (!scan.add_block(-30.f, -12.f, -35.f, 6.0)) return 22;
    if (scan.snapshot().progress != 0.f) return 23;
    if (!scan.finalize()) return 24;
    if (scan.snapshot().progress != 1.f) return 25;

    if (scan.begin(-1.0)) return 26;
    if (scan.begin(INFINITY)) return 27;
    return 0;
}
