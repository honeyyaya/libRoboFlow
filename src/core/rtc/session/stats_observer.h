#ifndef __RFLOW_CORE_RTC_SESSION_STATS_OBSERVER_H__
#define __RFLOW_CORE_RTC_SESSION_STATS_OBSERVER_H__

// 通用 webrtc::RTCStatsCollectorCallback 工厂：避免 client / service 各自写
// 同样的"包一层 callback"小类。调用方决定如何遍历 RTCStatsReport。

#include <functional>

#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"

namespace rflow::core::rtc {

webrtc::scoped_refptr<webrtc::RTCStatsCollectorCallback>
MakeStatsCollectorObserver(
    std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> cb);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_SESSION_STATS_OBSERVER_H__
