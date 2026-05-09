#ifndef __RFLOW_CORE_SIGNAL_SESSION_TRANSPORT_H__
#define __RFLOW_CORE_SIGNAL_SESSION_TRANSPORT_H__

#include "core/signal/protocol.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace rflow::core::signal::transport {

void CloseFd(int fd);

int DialSignalingTcp(const std::string& host, uint16_t port, std::string* err);

// 拨号成功后把 fd 写入 sock_fd（release 序），失败返回 false 并填充 *out_error。
bool DialAndStoreFd(const std::string& host,
                    uint16_t port,
                    std::atomic<int>& sock_fd,
                    std::string* out_error);

bool SendLine(int fd, std::string_view line, std::mutex& send_mutex, std::string* out_error);

bool SendRegisterRequestLine(int fd,
                             const rflow::signal::RegisterRequest& req,
                             std::mutex& send_mutex,
                             std::string* out_error);

// 用 sock_fd 当前持有的 fd 发送 register；失败时自动 CloseFd(fd) 并把 sock_fd 置 -1。
bool RegisterOrCloseStoredFd(std::atomic<int>& sock_fd,
                             std::mutex& send_mutex,
                             const rflow::signal::RegisterRequest& req,
                             std::string* out_error);

std::string ResolveTargetPeer(std::string_view explicit_peer,
                              bool remember_last_remote_peer,
                              const std::string& last_remote_peer_id,
                              std::mutex& peer_mutex);

bool ApplyInboundPeerMetadata(const rflow::signal::Message& msg,
                              bool remember_last_remote_peer,
                              std::string& last_remote_peer_id,
                              std::string& self_peer_id,
                              std::mutex& peer_mutex);

template <typename SlotT, typename OwnerT, typename IoManagerT>
bool AttachSessionToIo(OwnerT* owner,
                       std::atomic<int>& sock_fd,
                       std::shared_ptr<SlotT>& session_slot,
                       std::atomic<bool>& running,
                       IoManagerT& io_manager,
                       std::string* out_error) {
    auto slot = std::make_shared<SlotT>();
    slot->owner.store(owner, std::memory_order_release);
    slot->fd.store(sock_fd.load(std::memory_order_acquire), std::memory_order_release);
    if (!io_manager.RegisterSession(slot)) {
        if (out_error) *out_error = "register signaling session to shared io manager failed";
        CloseFd(sock_fd.exchange(-1, std::memory_order_acq_rel));
        running.store(false, std::memory_order_release);
        return false;
    }
    session_slot = std::move(slot);
    return true;
}

}  // namespace rflow::core::signal::transport

#endif  // __RFLOW_CORE_SIGNAL_SESSION_TRANSPORT_H__
