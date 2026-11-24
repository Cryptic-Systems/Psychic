#include "core/track_intelligence.h"
#include <cmath>
#include <limits>
using namespace dsp;

static TrackAnalysisSummary profile() {
    TrackAnalysisSummary s{};
    s.duration_seconds=100.0; s.active_seconds=100.0;
    s.integrated_rms_db=-18.f;
    s.bass_rms_db=-32.f; s.bass_prominence_db=-14.f;
    s.sample_peak_db=-1.f; s.mean_crest_db=10.f; s.max_crest_db=14.f;
    s.crest_p90_db=6.f; s.high_crest_seconds_ratio=.01f;
    s.loudness_p10_db=-20.f; s.loudness_p50_db=-18.f;
    s.loudness_p95_db=-8.f; s.loudness_range_db=4.f;
    return s;
}
static float confidence(const TrackAnalysisSummary& s, TrackTrait trait) {
    const auto r=AdaptiveTrackIntelligence::analyze(s);
    for(std::size_t i=0;i<r.finding_count;++i)
        if(r.findings[i].trait==trait) return r.findings[i].confidence;
    return 0.f;
}
static bool near(float a,float b) { return std::fabs(a-b)<0.015f; }

int main() {
    auto s=profile();
    // Threshold and midpoint checks keep the hand-authored confidence ramps
    // predictable and monotonic; these are engineering heuristics, not labels.
    s.loudness_p50_db=-14.f;
    if(!near(confidence(s,TrackTrait::ConsistentlyLoud),0.f)) return 1;
    s.loudness_p50_db=-12.f;
    const float loud_mid=confidence(s,TrackTrait::ConsistentlyLoud);
    if(!near(loud_mid,2.f/7.f)) return 2;
    s.loudness_p50_db=-10.f;
    if(!(confidence(s,TrackTrait::ConsistentlyLoud)>loud_mid)) return 3;

    s=profile(); s.loudness_range_db=15.f;
    if(!near(confidence(s,TrackTrait::HighDynamicRange),.5f)) return 4;
    s.loudness_range_db=20.f;
    if(!near(confidence(s,TrackTrait::HighDynamicRange),1.f)) return 5;

    s=profile(); s.bass_prominence_db=-5.f;
    if(!near(confidence(s,TrackTrait::BassProminent),.5f)) return 6;
    s.bass_prominence_db=-2.f;
    if(!near(confidence(s,TrackTrait::BassProminent),1.f)) return 7;

    s=profile(); s.crest_p90_db=12.f; s.high_crest_seconds_ratio=.35f;
    if(!near(confidence(s,TrackTrait::TransientRich),.5f)) return 8;
    s.crest_p90_db=16.f;
    if(!near(confidence(s,TrackTrait::TransientRich),1.f)) return 9;

    s=profile(); s.transition_seconds=21.5;
    if(!near(confidence(s,TrackTrait::SectionVariable),.5f)) return 10;
    s.transition_seconds=35.0;
    if(!near(confidence(s,TrackTrait::SectionVariable),1.f)) return 11;

    s=profile(); s.integrated_rms_db=-29.f;
    if(!near(confidence(s,TrackTrait::LowLevelQuiet),.5f)) return 12;

    // Malformed summaries must fail closed instead of turning NaNs into
    // maximum-confidence recommendations through floating-point min/max.
    s=profile(); s.integrated_rms_db=std::numeric_limits<float>::quiet_NaN();
    if(AdaptiveTrackIntelligence::analyze(s).sufficient_data) return 13;
    s=profile(); s.bass_prominence_db=std::numeric_limits<float>::infinity();
    if(AdaptiveTrackIntelligence::analyze(s).sufficient_data) return 14;
    s=profile(); s.loudness_p10_db=-10.f; s.loudness_p50_db=-20.f;
    if(AdaptiveTrackIntelligence::analyze(s).sufficient_data) return 15;
    s=profile(); s.high_crest_seconds_ratio=1.1f;
    if(AdaptiveTrackIntelligence::analyze(s).sufficient_data) return 16;
    s=profile(); s.transition_seconds=-1.0;
    if(AdaptiveTrackIntelligence::analyze(s).sufficient_data) return 17;
    return 0;
}
