#ifndef WEBRTC_DEMO_SIGNALING_CLIENT_H_
#define WEBRTC_DEMO_SIGNALING_CLIENT_H_

#include "core/signal/protocol.h"
#include "signaling/signaling_io_manager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace rflow::service::impl {

/// P2P signaling client backed by a shared IO thread for multiple service sessions.
/// Address format: "127.0.0.1:8765" or "ws://127.0.0.1:8765" (ws:// is ignored)
class SignalingClient {
public:
    /// role: "publisher" or "subscriber"
    /// stream_id: stream id, default is livestream
    explicit SignalingClient(const std::string& server_addr, const std::string& role,
                             const std::string& stream_id = "livestream");
    ~SignalingClient();

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
    friend class SignalingIoManager;

    bool Connect();
    void ParseAndDispatch(const rflow::signal::Message& msg);
    bool SendLine(std::string_view line);
    void ReportError(std::string_view error);
    std::string ResolveTargetPeer(std::string_view to_peer_id) const;

    std::string server_addr_;
    std::string host_;
    uint16_t    port_{0};
    std::string role_;
    std::string stream_id_;
    std::string self_peer_id_;
    std::string last_remote_peer_id_;

    OnAnswerCallback on_answer_;
    OnOfferCallback on_offer_;
    OnIceCallback on_ice_;
    OnPeerEventCallback on_subscriber_join_;
    OnPeerEventCallback on_subscriber_leave_;
    OnErrorCallback on_error_;

    std::atomic<int>   sock_fd_{-1};
    std::atomic<bool>  running_{false};
    std::shared_ptr<SignalingClientSessionSlot> session_slot_;
    mutable std::mutex peer_mutex_;
    mutable std::mutex send_mutex_;
};

}  // namespace rflow::service::impl

#endif  // WEBRTC_DEMO_SIGNALING_CLIENT_H_
