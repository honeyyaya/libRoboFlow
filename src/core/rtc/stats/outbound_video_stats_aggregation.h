#ifndef __RFLOW_CORE_RTC_STATS_OUTBOUND_VIDEO_STATS_AGGREGATION_H__
#define __RFLOW_CORE_RTC_STATS_OUTBOUND_VIDEO_STATS_AGGREGATION_H__

#include <api/scoped_refptr.h>
#include <cstdint>

namespace webrtc {
class RTCStatsReport;
}

namespace rflow::core::rtc {

/// Push / 发布端：`GetStats` 报告中与视频出站 QoS 相关的聚合（与 Pull 侧 inbound 互为镜像）。
struct OutboundPublisherVideoAggregation {
    uint64_t out_bytes = 0;
    uint64_t out_pkts = 0;
    uint32_t lost_pkts = 0;
    uint32_t fps = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t rtt_ms = 0;
};

void AccumulateOutboundPublisherVideoFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    OutboundPublisherVideoAggregation* out);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_STATS_OUTBOUND_VIDEO_STATS_AGGREGATION_H__
