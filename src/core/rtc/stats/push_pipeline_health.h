#ifndef __RFLOW_CORE_RTC_STATS_PUSH_PIPELINE_HEALTH_H__
#define __RFLOW_CORE_RTC_STATS_PUSH_PIPELINE_HEALTH_H__

#include <api/scoped_refptr.h>
#include <cstdint>

namespace webrtc {
class RTCStatsReport;
}

namespace rflow::core::rtc {

/// 采集侧健康数据（由 PushStreamer 从 CameraVideoTrackSource 填充后传入）。
struct CapturePipelineHealthInput {
    size_t mjpeg_queue_depth{0};
    uint64_t mjpeg_stale_drops{0};
    uint64_t mjpeg_queue_full_drops{0};
    uint64_t mjpeg_latest_only_drops{0};
    uint64_t empty_payload_drops{0};
    uint64_t convert_fail_drops{0};
    uint64_t nv12_pool_busy{0};
};

/// 推流链路周期性健康摘要（默认 5s，见 RFLOW_PUSH_HEALTH_INTERVAL_SEC）。
void LogPushPipelineHealth(int interval_sec,
                           const CapturePipelineHealthInput& capture,
                           const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_STATS_PUSH_PIPELINE_HEALTH_H__
