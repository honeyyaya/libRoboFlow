#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_INTERNALS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_INTERNALS_H__

// Pull 拉流端的 PeerConnection / Trace / Stats 等"非业务"的小工具。
// 拆出来主要为了让 pull_subscriber.cpp 单文件不超过千行，纯 helper 没有状态。
//
// 命名空间：rflow::service::impl::detail::pull（仅 service/impl/media 内部使用）。

#include <string>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"

#include "base/media_timing_trace.h"
#include "base/signaling_trace.h"
#include "media/rtc/video_rtp_stats_log.h"

namespace rflow::service::impl::detail::pull {

using rflow::common::base::MediaTimingTraceEnabled;
using rflow::common::base::MediaTimingTraceEveryN;
using rflow::common::base::SignalingNowUs;
using rflow::common::base::SignalingTimingTraceEnabled;
using rflow::service::impl::media_util::PrintInboundVideoStats;

inline void TraceSigTiming(const std::string& msg) {
    rflow::common::base::TraceSigTiming("pull", msg);
}

// 构造 pull 端用于 PeerConnection 的 RTCConfiguration（仅 STUN，UnifiedPlan）。
webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfig();

}  // namespace rflow::service::impl::detail::pull

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_INTERNALS_H__
