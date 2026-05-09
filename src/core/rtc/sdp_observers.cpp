#include "core/rtc/sdp_observers.h"

#include <utility>

#include "api/make_ref_counted.h"

namespace rflow::core::rtc {

namespace {

class SetRemoteDescObserverImpl : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
    explicit SetRemoteDescObserverImpl(std::function<void(webrtc::RTCError)> on_done)
        : on_done_(std::move(on_done)) {}

    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
        if (on_done_) on_done_(std::move(error));
    }

 private:
    std::function<void(webrtc::RTCError)> on_done_;
};

class SetLocalDescObserverImpl : public webrtc::SetLocalDescriptionObserverInterface {
 public:
    explicit SetLocalDescObserverImpl(std::function<void(webrtc::RTCError)> on_done)
        : on_done_(std::move(on_done)) {}

    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
        if (on_done_) on_done_(std::move(error));
    }

 private:
    std::function<void(webrtc::RTCError)> on_done_;
};

class SetLocalDescObserverLegacyImpl : public webrtc::SetSessionDescriptionObserver {
 public:
    SetLocalDescObserverLegacyImpl(std::function<void()> on_ok,
                                   std::function<void(webrtc::RTCError)> on_fail)
        : on_ok_(std::move(on_ok)), on_fail_(std::move(on_fail)) {}

    void OnSuccess() override {
        if (on_ok_) on_ok_();
    }

    void OnFailure(webrtc::RTCError error) override {
        if (on_fail_) on_fail_(std::move(error));
    }

 private:
    std::function<void()> on_ok_;
    std::function<void(webrtc::RTCError)> on_fail_;
};

class CreateSdpObserverImpl : public webrtc::CreateSessionDescriptionObserver {
 public:
    using OkFn = std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)>;
    using FailFn = std::function<void(webrtc::RTCError)>;

    CreateSdpObserverImpl(OkFn on_ok, FailFn on_fail)
        : on_ok_(std::move(on_ok)), on_fail_(std::move(on_fail)) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        std::unique_ptr<webrtc::SessionDescriptionInterface> owned(desc);
        if (on_ok_) on_ok_(std::move(owned));
    }

    void OnFailure(webrtc::RTCError error) override {
        if (on_fail_) on_fail_(std::move(error));
    }

 private:
    OkFn on_ok_;
    FailFn on_fail_;
};

}  // namespace

webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
MakeSetRemoteDescObserver(std::function<void(webrtc::RTCError)> on_done) {
    return webrtc::make_ref_counted<SetRemoteDescObserverImpl>(std::move(on_done));
}

webrtc::scoped_refptr<webrtc::SetLocalDescriptionObserverInterface>
MakeSetLocalDescObserver(std::function<void(webrtc::RTCError)> on_done) {
    return webrtc::make_ref_counted<SetLocalDescObserverImpl>(std::move(on_done));
}

webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver>
MakeSetLocalDescObserverLegacy(std::function<void()> on_ok,
                               std::function<void(webrtc::RTCError)> on_fail) {
    return webrtc::make_ref_counted<SetLocalDescObserverLegacyImpl>(std::move(on_ok),
                                                                    std::move(on_fail));
}

webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver>
MakeCreateSdpObserver(
    std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)> on_ok,
    std::function<void(webrtc::RTCError)> on_fail) {
    return webrtc::make_ref_counted<CreateSdpObserverImpl>(std::move(on_ok), std::move(on_fail));
}

}  // namespace rflow::core::rtc
