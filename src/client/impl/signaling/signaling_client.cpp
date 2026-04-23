#include "signaling_client.h"

#include "signaling_io_manager.h"

#include "common/internal/logger.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

// NOTE: POSIX socket implementation for Android/Linux.
// TODO: Move transport under a cross-platform core/net layer before enabling Windows.
#if defined(_WIN32)
#  error "signaling_client.cpp targets POSIX; replace the socket layer before building on Windows"
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rflow::client::impl {

using rflow::signal::BuildMessageLine;
using rflow::signal::BuildRegisterLine;
using rflow::signal::Endpoint;
using rflow::signal::Message;
using rflow::signal::MessageType;
using rflow::signal::ParseEndpoint;
using rflow::signal::SessionConfig;

namespace {

void CloseFd(int fd) {
    if (fd < 0) return;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

}  // namespace

SignalingClient::SignalingClient(SessionConfig config)
    : config_(std::move(config)) {
    Endpoint endpoint;
    if (ParseEndpoint(config_.server_addr, &endpoint)) {
        host_ = std::move(endpoint.host);
        port_ = endpoint.port;
    }
}

SignalingClient::~SignalingClient() {
    Stop();
}

bool SignalingClient::Connect() {
    if (host_.empty() || port_ == 0) {
        ReportError("invalid signaling server address");
        return false;
    }

    RFLOW_LOGI("[Signaling] connect %s:%u", host_.c_str(), port_);
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ReportError(std::string("socket: ") + std::strerror(errno));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        ReportError(std::string("invalid host: ") + host_);
        ::close(fd);
        return false;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ReportError(std::string("connect: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }

    sock_fd_.store(fd, std::memory_order_release);
    if (!SendLine(BuildRegisterLine(config_.registration))) {
        ReportError("send register failed");
        const int bad_fd = sock_fd_.exchange(-1, std::memory_order_acq_rel);
        CloseFd(bad_fd);
        return false;
    }

    RFLOW_LOGI("[Signaling] registered role=%s device_id=%s stream_index=%d",
               rflow::signal::ToString(config_.registration.role),
               config_.registration.device_id.c_str(),
               config_.registration.stream_index);
    return true;
}

bool SignalingClient::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    if (!Connect()) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    auto slot = std::make_shared<SignalingClientSessionSlot>();
    slot->owner.store(this, std::memory_order_release);
    slot->fd.store(sock_fd_.load(std::memory_order_acquire), std::memory_order_release);
    if (!SignalingIoManager::Instance().RegisterSession(slot)) {
        ReportError("register signaling session to shared io manager failed");
        CloseFd(sock_fd_.exchange(-1, std::memory_order_acq_rel));
        running_.store(false, std::memory_order_release);
        return false;
    }

    session_slot_ = std::move(slot);
    return true;
}

void SignalingClient::Stop() {
    running_.store(false, std::memory_order_release);

    auto slot = std::move(session_slot_);
    if (slot) {
        SignalingIoManager::Instance().UnregisterSession(slot);
        return;
    }

    CloseFd(sock_fd_.exchange(-1, std::memory_order_acq_rel));
}

bool SignalingClient::Send(const Message& msg) {
    if (msg.type == MessageType::kOffer) {
        ReportError("pull-side client does not send offer");
        return false;
    }

    Message outbound = msg;
    if (outbound.to.empty()) {
        outbound.to = ResolveTargetPeer(outbound.to);
    }
    return SendLine(BuildMessageLine(outbound));
}

bool SignalingClient::IsRunning() const {
    return running_.load(std::memory_order_acquire) &&
           sock_fd_.load(std::memory_order_acquire) >= 0;
}

void SignalingClient::SetDelegate(rflow::signal::SessionDelegate* delegate) {
    delegate_.store(delegate, std::memory_order_release);
}

bool SignalingClient::SendLine(std::string_view line) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    const int fd = sock_fd_.load(std::memory_order_acquire);
    if (fd < 0) {
        return false;
    }

    const std::string payload(line);
    const std::string with_newline = payload + "\n";
    size_t offset = 0;
    while (offset < with_newline.size()) {
        const ssize_t sent =
            ::send(fd, with_newline.data() + offset, with_newline.size() - offset, MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        ReportError(std::string("send signaling failed: ") + std::strerror(errno));
        return false;
    }
    return true;
}

void SignalingClient::DispatchMessage(const Message& msg) {
    if (!msg.from.empty() && config_.remember_last_remote_peer) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        last_remote_peer_id_ = msg.from;
    }

    if (msg.type == MessageType::kWelcome) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        self_peer_id_ = msg.peer_id;
    }

    if (auto* delegate = delegate_.load(std::memory_order_acquire)) {
        delegate->OnSignalMessage(msg);
    }
}

void SignalingClient::ReportError(std::string_view error) {
    if (auto* delegate = delegate_.load(std::memory_order_acquire)) {
        delegate->OnSignalError(error);
    }
}

std::string SignalingClient::ResolveTargetPeer(std::string_view explicit_peer) const {
    if (!explicit_peer.empty()) {
        return std::string(explicit_peer);
    }
    if (!config_.remember_last_remote_peer) {
        return {};
    }

    std::lock_guard<std::mutex> lock(peer_mutex_);
    return last_remote_peer_id_;
}

}  // namespace rflow::client::impl
