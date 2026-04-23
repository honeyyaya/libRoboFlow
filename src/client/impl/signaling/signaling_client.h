/**
 * @file   signaling_client.h
 * @brief  POSIX TCP signaling transport implementation for rflow::signal::Session
 *         backed by a shared IO thread for multiple client sessions.
 */

#ifndef __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__
#define __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__

#include "core/signal/session.h"
#include "signaling_io_manager.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace rflow::client::impl {

class SignalingClient final : public rflow::signal::Session {
 public:
    explicit SignalingClient(rflow::signal::SessionConfig config);
    ~SignalingClient() override;

    SignalingClient(const SignalingClient&)            = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    bool Start() override;
    void Stop() override;
    bool Send(const rflow::signal::Message& msg) override;
    bool IsRunning() const override;
    void SetDelegate(rflow::signal::SessionDelegate* delegate) override;

 private:
    friend class SignalingIoManager;

    bool Connect();
    void DispatchMessage(const rflow::signal::Message& msg);
    bool SendLine(std::string_view line);
    void ReportError(std::string_view error);
    std::string ResolveTargetPeer(std::string_view explicit_peer) const;

    rflow::signal::SessionConfig config_;
    std::string                  host_;
    uint16_t                     port_{0};
    std::string                  self_peer_id_;
    std::string                  last_remote_peer_id_;

    std::atomic<int>                         sock_fd_{-1};
    std::atomic<bool>                        running_{false};
    std::atomic<rflow::signal::SessionDelegate*> delegate_{nullptr};
    std::shared_ptr<SignalingClientSessionSlot> session_slot_;
    mutable std::mutex                       peer_mutex_;
    mutable std::mutex                       send_mutex_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__
