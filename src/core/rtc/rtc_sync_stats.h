#ifndef __RFLOW_CORE_RTC_RTC_SYNC_STATS_H__
#define __RFLOW_CORE_RTC_RTC_SYNC_STATS_H__

#include <api/peer_connection_interface.h>
#include <api/stats/rtc_stats_report.h>

#include <chrono>
#include <functional>

namespace rflow::core::rtc {

/// `SyncGetPeerConnectionStats` 建议使用的超时：`RFLOW_SYNC_GETSTATS_TIMEOUT_MS`（默认 1500）。
std::chrono::milliseconds SyncGetPeerConnectionStatsTimeout();

/// 在 `timeout` 内阻塞等待 `GetStats` 回调；失败返回 false。
/// `consumer` 在 WebRTC 统计分发线程上调用；返回后主线程可安全读取其已写入的数据。
bool SyncGetPeerConnectionStats(
    webrtc::PeerConnectionInterface* pc,
    std::chrono::milliseconds timeout,
    const std::function<void(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>& consumer);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_RTC_SYNC_STATS_H__
