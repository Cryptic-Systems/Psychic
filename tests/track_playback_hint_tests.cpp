#include "core/track_playback_hint.h"

#include <cmath>
#include <limits>

using namespace dsp;

static TrackScanSnapshot completed_scan() {
    TrackScanSnapshot s{};
    s.state = TrackScanState::Completed;
    s.report_ready = true;
    s.intelligence.sufficient_data = true;
    s.analysis.duration_seconds = 180.0;
    s.analysis.active_seconds = 170.0;
    s.analysis.bass_rms_db = -22.f;
    s.analysis.mean_crest_db = 11.5f;
    return s;
}

int main() {
    TrackPlaybackHint hint{};
    auto scan = completed_scan();
    if (!make_track_playback_hint(scan, &hint)) return 1;
    if (std::fabs(hint.analysis.bass_energy_db + 22.f) > 1e-5f) return 2;
    if (std::fabs(hint.analysis.crest_db - 11.5f) > 1e-5f) return 3;
    if (hint.max_safe_bass_db != 24.f) return 4;

    // The adapter cannot promote partial or insufficient scans to hints.
    scan.state = TrackScanState::Scanning;
    if (make_track_playback_hint(scan, &hint)) return 5;
    scan = completed_scan();
    scan.report_ready = false;
    if (make_track_playback_hint(scan, &hint)) return 6;
    scan = completed_scan();
    scan.intelligence.sufficient_data = false;
    if (make_track_playback_hint(scan, &hint)) return 7;

    // Malformed scanner metrics fail closed; outputs are not partially written.
    scan = completed_scan();
    scan.analysis.bass_rms_db = std::numeric_limits<float>::quiet_NaN();
    hint.analysis.crest_db = 77.f;
    if (make_track_playback_hint(scan, &hint)) return 8;
    if (hint.analysis.crest_db != 77.f) return 9;
    scan = completed_scan();
    scan.analysis.mean_crest_db = std::numeric_limits<float>::infinity();
    if (make_track_playback_hint(scan, &hint)) return 10;

    // Bounded conversion prevents extreme but finite values from escaping.
    scan = completed_scan();
    scan.analysis.bass_rms_db = 30.f;
    scan.analysis.mean_crest_db = 60.f;
    if (!make_track_playback_hint(scan, &hint)) return 11;
    if (hint.analysis.bass_energy_db != 24.f ||
        hint.analysis.crest_db != 24.f) return 12;

    if (apply_track_playback_hint(nullptr, completed_scan()) !=
        DSP_ERR_INVALID_ARG) return 13;

    DspConfig config{};
    config.sample_rate = 48000.f;
    config.channels = 2;
    config.max_block = 1024;
    DspEngine* engine = dsp_create(&config);
    if (!engine) return 14;
    if (apply_track_playback_hint(engine, completed_scan()) != DSP_OK) {
        dsp_destroy(engine);
        return 15;
    }
    if (std::fabs(dsp_limiter_get_crest_hint(engine) - 11.5f) > 1e-4f) {
        dsp_destroy(engine);
        return 16;
    }
    // A rejected scan must not overwrite the last valid playback hint.
    auto invalid = completed_scan();
    invalid.report_ready = false;
    if (apply_track_playback_hint(engine, invalid) != DSP_ERR_INVALID_ARG) {
        dsp_destroy(engine);
        return 17;
    }
    if (std::fabs(dsp_limiter_get_crest_hint(engine) - 11.5f) > 1e-4f) {
        dsp_destroy(engine);
        return 18;
    }
    dsp_destroy(engine);
    return 0;
}
