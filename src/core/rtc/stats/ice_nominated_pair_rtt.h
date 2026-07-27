#ifndef __RFLOW_CORE_RTC_STATS_ICE_NOMINATED_PAIR_RTT_H__
#define __RFLOW_CORE_RTC_STATS_ICE_NOMINATED_PAIR_RTT_H__

#include <api/scoped_refptr.h>
#include <cstdint>

namespace webrtc {
class RTCStatsReport;
}

namespace rflow::core::rtc {

/// 对齐 `RtcStreamSession` Pipeline 日志：优先采纳 nominated pair 上的 RTT 读数，
/// 与「全 ICE pair 取 max」的 CollectStats 路径语义不同。
struct IceNominatedRttAggregation {
    double current_round_trip_time_s = 0.0;
    double total_round_trip_time_s   = 0.0;
    uint64_t responses_received      = 0;
};

void AccumulateNominatedIcePairRttFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    IceNominatedRttAggregation* out);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_STATS_ICE_NOMINATED_PAIR_RTT_H__
