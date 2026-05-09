/**
 * @file   tcp_session.h
 * @brief  POSIX TCP signaling session implementation for rflow::signal::Session.
 *
 * 单进程多 client/service 端共用一个 IO 线程（epoll/poll 复用），
 * 上层通过 SessionDelegate 接收消息，通过 Send(Message) 发送。
 *
 * client/service 两侧合一：原 src/client/impl/signaling/signaling_client.cpp 和
 * src/service/impl/signaling/signaling_client.cpp 公共部分都收敛到此文件。
 * service 侧的"高层 callback API"（SetOnAnswer/SetOnIce/...）由其自身适配器
 * （service::impl::SignalingClient）在 SessionDelegate 上转发，保持对外接口稳定。
 */

#ifndef __RFLOW_CORE_SIGNAL_TCP_SESSION_H__
#define __RFLOW_CORE_SIGNAL_TCP_SESSION_H__

#include "core/signal/session.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace rflow::core::signal {
template <typename SlotT, typename OwnerT>
class SharedSignalingIoManager;
}  // namespace rflow::core::signal

namespace rflow::signal {

struct TcpClientSessionSlot;

class TcpClientSession final : public Session {
public:
    explicit TcpClientSession(SessionConfig config);
    ~TcpClientSession() override;

    TcpClientSession(const TcpClientSession&)            = delete;
    TcpClientSession& operator=(const TcpClientSession&) = delete;

    bool Start() override;
    void Stop() override;
    bool Send(const Message& msg) override;
    bool IsRunning() const override;
    void SetDelegate(SessionDelegate* delegate) override;

private:
    template <typename SlotT, typename OwnerT>
    friend class ::rflow::core::signal::SharedSignalingIoManager;

    bool        Connect();
    bool        SendLine(std::string_view line);
    void        ReportError(std::string_view error);
    std::string ResolveTargetPeer(std::string_view explicit_peer) const;

    /// 由共享 IO 模板线程在 read 完整 \n 包后回调；线程安全。
    void ParseAndDispatch(const Message& msg);

    SessionConfig                            config_;
    std::string                              host_;
    uint16_t                                 port_{0};
    std::string                              self_peer_id_;
    std::string                              last_remote_peer_id_;
    std::atomic<int>                         sock_fd_{-1};
    std::atomic<bool>                        running_{false};
    std::atomic<SessionDelegate*>            delegate_{nullptr};
    std::shared_ptr<TcpClientSessionSlot>    session_slot_;
    mutable std::mutex                       peer_mutex_;
    mutable std::mutex                       send_mutex_;
};

}  // namespace rflow::signal

#endif  // __RFLOW_CORE_SIGNAL_TCP_SESSION_H__
