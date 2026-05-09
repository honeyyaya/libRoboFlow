#include "signaling/signaling_client.h"

#include "common/base/trace_switches.h"
#include "common/public/logger_api.h"
#include "core/signal/protocol.h"
#include "core/signal/tcp_session.h"

#include <chrono>
#include <utility>

namespace rflow::service::impl {

namespace {

bool SignalingTimingTraceEnabled() {
    static const bool enabled =
        rflow::common::base::TraceFlagEnabled("RFLOW_SIGNALING_TIMING_TRACE");
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
    RFLOW_LOGI("[SIG_TIMING] t_us=%lld %s",
               static_cast<long long>(NowUs()), msg.c_str());
}

rflow::signal::SessionConfig MakeSessionConfig(const std::string& server_addr,
                                               const std::string& role,
                                               const std::string& stream_id) {
    rflow::signal::SessionConfig cfg;
    cfg.server_addr = server_addr;
    cfg.registration.role        = rflow::signal::PeerRoleFromString(role);
    cfg.registration.stream_id   = stream_id.empty() ? std::string("livestream") : stream_id;
    cfg.remember_last_remote_peer = true;
    return cfg;
}

}  // namespace

SignalingClient::SignalingClient(const std::string& server_addr, const std::string& role,
                                 const std::string& stream_id)
    : role_(role), stream_id_(stream_id.empty() ? "livestream" : stream_id) {
    session_ = std::make_unique<rflow::signal::TcpClientSession>(
        MakeSessionConfig(server_addr, role_, stream_id_));
    session_->SetDelegate(this);
}

SignalingClient::~SignalingClient() {
    Stop();
}

bool SignalingClient::Start() {
    return session_ ? session_->Start() : false;
}

void SignalingClient::Stop() {
    if (session_) {
        session_->SetDelegate(nullptr);
        session_->Stop();
    }
}

bool SignalingClient::RoleIsPublisher() const noexcept {
    return role_ == "publisher";
}

void SignalingClient::SendOffer(const std::string& sdp, const std::string& to_peer_id) {
    if (!session_) return;

    rflow::signal::Message msg;
    msg.type = rflow::signal::MessageType::kOffer;
    msg.to   = to_peer_id;
    msg.sdp  = sdp;

    if (RoleIsPublisher() && msg.to.empty()) {
        // publisher 必须明确目标 subscriber；TcpClientSession::Send 会回退到
        // last_remote_peer_id_，但发布端通常不应让缺省路径决定路由。
        RFLOW_LOGW("[Signaling] ignoring offer without subscriber target");
        return;
    }
    TraceSig("send begin type=offer sdp_len=" + std::to_string(sdp.size()));
    session_->Send(msg);
    TraceSig("send done type=offer sdp_len=" + std::to_string(sdp.size()));
}

void SignalingClient::SendAnswer(const std::string& sdp, const std::string& to_peer_id) {
    if (!session_) return;
    rflow::signal::Message msg;
    msg.type = rflow::signal::MessageType::kAnswer;
    msg.to   = to_peer_id;
    msg.sdp  = sdp;
    TraceSig("send begin type=answer sdp_len=" + std::to_string(sdp.size()));
    session_->Send(msg);
    TraceSig("send done type=answer sdp_len=" + std::to_string(sdp.size()));
}

void SignalingClient::SendIceCandidate(const std::string& mid, int mline_index,
                                       const std::string& candidate,
                                       const std::string& to_peer_id) {
    if (!session_) return;
    rflow::signal::Message msg;
    msg.type        = rflow::signal::MessageType::kIce;
    msg.to          = to_peer_id;
    msg.mid         = mid;
    msg.mline_index = mline_index;
    msg.candidate   = candidate;
    TraceSig("send begin type=ice mid=" + mid + " cand_len=" + std::to_string(candidate.size()));
    session_->Send(msg);
    TraceSig("send done type=ice mid=" + mid);
}

void SignalingClient::OnSignalMessage(const rflow::signal::Message& msg) {
    TraceSig("dispatch type=" + std::string(rflow::signal::ToString(msg.type)) +
             " from=" + (msg.from.empty() ? std::string("-") : msg.from));

    switch (msg.type) {
        case rflow::signal::MessageType::kSubscriberJoin:
            TraceSig("callback subscriber_join");
            if (on_subscriber_join_) on_subscriber_join_(msg.from);
            return;
        case rflow::signal::MessageType::kSubscriberLeave:
            TraceSig("callback subscriber_leave");
            if (on_subscriber_leave_) on_subscriber_leave_(msg.from);
            return;
        case rflow::signal::MessageType::kAnswer:
            TraceSig("callback answer sdp_len=" + std::to_string(msg.sdp.size()));
            if (on_answer_) on_answer_(msg.from, "answer", msg.sdp);
            return;
        case rflow::signal::MessageType::kOffer:
            TraceSig("callback offer sdp_len=" + std::to_string(msg.sdp.size()));
            if (on_offer_) on_offer_(msg.from, "offer", msg.sdp);
            return;
        case rflow::signal::MessageType::kIce:
            TraceSig("callback ice mid=" + msg.mid + " cand_len=" + std::to_string(msg.candidate.size()));
            if (on_ice_ && !msg.candidate.empty()) {
                on_ice_(msg.from, msg.mid, msg.mline_index, msg.candidate);
            }
            return;
        default:
            return;
    }
}

void SignalingClient::OnSignalError(std::string_view error) {
    if (on_error_) on_error_(std::string(error));
}

}  // namespace rflow::service::impl
