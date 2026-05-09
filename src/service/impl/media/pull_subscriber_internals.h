#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PULL_SUBSCRIBER_INTERNALS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PULL_SUBSCRIBER_INTERNALS_H__

// Pull 拉流端的 PeerConnection / Trace / Stats 等"非业务"的小工具。
// 拆出来主要为了让 pull_subscriber.cpp 单文件不超过千行，纯 helper 没有状态。
//
// 命名空间：rflow::service::impl::detail::pull（仅 service/impl/media 内部使用）。

#include <cstdint>
#include <string>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"

namespace rflow::service::impl::detail::pull {

// ---- trace switches -------------------------------------------------------

// RFLOW_SIGNALING_TIMING_TRACE=1 时打开 SDP/ICE 流的 us 级 trace。
bool SignalingTimingTraceEnabled();

// 单调钟 us 时间戳。
int64_t SignalingNowUs();

// 仅在 SignalingTimingTraceEnabled() 时输出，前缀 [SIG_TIMING][pull]。
void TraceSigTiming(const std::string& msg);

// RFLOW_MEDIA_TIMING_TRACE=1 时启用 SDK 内部媒体时序 trace。
bool MediaTimingTraceEnabled();

// 媒体时序 trace 每 N 帧打一次（缺省 30，范围 [1, 600]）。
unsigned MediaTimingTraceEveryN();

// ---- pc config helpers ----------------------------------------------------

// 构造 pull 端用于 PeerConnection 的 RTCConfiguration（仅 STUN，UnifiedPlan）。
webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfig();

// ---- stats logging --------------------------------------------------------

// 把统计 report 中的 inbound video 项打印出来；含 goog_timing_frame_info 解析。
void PrintInboundVideoStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

}  // namespace rflow::service::impl::detail::pull

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PULL_SUBSCRIBER_INTERNALS_H__
