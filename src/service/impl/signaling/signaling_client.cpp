#include "signaling/signaling_client.h"

#include "signaling/signaling_io_manager.h"

#include "core/signal/protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <cstdlib>

namespace webrtc_demo {
namespace {

void CloseFd(int fd) {
    if (fd < 0) {
        return;
    }
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

bool SignalingTimingTraceEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("WEBRTC_DEMO_SIGNALING_TIMING_TRACE");
        return v && v[0] == '1';
    }();
    return enabled;
}

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void TraceSig(const std::string& msg) {
    if (!SignalingTimingTraceEnabled()) {
        return;
    }
    std::cout << "[SIG_TIMING] t_us=" << NowUs() << " " << msg << std::endl;
}

std::string ExtractTypeForTrace(const std::string& line) {
    return rflow::signal::ExtractJsonString(line, "type");
}

}  // namespace

SignalingClient::SignalingClient(const std::string& server_addr, const std::string& role,
                                const std::string& stream_id)
    : server_addr_(server_addr), role_(role) {
    rflow::signal::Endpoint endpoint;
    if (rflow::signal::ParseEndpoint(server_addr, &endpoint)) {
        host_ = std::move(endpoint.host);
        port_ = endpoint.port;
    }
    stream_id_ = stream_id.empty() ? "livestream" : stream_id;
}

SignalingClient::~SignalingClient() {
    Stop();
}

bool SignalingClient::Connect() {
    if (host_.empty() || port_ == 0) {
        ReportError("invalid signaling server address");
        return false;
    }

    std::cout << "[Signaling] Connecting to " << host_ << ":" << port_ << " (role=" << role_ << ")..."
              << std::endl;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ReportError(std::string("socket: ") + std::strerror(errno));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        ReportError("invalid host: " + host_);
        ::close(fd);
        return false;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ReportError(std::string("connect: ") + std::strerror(errno));
        ::close(fd);
        return false;
    }
    std::cout << "[Signaling] TCP connected" << std::endl;

    sock_fd_.store(fd, std::memory_order_release);

    rflow::signal::RegisterRequest req;
    req.role = rflow::signal::PeerRoleFromString(role_);
    req.stream_id = stream_id_;
    if (!SendLine(rflow::signal::BuildRegisterLine(req))) {
        ReportError("send register failed");
        CloseFd(sock_fd_.exchange(-1, std::memory_order_acq_rel));
        return false;
    }
    std::cout << "[Signaling] Registered (role=" << role_ << ")" << std::endl;
    return true;
}

bool SignalingClient::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return true;
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

bool SignalingClient::SendLine(std::string_view line) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    const int fd = sock_fd_.load(std::memory_order_acquire);
    if (fd < 0) {
        return false;
    }

    const std::string payload(line);
    const std::string msg = payload + "\n";
    const std::string type = ExtractTypeForTrace(payload);
    TraceSig("send begin type=" + type + " bytes=" + std::to_string(msg.size()));
    size_t off = 0;
    while (off < msg.size()) {
        const ssize_t sent = ::send(fd, msg.data() + off, msg.size() - off, MSG_NOSIGNAL);
        if (sent > 0) {
            off += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        ReportError(std::string("send signaling failed: ") + std::strerror(errno));
        return false;
    }
    TraceSig("send done type=" + type + " sent=" + std::to_string(off));
    return true;
}

std::string SignalingClient::ResolveTargetPeer(std::string_view to_peer_id) const {
    if (!to_peer_id.empty()) return to_peer_id;
    std::lock_guard<std::mutex> lock(peer_mutex_);
    return last_remote_peer_id_;
}

void SignalingClient::SendOffer(const std::string& sdp, const std::string& to_peer_id) {
    std::string target = ResolveTargetPeer(to_peer_id);
    if (role_ == "publisher" && target.empty()) {
        std::cerr << "[Signaling] Ignoring offer without subscriber target" << std::endl;
        return;
    }

    rflow::signal::Message msg;
    msg.type = rflow::signal::MessageType::kOffer;
    msg.to = std::move(target);
    msg.sdp = sdp;
    SendLine(rflow::signal::BuildMessageLine(msg));
}

void SignalingClient::SendAnswer(const std::string& sdp, const std::string& to_peer_id) {
    std::string target = ResolveTargetPeer(to_peer_id);

    rflow::signal::Message msg;
    msg.type = rflow::signal::MessageType::kAnswer;
    msg.to = std::move(target);
    msg.sdp = sdp;
    SendLine(rflow::signal::BuildMessageLine(msg));
}

void SignalingClient::SendIceCandidate(const std::string& mid, int mline_index,
                                       const std::string& candidate,
                                       const std::string& to_peer_id) {
    std::string target = ResolveTargetPeer(to_peer_id);

    rflow::signal::Message msg;
    msg.type = rflow::signal::MessageType::kIce;
    msg.to = std::move(target);
    msg.mid = mid;
    msg.mline_index = mline_index;
    msg.candidate = candidate;
    SendLine(rflow::signal::BuildMessageLine(msg));
}

void SignalingClient::ParseAndDispatch(const rflow::signal::Message& msg) {
    TraceSig("dispatch type=" + std::string(rflow::signal::ToString(msg.type)) +
             " from=" + (msg.from.empty() ? std::string("-") : msg.from));
    if (!msg.from.empty()) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        last_remote_peer_id_ = msg.from;
    }

    if (msg.type == rflow::signal::MessageType::kWelcome) {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        self_peer_id_ = msg.peer_id;
        return;
    }

    if (msg.type == rflow::signal::MessageType::kSubscriberJoin) {
        TraceSig("callback subscriber_join");
        if (on_subscriber_join_) on_subscriber_join_(msg.from);
        return;
    }
    if (msg.type == rflow::signal::MessageType::kSubscriberLeave) {
        TraceSig("callback subscriber_leave");
        if (on_subscriber_leave_) on_subscriber_leave_(msg.from);
        return;
    }

    if (msg.type == rflow::signal::MessageType::kAnswer) {
        TraceSig("callback answer sdp_len=" + std::to_string(msg.sdp.size()));
        if (on_answer_) on_answer_(msg.from, "answer", msg.sdp);
        return;
    }
    if (msg.type == rflow::signal::MessageType::kOffer) {
        TraceSig("callback offer sdp_len=" + std::to_string(msg.sdp.size()));
        if (on_offer_) on_offer_(msg.from, "offer", msg.sdp);
        return;
    }
    if (msg.type == rflow::signal::MessageType::kIce) {
        TraceSig("callback ice mid=" + msg.mid + " cand_len=" + std::to_string(msg.candidate.size()));
        if (on_ice_ && !msg.candidate.empty()) {
            on_ice_(msg.from, msg.mid, msg.mline_index, msg.candidate);
        }
    }
}

void SignalingClient::ReportError(std::string_view error) {
    if (on_error_) on_error_(std::string(error));
}

}  // namespace webrtc_demo
