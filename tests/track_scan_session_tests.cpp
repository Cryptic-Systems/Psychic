#include "core/track_scan_session.h"

#include <cmath>
#include <cstdio>

static int fail(int code) { std::printf("track scan test failed at check %d\\n", code); return code; }

int main() {
    dsp::AdaptiveTrackScanSession scan;
    if (scan.snapshot().state != dsp::TrackScanState::Idle) return fail(1);
    if (!scan.begin(10.0)) return fail(2);
    if (scan.begin(10.0)) return fail(3);
    if (!scan.add_block(-12.f, -1.f, -18.f, 4.0)) return fail(4);

    auto progress = scan.snapshot();
    if (progress.state != dsp::TrackScanState::Scanning ||
        progress.report_ready || std::fabs(progress.progress - 0.4f) > 0.001f)
        return fail(5);
    if (scan.add_block(-12.f, -1.f, -18.f, 0.0)) return fail(6);

    if (!scan.add_block(-12.f, -1.f, -18.f, 6.0)) return fail(7);
    if (!scan.finalize()) return fail(8);
    if (scan.finalize()) return fail(9);

    const auto complete = scan.snapshot();
    if (complete.state != dsp::TrackScanState::Completed ||
        !complete.report_ready || complete.progress != 1.f)
        return fail(10);
    if (std::fabs(complete.analysis.duration_seconds - 10.0) > 1.0e-6)
        return fail(11);
    if (complete.intelligence.sufficient_data != true) return fail(12);
    if (scan.add_block(-12.f, -1.f, -18.f, 1.0)) return fail(13);
    if (scan.cancel()) return fail(14);

    // Cancellation retains partial measurements but never publishes a report.
    if (!scan.begin(20.0)) return fail(15);
    if (!scan.add_block(-30.f, -12.f, -35.f, 2.0)) return fail(16);
    if (!scan.cancel()) return fail(17);
    const auto cancelled = scan.snapshot();
    if (cancelled.state != dsp::TrackScanState::Cancelled ||
        cancelled.report_ready || cancelled.progress > 0.101f ||
        std::fabs(cancelled.analysis.duration_seconds - 2.0) > 1.0e-6)
        return fail(18);
    if (scan.add_block(-30.f, -12.f, -35.f, 1.0)) return fail(19);
    if (scan.finalize()) return fail(20);

    // Unknown duration is supported; progress is indeterminate until finalize.
    if (!scan.begin()) return fail(21);
    if (!scan.add_block(-30.f, -12.f, -35.f, 6.0)) return fail(22);
    if (scan.snapshot().progress != 0.f) return fail(23);
    if (!scan.finalize()) return fail(24);
    if (scan.snapshot().progress != 1.f) return fail(25);

    if (scan.begin(-1.0)) return fail(26);
    if (scan.begin(INFINITY)) return fail(27);
    return fail(0);
}
