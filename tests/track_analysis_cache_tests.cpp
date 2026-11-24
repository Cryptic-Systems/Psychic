#include "core/track_analysis_cache.h"

#include <string>

int main() {
    dsp::AdaptiveTrackScanSession scan;
    if (!scan.begin(6.0)) return 1;
    if (!scan.add_block(-12.f, -2.f, -16.f, 6.0)) return 2;
    if (!scan.finalize()) return 3;
    const auto report = scan.snapshot();
    if (!report.report_ready || !report.intelligence.sufficient_data) return 4;

    dsp::AdaptiveTrackAnalysisCache cache(2);
    dsp::TrackCacheIdentity a{"library://track/a", 1000, 123456, "hash-a"};
    dsp::TrackCacheIdentity b{"library://track/b", 2000, 223456, "hash-b"};
    dsp::TrackCacheIdentity c{"library://track/c", 3000, 323456, "hash-c"};
    dsp::TrackCacheIdentity invalid{};

    if (cache.store(invalid, report)) return 5;
    auto partial = report;
    partial.state = dsp::TrackScanState::Cancelled;
    partial.report_ready = false;
    if (cache.store(a, partial)) return 6;

    if (!cache.store(a, report) || !cache.store(b, report)) return 7;
    dsp::TrackScanSnapshot out{};
    if (!cache.lookup(a, out) || !out.report_ready ||
        out.analysis.duration_seconds != report.analysis.duration_seconds)
        return 8;

    // A new revision of the same stable track ID invalidates the old report.
    auto changed = a;
    changed.file_size_bytes += 1;
    if (cache.lookup(changed, out)) return 9;
    auto stats = cache.stats();
    if (stats.stale_invalidations != 1 || stats.misses != 1) return 10;
    if (!cache.store(changed, report)) return 11;

    // Touch changed revision, then inserting C evicts B (least recently used).
    if (!cache.lookup(changed, out)) return 12;
    if (!cache.store(c, report)) return 13;
    if (!cache.lookup(changed, out)) return 14;
    if (cache.lookup(b, out)) return 15;

    stats = cache.stats();
    if (stats.entries != 2 || stats.capacity != 2 || stats.evictions != 1)
        return 16;
    if (!cache.invalidate("library://track/c")) return 17;
    if (cache.invalidate("library://track/c")) return 18;
    if (cache.stats().entries != 1) return 19;

    cache.clear();
    if (cache.stats().entries != 0) return 20;
    if (cache.lookup(changed, out)) return 21;
    return 0;
}
