#ifndef RFLOW_SIGNALING_CLIENT_H_
#define RFLOW_SIGNALING_CLIENT_H_

// service::impl::SignalingClient 是 core::signal::TcpClientSession 的高层适配器：
// - 用 SignalingClient 暴露 publisher 端语义化的 SetOnAnswer / SetOnIce / SetOnSubscriberJoin 等
//   callback API（保留与原有调用方兼容）
// - 内部把 SessionDelegate::OnSignalMessage(Message) 分发到上述高层 callback。
//
// 真正的 TCP / IO 复用 / 注册握手等，由 core::signal::TcpClientSession 实现，
// 同一份 transport 也被 client::impl::RtcStreamSession 直接使用，避免两边各维护一套。

#include "core/signal/protocol.h"
#include "core/signal/session.h"

#include <functional>
#include <memory>
#include <string>

namespace rflow::signal {
class TcpClientSession;
}  // namespace rflow::signal

namespace rflow::service::impl {

class SignalingClient final : public rflow::signal::SessionDelegate {
public:
    /// role: "publisher" or "subscriber"
    /// stream_id: stream id, default is livestream
    SignalingClient(const std::string& server_addr, const std::string& role,
                    const std::string& stream_id = "livestream");
    ~SignalingClient() override;

    SignalingClient(const SignalingClient&)            = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    bool Start();
    void Stop();

    void SendOffer(const std::string& sdp, const std::string& to_peer_id = "");
    void SendAnswer(const std::string& sdp, const std::string& to_peer_id = "");
    void SendIceCandidate(const std::string& mid, int mline_index, const std::string& candidate,
                          const std::string& to_peer_id = "");

    using OnAnswerCallback =
        std::function<void(const std::string& peer_id, const std::string& type, const std::string& sdp)>;
    void SetOnAnswer(OnAnswerCallback cb) { on_answer_ = std::move(cb); }

    using OnOfferCallback =
        std::function<void(const std::string& peer_id, const std::string& type, const std::string& sdp)>;
    void SetOnOffer(OnOfferCallback cb) { on_offer_ = std::move(cb); }

    using OnIceCallback = std::function<void(const std::string& peer_id, const std::string& mid, int mline_index,
                                             const std::string& candidate)>;
    void SetOnIce(OnIceCallback cb) { on_ice_ = std::move(cb); }

    using OnPeerEventCallback = std::function<void(const std::string& peer_id)>;
    void SetOnSubscriberJoin(OnPeerEventCallback cb) { on_subscriber_join_ = std::move(cb); }
    void SetOnSubscriberLeave(OnPeerEventCallback cb) { on_subscriber_leave_ = std::move(cb); }

    using OnErrorCallback = std::function<void(const std::string& msg)>;
    void SetOnError(OnErrorCallback cb) { on_error_ = std::move(cb); }

private:
    // SessionDelegate
    void OnSignalMessage(const rflow::signal::Message& msg) override;
    void OnSignalError(std::string_view error) override;

    bool RoleIsPublisher() const noexcept;

    std::string role_;
    std::string stream_id_;

    OnAnswerCallback    on_answer_;
    OnOfferCallback     on_offer_;
    OnIceCallback       on_ice_;
    OnPeerEventCallback on_subscriber_join_;
    OnPeerEventCallback on_subscriber_leave_;
    OnErrorCallback     on_error_;

    std::unique_ptr<rflow::signal::TcpClientSession> session_;
};

}  // namespace rflow::service::impl

#endif  // RFLOW_SIGNALING_CLIENT_H_
