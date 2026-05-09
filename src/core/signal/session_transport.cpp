#include "core/signal/session_transport.h"

#include <cerrno>
#include <cstring>

#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rflow::core::signal::transport {

void CloseFd(int fd) {
    if (fd < 0) return;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

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
    hints.ai_family = AF_UNSPEC;
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
        if (fd < 0) continue;
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

bool SendLine(int fd, std::string_view line, std::mutex& send_mutex, std::string* out_error) {
    std::lock_guard<std::mutex> lock(send_mutex);
    if (fd < 0) {
        if (out_error) *out_error = "socket not connected";
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
        if (sent < 0 && errno == EINTR) continue;
        if (out_error) *out_error = std::string("send signaling failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool SendRegisterRequestLine(int fd,
                             const rflow::signal::RegisterRequest& req,
                             std::mutex& send_mutex,
                             std::string* out_error) {
    return SendLine(fd, rflow::signal::BuildRegisterLine(req), send_mutex, out_error);
}

bool DialAndStoreFd(const std::string& host,
                    uint16_t port,
                    std::atomic<int>& sock_fd,
                    std::string* out_error) {
    const int fd = DialSignalingTcp(host, port, out_error);
    if (fd < 0) {
        return false;
    }
    sock_fd.store(fd, std::memory_order_release);
    return true;
}

bool RegisterOrCloseStoredFd(std::atomic<int>& sock_fd,
                             std::mutex& send_mutex,
                             const rflow::signal::RegisterRequest& req,
                             std::string* out_error) {
    if (SendRegisterRequestLine(sock_fd.load(std::memory_order_acquire),
                                req, send_mutex, out_error)) {
        return true;
    }
    CloseFd(sock_fd.exchange(-1, std::memory_order_acq_rel));
    return false;
}

std::string ResolveTargetPeer(std::string_view explicit_peer,
                              bool remember_last_remote_peer,
                              const std::string& last_remote_peer_id,
                              std::mutex& peer_mutex) {
    if (!explicit_peer.empty()) {
        return std::string(explicit_peer);
    }
    if (!remember_last_remote_peer) {
        return {};
    }

    std::lock_guard<std::mutex> lock(peer_mutex);
    return last_remote_peer_id;
}

bool ApplyInboundPeerMetadata(const rflow::signal::Message& msg,
                              bool remember_last_remote_peer,
                              std::string& last_remote_peer_id,
                              std::string& self_peer_id,
                              std::mutex& peer_mutex) {
    if (!msg.from.empty() && remember_last_remote_peer) {
        std::lock_guard<std::mutex> lock(peer_mutex);
        last_remote_peer_id = msg.from;
    }

    if (msg.type == rflow::signal::MessageType::kWelcome) {
        std::lock_guard<std::mutex> lock(peer_mutex);
        self_peer_id = msg.peer_id;
        return true;
    }
    return false;
}

}  // namespace rflow::core::signal::transport
