#ifndef __RFLOW_SERVICE_IMPL_MEDIA_RTC_VIDEO_RTP_STATS_LOG_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_RTC_VIDEO_RTP_STATS_LOG_H__

#include <string>

#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"

namespace rflow::service::impl::media_util {

void PrintOutboundVideoStats(
    const std::string& pc_tag,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

void PrintInboundVideoStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);

}  // namespace rflow::service::impl::media_util

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_RTC_VIDEO_RTP_STATS_LOG_H__
