#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_INTERNALS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_INTERNALS_H__

// Push 推流端的 PeerConnection / Thread / Trace / Stats 等"非业务"的小工具。
// 拆出来主要为了让 push_streamer.cpp 单文件不超过千行，纯 helper 没有状态。
//
// 命名空间：rflow::service::impl::detail::push（仅 service/impl/media 内部使用）。

#include <cstdint>
#include <string>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"

#include "base/latency_trace.h"
#include "base/signaling_trace.h"
#include "media/push/push_streamer.h"
#include "media/rtc/peer_connection_utils.h"
#include "media/rtc/video_rtp_stats_log.h"

namespace webrtc {
class Thread;
}

namespace rflow::service::impl {
class CameraVideoTrackSource;
}

namespace rflow::service::impl::detail::push {

using rflow::common::base::LatencyTraceEnabled;
using rflow::common::base::SignalingNowUs;
using rflow::common::base::SignalingTimingTraceEnabled;
using rflow::service::impl::media_util::ClosePeerConnectionWithDeadline;
using rflow::service::impl::media_util::MakeVideoOfferOptions;
using rflow::service::impl::media_util::ParseVideoNetworkPriority;
using rflow::service::impl::media_util::PrintOutboundVideoStats;
using rflow::service::impl::media_util::StopWebrtcThreadWithDeadline;

inline void TraceSigTiming(const std::string& msg) {
    rflow::common::base::TraceSigTiming("push", msg);
}

// RFLOW_LATENCY_TRACE=1 时打开端到端编码/网络耗时 trace（见 base/latency_trace.h）。

inline webrtc::PeerConnectionInterface::RTCOfferAnswerOptions MakeOfferOptions() {
    return MakeVideoOfferOptions();
}

// 构造 push 端用于 PeerConnection 的 RTCConfiguration（IceServer/bundle/sdp_semantics 等）。
webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfiguration(
    const PushStreamerConfig& config);

// WebRTC 发送侧丢帧（VSE/media opt 等）：仅当 frames_dropped 相对上次有增量时打印。
void MaybeLogWebRtcSendDropStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

// 周期性推流链路健康摘要（见 RFLOW_PUSH_HEALTH_INTERVAL_SEC）。
void MaybeLogPushPipelineHealth(
    int interval_sec,
    const rflow::service::impl::CameraVideoTrackSource* camera,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

}  // namespace rflow::service::impl::detail::push

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_INTERNALS_H__
