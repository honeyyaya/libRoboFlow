#include "rtc/session/rtc_sync_stats.h"

#include "rtc/session/stats_observer.h"

#include "runtime/runtime_knobs.h"

#include <condition_variable>
#include <mutex>

namespace rflow::core::rtc {

std::chrono::milliseconds SyncGetPeerConnectionStatsTimeout() {
    const int ms = rflow::core::runtime::ReadInt("RFLOW_SYNC_GETSTATS_TIMEOUT_MS");
    return std::chrono::milliseconds(ms);
}

bool SyncGetPeerConnectionStats(
    webrtc::PeerConnectionInterface* pc,
    std::chrono::milliseconds timeout,
    const std::function<void(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>& consumer) {
    if (!pc || !consumer) {
        return false;
    }

    std::mutex              mu;
    std::condition_variable cv;
    bool                    done = false;

    auto cb = MakeStatsCollectorObserver(
        [&](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            consumer(report);
            {
                std::lock_guard<std::mutex> lk(mu);
                done = true;
            }
            cv.notify_one();
        });

    pc->GetStats(cb.get());

    std::unique_lock<std::mutex> lk(mu);
    return cv.wait_for(lk, timeout, [&done] { return done; });
}

}  // namespace rflow::core::rtc
