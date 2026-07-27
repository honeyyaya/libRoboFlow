#include "rtc/session/stats_observer.h"

#include <utility>

#include "api/make_ref_counted.h"

namespace rflow::core::rtc {

namespace {

class StatsCollectorObserverImpl : public webrtc::RTCStatsCollectorCallback {
 public:
    using Cb = std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>;

    explicit StatsCollectorObserverImpl(Cb cb) : cb_(std::move(cb)) {}

    void OnStatsDelivered(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
        if (cb_) cb_(report);
    }

 private:
    Cb cb_;
};

}  // namespace

webrtc::scoped_refptr<webrtc::RTCStatsCollectorCallback>
MakeStatsCollectorObserver(
    std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> cb) {
    return webrtc::make_ref_counted<StatsCollectorObserverImpl>(std::move(cb));
}

}  // namespace rflow::core::rtc
