#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PUSH_STREAMER_INTERNALS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PUSH_STREAMER_INTERNALS_H__

// Push 推流端的 PeerConnection / Thread / Trace / Stats 等"非业务"的小工具。
// 拆出来主要为了让 push_streamer.cpp 单文件不超过千行，纯 helper 没有状态。
//
// 命名空间：rflow::service::impl::detail::push（仅 service/impl/media 内部使用）。

#include <cstdint>
#include <string>

#include "api/peer_connection_interface.h"
#include "api/priority.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"

#include "media/push_streamer.h"

namespace webrtc {
class Thread;
}  // namespace webrtc

namespace rflow::service::impl {
class CameraVideoTrackSource;
}

namespace rflow::service::impl::detail::push {

// ---- trace switches -------------------------------------------------------

// RFLOW_SIGNALING_TIMING_TRACE=1 时打开 SDP/ICE 流的 us 级 trace。
bool SignalingTimingTraceEnabled();

// 单调钟 us 时间戳。
int64_t SignalingNowUs();

// 仅在 SignalingTimingTraceEnabled() 时输出，前缀 [SIG_TIMING][push]。
void TraceSigTiming(const std::string& msg);

// RFLOW_LATENCY_TRACE=1 时打开端到端编码/网络耗时 trace。
bool LatencyTraceEnabled();

// ---- shutdown helpers -----------------------------------------------------

// 在独立线程内对 PeerConnection 调用 Close()，超时则放弃等待并 detach。
// 返回 true 表示在 timeout 内 Close 已返回；false 表示超时。
// 部分平台双 PC 回环下 Close 可能长期阻塞，需借此防止整体卡停。
bool ClosePeerConnectionWithDeadline(
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const char* log_tag,
    int timeout_sec);

// 类似策略：thread->Stop() 也在独立线程，超时放弃。
void StopWebrtcThreadWithDeadline(webrtc::Thread* thread, int timeout_sec);

// ---- pc config helpers ----------------------------------------------------

// "very_low / low / medium / high" → webrtc::Priority；未知值回退 kHigh。
webrtc::Priority ParseVideoNetworkPriority(const std::string& s);

// 构造 push 端用于 PeerConnection 的 RTCConfiguration（IceServer/bundle/sdp_semantics 等）。
webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfiguration(
    const PushStreamerConfig& config);

// 构造仅含视频、单 simulcast layer 的 OfferAnswerOptions。
webrtc::PeerConnectionInterface::RTCOfferAnswerOptions MakeOfferOptions();

// ---- stats logging --------------------------------------------------------

// 把统计 report 中的 outbound video 项打印出来；pc_tag 用于在多 peer 场景区分。
void PrintOutboundVideoStats(
    const std::string& pc_tag,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

// WebRTC 发送侧丢帧（VSE/media opt 等）：仅当 frames_dropped 相对上次有增量时打印。
void MaybeLogWebRtcSendDropStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

// 周期性推流链路健康摘要（见 RFLOW_PUSH_HEALTH_INTERVAL_SEC）。
void MaybeLogPushPipelineHealth(
    int interval_sec,
    const rflow::service::impl::CameraVideoTrackSource* camera,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

}  // namespace rflow::service::impl::detail::push

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PUSH_STREAMER_INTERNALS_H__
