#include "signaling_client.h"

#include "signaling_io_manager.h"

#include "common/internal/logger.h"
#include "rflow/librflow_common.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <netdb.h>

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

/// 与 BuildRegisterLine / 推流端 stream_id 规则对齐：空 device_id、异常 stream_index 会导致进默认房 livestream。
void NormalizeSubscriberRegistration(rflow::signal::RegisterRequest& reg) {
    if (reg.role != rflow::signal::PeerRole::kSubscriber) {
        return;
    }
    if (reg.device_id.empty()) {
        reg.device_id = RFLOW_DEFAULT_DEVICE_ID;
    }
    if (reg.stream_index < 0) {
        reg.stream_index = 0;
    }
}

/** 建立 TCP 连接；先尝试 IPv4 字面量，否则 getaddrinfo（域名 / IPv6 字面量等）。成功返回 fd，失败返回 -1。 */
int DialSignalingTcp(const std::string& host, uint16_t port, std::string* err) {
    sockaddr_in addr4{};
    if (::inet_pton(AF_INET, host.c_str(), &addr4.sin_addr) == 1) {
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            if (err) *err = std::string("socket: ") + std::strerror(errno);
            return -1;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) == 0) {
            return fd;
        }
        if (err) *err = std::string("connect: ") + std::strerror(errno);
        CloseFd(fd);
        return -1;
    }

    addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;
    const std::string port_str = std::to_string(static_cast<int>(port));
    addrinfo* res = nullptr;
    const int gai = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        if (err) *err = std::string("getaddrinfo: ") + ::gai_strerror(gai);
        return -1;
    }
    int out_fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        const int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0) {
            out_fd = fd;
            break;
        }
        CloseFd(fd);
    }
    ::freeaddrinfo(res);
    if (out_fd < 0 && err) {
        *err = "connect: all addresses failed for host " + host;
    }
    return out_fd;
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
    std::string dial_err;
    const int fd = DialSignalingTcp(host_, port_, &dial_err);
    if (fd < 0) {
        ReportError(dial_err.empty() ? std::string("connect failed") : dial_err);
        return false;
    }

    sock_fd_.store(fd, std::memory_order_release);

    rflow::signal::RegisterRequest reg = config_.registration;
    NormalizeSubscriberRegistration(reg);
    const std::string reg_line = BuildRegisterLine(reg);
    if (const char* v = std::getenv("RFLOW_VERBOSE_SIGNAL")) {
        if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y') {
            RFLOW_LOGI("[Signaling] register_json=%s", reg_line.c_str());
        }
    }
    if (!SendLine(reg_line)) {
        ReportError("send register failed");
        const int bad_fd = sock_fd_.exchange(-1, std::memory_order_acq_rel);
        CloseFd(bad_fd);
        return false;
    }

    RFLOW_LOGI("[Signaling] registered role=%s device_id=%s stream_index=%d "
               "(信令房间须与推流 stream_id 一致；疑问题时设 RFLOW_VERBOSE_SIGNAL=1 看 register_json)",
               rflow::signal::ToString(reg.role),
               reg.device_id.c_str(),
               static_cast<int>(reg.stream_index));
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
