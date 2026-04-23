#include "signaling_client.h"

#include "common/internal/logger.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// NOTE: POSIX socket implementation for Android/Linux.
// TODO: Move transport under a cross-platform core/net layer before enabling Windows.
#if defined(_WIN32)
#  error "signaling_client.cpp targets POSIX; replace the socket layer before building on Windows"
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rflow::client::impl {

using rflow::signal::BuildMessageLine;
using rflow::signal::BuildRegisterLine;
using rflow::signal::Endpoint;
using rflow::signal::Message;
using rflow::signal::MessageType;
using rflow::signal::ParseEndpoint;
using rflow::signal::ParseMessage;
using rflow::signal::SessionConfig;

struct SignalingClientSessionSlot {
    std::atomic<SignalingClient*> owner{nullptr};
    std::atomic<int>              fd{-1};
    std::atomic<bool>             active{true};
    std::string                   read_buffer;
};

namespace {

void CloseFd(int fd) {
    if (fd < 0) return;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

bool SetNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

class SignalingIoManager {
 public:
    static SignalingIoManager& Instance() {
        static SignalingIoManager g;
        return g;
    }

    bool RegisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
        if (!slot) {
            return false;
        }
        if (!EnsureStarted()) {
            return false;
        }

        const int fd = slot->fd.load(std::memory_order_acquire);
        if (fd < 0) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            sessions_[fd] = slot;
        }
        Wake();
        return true;
    }

    void UnregisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
        if (!slot) {
            return;
        }

        SignalingClient* owner = slot->owner.exchange(nullptr, std::memory_order_acq_rel);
        slot->active.store(false, std::memory_order_release);

        const int fd = slot->fd.exchange(-1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = sessions_.find(fd);
            if (it != sessions_.end() && it->second == slot) {
                sessions_.erase(it);
            }
        }

        if (owner) {
            owner->sock_fd_.store(-1, std::memory_order_release);
        }
        CloseFd(fd);
        Wake();
    }

 private:
    SignalingIoManager() = default;

    ~SignalingIoManager() {
        running_.store(false, std::memory_order_release);
        Wake();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }

        const int wake_read = wake_read_fd_.exchange(-1, std::memory_order_acq_rel);
        const int wake_write = wake_write_fd_.exchange(-1, std::memory_order_acq_rel);
        CloseFd(wake_read);
        CloseFd(wake_write);
    }

    bool EnsureStarted() {
        std::lock_guard<std::mutex> lock(start_mu_);
        if (started_) {
            return true;
        }

        int wake_pipe[2]{-1, -1};
        if (::pipe(wake_pipe) != 0) {
            return false;
        }
        if (!SetNonBlocking(wake_pipe[0]) || !SetNonBlocking(wake_pipe[1])) {
            CloseFd(wake_pipe[0]);
            CloseFd(wake_pipe[1]);
            return false;
        }

        wake_read_fd_.store(wake_pipe[0], std::memory_order_release);
        wake_write_fd_.store(wake_pipe[1], std::memory_order_release);
        running_.store(true, std::memory_order_release);
        io_thread_ = std::thread(&SignalingIoManager::Run, this);
        started_ = true;
        return true;
    }

    void Wake() {
        const int wake_fd = wake_write_fd_.load(std::memory_order_acquire);
        if (wake_fd < 0) {
            return;
        }
        const uint8_t byte = 0;
        const ssize_t ignored = ::write(wake_fd, &byte, sizeof(byte));
        (void)ignored;
    }

    void DrainWakePipe() {
        const int wake_fd = wake_read_fd_.load(std::memory_order_acquire);
        if (wake_fd < 0) {
            return;
        }

        char buf[64];
        while (true) {
            const ssize_t n = ::read(wake_fd, buf, sizeof(buf));
            if (n > 0) {
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }

    std::vector<std::shared_ptr<SignalingClientSessionSlot>> SnapshotSessions() {
        std::vector<std::shared_ptr<SignalingClientSessionSlot>> out;
        std::lock_guard<std::mutex> lock(mu_);
        out.reserve(sessions_.size());
        for (const auto& [fd, slot] : sessions_) {
            (void)fd;
            if (!slot) {
                continue;
            }
            if (!slot->active.load(std::memory_order_acquire)) {
                continue;
            }
            if (slot->fd.load(std::memory_order_acquire) < 0) {
                continue;
            }
            out.push_back(slot);
        }
        return out;
    }

    void DispatchLine(const std::shared_ptr<SignalingClientSessionSlot>& slot,
                      std::string_view line) {
        Message msg;
        if (!ParseMessage(line, &msg)) {
            return;
        }

        SignalingClient* owner = slot->owner.load(std::memory_order_acquire);
        if (!owner) {
            return;
        }
        owner->DispatchMessage(msg);
    }

    void CloseSession(const std::shared_ptr<SignalingClientSessionSlot>& slot,
                      std::string_view                                   error) {
        if (!slot) {
            return;
        }

        SignalingClient* owner = slot->owner.exchange(nullptr, std::memory_order_acq_rel);
        slot->active.store(false, std::memory_order_release);

        const int fd = slot->fd.exchange(-1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = sessions_.find(fd);
            if (it != sessions_.end() && it->second == slot) {
                sessions_.erase(it);
            }
        }

        CloseFd(fd);
        if (!owner) {
            return;
        }

        owner->sock_fd_.store(-1, std::memory_order_release);
        owner->running_.store(false, std::memory_order_release);
        if (owner->session_slot_ == slot) {
            owner->session_slot_.reset();
        }
        owner->ReportError(error);
    }

    void HandleReadable(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
        const int fd = slot->fd.load(std::memory_order_acquire);
        if (fd < 0 || !slot->active.load(std::memory_order_acquire)) {
            return;
        }

        char tmp[65536];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp) - 1, 0);
        if (n > 0) {
            tmp[n] = '\0';
            slot->read_buffer.append(tmp, static_cast<size_t>(n));
            size_t pos = 0;
            while ((pos = slot->read_buffer.find('\n')) != std::string::npos) {
                std::string line = slot->read_buffer.substr(0, pos);
                slot->read_buffer.erase(0, pos + 1);
                DispatchLine(slot, line);
            }
            return;
        }

        if (n == 0) {
            CloseSession(slot, "signaling connection closed");
            return;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        CloseSession(slot, std::string("recv signaling failed: ") + std::strerror(errno));
    }

    void Run() {
        while (running_.load(std::memory_order_acquire)) {
            auto sessions = SnapshotSessions();

            std::vector<pollfd> poll_fds;
            poll_fds.reserve(sessions.size() + 1);
            poll_fds.push_back(
                pollfd{wake_read_fd_.load(std::memory_order_acquire), static_cast<short>(POLLIN), 0});
            for (const auto& slot : sessions) {
                poll_fds.push_back(
                    pollfd{slot->fd.load(std::memory_order_acquire),
                           static_cast<short>(POLLIN | POLLERR | POLLHUP),
                           0});
            }

            const int ready = ::poll(poll_fds.data(),
                                     static_cast<nfds_t>(poll_fds.size()),
                                     -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                continue;
            }
            if (ready == 0) {
                continue;
            }

            if (poll_fds[0].revents & POLLIN) {
                DrainWakePipe();
            }

            for (size_t i = 0; i < sessions.size(); ++i) {
                const short events = poll_fds[i + 1].revents;
                if (events == 0) {
                    continue;
                }
                if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                    CloseSession(sessions[i], "signaling connection closed");
                    continue;
                }
                if (events & POLLIN) {
                    HandleReadable(sessions[i]);
                }
            }
        }
    }

    std::atomic<bool> running_{false};
    std::atomic<int>  wake_read_fd_{-1};
    std::atomic<int>  wake_write_fd_{-1};
    std::mutex        start_mu_;
    bool              started_{false};
    std::thread       io_thread_;

    std::mutex mu_;
    std::unordered_map<int, std::shared_ptr<SignalingClientSessionSlot>> sessions_;
};

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
