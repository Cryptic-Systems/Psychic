#include "core/track_intelligence.h"
#include <cmath>
using namespace dsp;
static bool has(const TrackIntelligenceReport& r, TrackTrait t) {
    for (std::size_t i=0;i<r.finding_count;++i) if(r.findings[i].trait==t) return true;
    return false;
}
int main() {
    TrackAnalysisSummary loud{};
    loud.duration_seconds=240; loud.active_seconds=230; loud.silence_seconds=10;
    loud.integrated_rms_db=-8; loud.loudness_p10_db=-10; loud.loudness_p50_db=-8;
    loud.loudness_p95_db=-6; loud.loudness_range_db=4; loud.bass_rms_db=-10;
    loud.bass_prominence_db=-2; loud.sample_peak_db=-0.2f; loud.mean_crest_db=8;
    loud.max_crest_db=22; loud.crest_p90_db=9; loud.high_crest_seconds_ratio=.01f;
    auto report=AdaptiveTrackIntelligence::analyze(loud);
    if(!report.sufficient_data) return 1;
    if(!has(report,TrackTrait::ConsistentlyLoud)) return 2;
    if(!has(report,TrackTrait::BassProminent)) return 3;
    if(has(report,TrackTrait::TransientRich)) return 4;
    if(report.recommendation_count!=report.finding_count) return 5;
    loud.crest_p90_db=16; loud.high_crest_seconds_ratio=.35f;
    report=AdaptiveTrackIntelligence::analyze(loud);
    if(!has(report,TrackTrait::TransientRich)) return 6;

    TrackAnalysisSummary dynamic{};
    dynamic.duration_seconds=180; dynamic.active_seconds=165;
    dynamic.integrated_rms_db=-27; dynamic.loudness_p10_db=-38;
    dynamic.loudness_p50_db=-27; dynamic.loudness_p95_db=-10;
    dynamic.loudness_range_db=28; dynamic.bass_prominence_db=-12;
    dynamic.crest_p90_db=7; dynamic.high_crest_seconds_ratio=.02f;
    dynamic.transition_seconds=18;
    report=AdaptiveTrackIntelligence::analyze(dynamic);
    if(!has(report,TrackTrait::LowLevelQuiet)) return 7;
    if(!has(report,TrackTrait::HighDynamicRange)) return 8;
    if(!has(report,TrackTrait::SectionVariable)) return 9;
    if(has(report,TrackTrait::ConsistentlyLoud)) return 10;

    TrackAnalysisSummary short_track{}; short_track.duration_seconds=2; short_track.active_seconds=2;
    report=AdaptiveTrackIntelligence::analyze(short_track);
    if(report.sufficient_data || report.finding_count) return 11;
    TrackAnalysisSummary silence{}; silence.duration_seconds=30;
    report=AdaptiveTrackIntelligence::analyze(silence);
    if(report.sufficient_data || report.recommendation_count) return 12;
    for(std::size_t i=0;i<report.finding_count;++i)
        if(!std::isfinite(report.findings[i].confidence) ||
           report.findings[i].confidence<0 || report.findings[i].confidence>1) return 13;
    if(AdaptiveTrackIntelligence::trait_name(TrackTrait::TransientRich)==nullptr) return 14;
    return 0;
}
