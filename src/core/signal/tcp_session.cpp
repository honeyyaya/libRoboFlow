#include "signal/tcp_session.h"

#include "base/logging.h"
#include "runtime/runtime_knobs.h"
#include "signal/session_transport.h"
#include "signal/shared_signaling_io_manager.h"
#include "rflow/librflow_common.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32)
#  error "tcp_session.cpp targets POSIX; replace the socket layer before building on Windows"
#endif

namespace rflow::signal {

struct TcpClientSessionSlot {
    std::atomic<TcpClientSession*> owner{nullptr};
    std::atomic<int>               fd{-1};
    std::atomic<bool>              active{true};
    std::string                    read_buffer;
    size_t                         parse_offset{0};
};

namespace {

/// 与 BuildRegisterLine / 推流端 stream_id 规则对齐：
/// subscriber 端默认填 livestream / 0，避免错挂 device_id 空房。
void NormalizeSubscriberRegistration(RegisterRequest& reg) {
    if (reg.role != PeerRole::kSubscriber) {
        return;
    }
    if (reg.device_id.empty()) {
        reg.device_id = RFLOW_DEFAULT_DEVICE_ID;
    }
    if (reg.stream_index < 0) {
        reg.stream_index = 0;
    }
}

class TcpClientIoHub {
public:
    static TcpClientIoHub& Instance() {
        static TcpClientIoHub g;
        return g;
    }

    bool RegisterSession(const std::shared_ptr<TcpClientSessionSlot>& slot) {
        return shared_io_.RegisterSession(slot);
    }

    void UnregisterSession(const std::shared_ptr<TcpClientSessionSlot>& slot) {
        shared_io_.UnregisterSession(slot);
    }

private:
    TcpClientIoHub() = default;
    ~TcpClientIoHub() = default;

    core::signal::SharedSignalingIoManager<TcpClientSessionSlot, TcpClientSession> shared_io_;
};

}  // namespace

TcpClientSession::TcpClientSession(SessionConfig config) : config_(std::move(config)) {
    Endpoint endpoint;
    if (ParseEndpoint(config_.server_addr, &endpoint)) {
        host_ = std::move(endpoint.host);
        port_ = endpoint.port;
    }
}

TcpClientSession::~TcpClientSession() {
    Stop();
}

bool TcpClientSession::Connect() {
    if (host_.empty() || port_ == 0) {
        ReportError("invalid signaling server address");
        return false;
    }

    RFLOW_CORE_LOGI("[Signaling] connect %s:%u role=%s",
               host_.c_str(), port_, ToString(config_.registration.role));
    std::string err;
    if (!core::signal::transport::DialAndStoreFd(host_, port_, sock_fd_, &err)) {
        ReportError(err.empty() ? std::string("connect failed") : err);
        return false;
    }

    RegisterRequest reg = config_.registration;
    NormalizeSubscriberRegistration(reg);
    if (rflow::core::runtime::ReadBool("RFLOW_VERBOSE_SIGNAL")) {
        RFLOW_CORE_LOGI("[Signaling] register_json=%s", BuildRegisterLine(reg).c_str());
    }

    if (!core::signal::transport::RegisterOrCloseStoredFd(sock_fd_, send_mutex_, reg, &err)) {
        ReportError(err.empty() ? std::string("send register failed") : err);
        return false;
    }

    RFLOW_CORE_LOGI("[Signaling] registered role=%s device_id=%s stream_index=%d "
               "(signaling room must match push stream_id; set RFLOW_VERBOSE_SIGNAL=1 to inspect register_json if unsure)",
               ToString(reg.role),
               reg.device_id.c_str(),
               static_cast<int>(reg.stream_index));
    return true;
}

bool TcpClientSession::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    if (!Connect()) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    std::string attach_error;
    if (!core::signal::transport::AttachSessionToIo<TcpClientSessionSlot>(
            this, sock_fd_, session_slot_, running_, TcpClientIoHub::Instance(), &attach_error)) {
        ReportError(attach_error);
        return false;
    }
    return true;
}

void TcpClientSession::Stop() {
    running_.store(false, std::memory_order_release);

    auto slot = std::move(session_slot_);
    if (slot) {
        TcpClientIoHub::Instance().UnregisterSession(slot);
        return;
    }

    core::signal::transport::CloseFd(sock_fd_.exchange(-1, std::memory_order_acq_rel));
}

bool TcpClientSession::Send(const Message& msg) {
    Message outbound = msg;
    if (outbound.to.empty()) {
        outbound.to = ResolveTargetPeer(outbound.to);
    }
    return SendLine(BuildMessageLine(outbound));
}

bool TcpClientSession::IsRunning() const {
    return running_.load(std::memory_order_acquire) &&
           sock_fd_.load(std::memory_order_acquire) >= 0;
}

void TcpClientSession::SetDelegate(SessionDelegate* delegate) {
    delegate_.store(delegate, std::memory_order_release);
}

bool TcpClientSession::SendLine(std::string_view line) {
    const int fd = sock_fd_.load(std::memory_order_acquire);
    std::string transport_error;
    if (!core::signal::transport::SendLine(fd, line, send_mutex_, &transport_error)) {
        if (!transport_error.empty()) {
            ReportError(transport_error);
        }
        return false;
    }
    return true;
}

void TcpClientSession::ParseAndDispatch(const Message& msg) {
    // 协议版本不匹配仅打一次警告，不切断会话。当前 v1↔v2 字段集合完全兼容，
    // 仅是把 "v" 暴露出来；未来引入破坏性扩展时可在此处升级为拒绝连接。
    if (msg.protocol_version != kSignalingProtocolVersion) {
        static std::atomic<bool> warned{false};
        bool expected = false;
        if (warned.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            RFLOW_CORE_LOGW(
                "[signal] protocol version mismatch: peer=%d local=%d (still compatible, "
                "warn once per process)",
                msg.protocol_version, kSignalingProtocolVersion);
        }
    }

    if (core::signal::transport::ApplyInboundPeerMetadata(
            msg, config_.remember_last_remote_peer, last_remote_peer_id_, self_peer_id_, peer_mutex_)) {
        return;
    }

    if (auto* delegate = delegate_.load(std::memory_order_acquire)) {
        delegate->OnSignalMessage(msg);
    }
}

void TcpClientSession::ReportError(std::string_view error) {
    if (auto* delegate = delegate_.load(std::memory_order_acquire)) {
        delegate->OnSignalError(error);
    }
}

std::string TcpClientSession::ResolveTargetPeer(std::string_view explicit_peer) const {
    return core::signal::transport::ResolveTargetPeer(explicit_peer,
                                                     config_.remember_last_remote_peer,
                                                     last_remote_peer_id_,
                                                     peer_mutex_);
}

}  // namespace rflow::signal
