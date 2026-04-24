#include "signaling/signaling_io_manager.h"

#include "signaling/signaling_client.h"

#include "core/signal/protocol.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rflow::service::impl {

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

}  // namespace

SignalingIoManager& SignalingIoManager::Instance() {
    static SignalingIoManager g;
    return g;
}

bool SignalingIoManager::RegisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
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

void SignalingIoManager::UnregisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
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

SignalingIoManager::~SignalingIoManager() {
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

bool SignalingIoManager::EnsureStarted() {
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

void SignalingIoManager::Wake() {
    const int wake_fd = wake_write_fd_.load(std::memory_order_acquire);
    if (wake_fd < 0) {
        return;
    }
    const uint8_t byte = 0;
    const ssize_t ignored = ::write(wake_fd, &byte, sizeof(byte));
    (void)ignored;
}

void SignalingIoManager::DrainWakePipe() {
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

std::vector<std::shared_ptr<SignalingClientSessionSlot>> SignalingIoManager::SnapshotSessions() {
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

void SignalingIoManager::DispatchLine(const std::shared_ptr<SignalingClientSessionSlot>& slot,
                                      std::string_view                                   line) {
    rflow::signal::Message msg;
    if (!rflow::signal::ParseMessage(line, &msg)) {
        return;
    }

    SignalingClient* owner = slot->owner.load(std::memory_order_acquire);
    if (!owner) {
        return;
    }
    owner->ParseAndDispatch(msg);
}

void SignalingIoManager::CloseSession(const std::shared_ptr<SignalingClientSessionSlot>& slot,
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

void SignalingIoManager::HandleReadable(const std::shared_ptr<SignalingClientSessionSlot>& slot) {
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

void SignalingIoManager::Run() {
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

}  // namespace rflow::service::impl
