#ifndef __RFLOW_CORE_RTC_SESSION_SDP_OBSERVERS_H__
#define __RFLOW_CORE_RTC_SESSION_SDP_OBSERVERS_H__

// 通用 webrtc SDP observer 工厂：避免在 client/rtc_stream_session、
// service/push_streamer、service/pull_subscriber 中各自重复定义 SetRemote /
// SetLocal / CreateSdp 的 wrapper class。
//
// 仅在开启 webrtc 实现的目标里编译（依赖 libwebrtc 头文件）。

#include <functional>
#include <memory>

#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"

namespace rflow::core::rtc {

// 新版接口：error.ok() 表示成功。
webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
MakeSetRemoteDescObserver(std::function<void(webrtc::RTCError)> on_done);

webrtc::scoped_refptr<webrtc::SetLocalDescriptionObserverInterface>
MakeSetLocalDescObserver(std::function<void(webrtc::RTCError)> on_done);

// 旧版双回调接口（OnSuccess() 无错误，OnFailure(RTCError)）；某些代码路径仍在用。
webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver>
MakeSetLocalDescObserverLegacy(std::function<void()> on_ok,
                               std::function<void(webrtc::RTCError)> on_fail);

// CreateOffer / CreateAnswer 共用观察者。成功回调会以 unique_ptr 形式接管 SDP。
webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver>
MakeCreateSdpObserver(
    std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)> on_ok,
    std::function<void(webrtc::RTCError)> on_fail);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_SESSION_SDP_OBSERVERS_H__
