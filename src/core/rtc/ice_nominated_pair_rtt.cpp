#include "rtc/ice_nominated_pair_rtt.h"

#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"

namespace rflow::core::rtc {

void AccumulateNominatedIcePairRttFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    IceNominatedRttAggregation* out) {
    if (!report || !out) {
        return;
    }

    bool have_pair = false;
    for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
        if (!pair) continue;
        const bool nominated = pair->nominated && *pair->nominated;
        if (have_pair && !nominated) continue;
        if (pair->current_round_trip_time) {
            out->current_round_trip_time_s = *pair->current_round_trip_time;
        }
        if (pair->total_round_trip_time) {
            out->total_round_trip_time_s = *pair->total_round_trip_time;
        }
        if (pair->responses_received) {
            out->responses_received = *pair->responses_received;
        }
        if (nominated) {
            have_pair = true;
        } else if (!have_pair) {
            have_pair = true;
        }
    }
}

}  // namespace rflow::core::rtc
