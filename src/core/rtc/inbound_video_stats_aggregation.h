#ifndef __RFLOW_CORE_RTC_INBOUND_VIDEO_STATS_AGGREGATION_H__
#define __RFLOW_CORE_RTC_INBOUND_VIDEO_STATS_AGGREGATION_H__

#include <api/scoped_refptr.h>
#include <cstdint>
#include <string>

namespace webrtc {
class RTCStatsReport;
}

namespace rflow::core::rtc {

/// Rolling merge of libwebrtc inbound video RTP counters across SSRC/report rows,
/// preserving both ABI「CollectStats」与 watchdog / Pipeline 日志各自的汇总语义。
struct InboundVideoRtpAggregation {
    uint64_t bytes_received = 0;
    uint64_t packets_received = 0;
    /// `librflow_stream_get_stats`：仅累加 >0 的 packets_lost（与旧实现一致）
    uint32_t lost_pkts_positive_sum = 0;
    /// 带符号累加，供丢包率等
    int64_t packets_lost_signed = 0;
    uint64_t frames_decoded = 0;
    /// Watchdog / 日志：跨行求和
    uint64_t frames_dropped_sum = 0;
    /// CollectStats：decode_fail_count 用逐行 max
    uint64_t frames_dropped_max = 0;
    uint32_t fps_uint = 0;
    double fps_double = 0.0;
    uint32_t jitter_ms_max = 0;
    double jitter_seconds_max = 0.0;
    uint32_t freeze_count_max = 0;
    double jitter_buffer_delay_seconds = 0.0;
    uint64_t jitter_buffer_emitted_count = 0;
    uint64_t fec_packets_received = 0;
    uint32_t nack_count_max = 0;
    uint32_t pli_count_max = 0;
    uint32_t fir_count_max = 0;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    double total_decode_time_s = 0.0;
    double total_processing_delay_s = 0.0;
    double total_assembly_time_s = 0.0;
    std::string decoder_implementation;
    std::string codec_id;
    bool have_video = false;
};

void AccumulateInboundVideoRtpFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    InboundVideoRtpAggregation* out);

/// ICE candidate pair：入站链路最大 RTT 与上行可用带宽推算（pull CollectStats 用）。
void AccumulateIncomingIcePairMetricsFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    uint32_t* rtt_ms_max,
    uint32_t* available_incoming_kbps_max);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_INBOUND_VIDEO_STATS_AGGREGATION_H__
