#include "media/push_streamer_internals.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <future>
#include <sstream>
#include <thread>
#include <utility>

#include "api/stats/rtcstats_objects.h"
#include "rtc_base/thread.h"

#include "base/trace_switches.h"
#include "public/log_tagged.h"

namespace rflow::service::impl::detail::push {

bool SignalingTimingTraceEnabled() {
    static const bool enabled =
        rflow::common::util::TraceFlagEnabled("RFLOW_SIGNALING_TIMING_TRACE");
    return enabled;
}

int64_t SignalingNowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void TraceSigTiming(const std::string& msg) {
    if (!SignalingTimingTraceEnabled()) {
        return;
    }
    RFLOW_LOG_TAG_I("SIG_TIMING", "[push] t_us=%lld %s",
                    static_cast<long long>(SignalingNowUs()),
                    msg.c_str());
}

bool LatencyTraceEnabled() {
    static const bool enabled =
        rflow::common::util::TraceFlagEnabled("WEBRTC_LATENCY_TRACE");
    return enabled;
}

bool ClosePeerConnectionWithDeadline(
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const char* log_tag,
    int timeout_sec) {
    if (!pc) {
        return true;
    }
    std::packaged_task<void()> task([pc]() { pc->Close(); });
    std::future<void> done = task.get_future();
    std::thread worker(std::move(task));
    if (done.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready) {
        RFLOW_LOG_TAG_W("PushStreamer", "%s PeerConnection::Close exceeded %ds; continuing shutdown", log_tag,
                        timeout_sec);
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}

void StopWebrtcThreadWithDeadline(webrtc::Thread* thread, int timeout_sec) {
    if (!thread) {
        return;
    }
    std::packaged_task<void()> task([thread]() { thread->Stop(); });
    std::future<void> done = task.get_future();
    std::thread worker(std::move(task));
    if (done.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready) {
        RFLOW_LOG_TAG_W("PushStreamer", "webrtc signaling Thread::Stop exceeded %ds; continuing shutdown",
                        timeout_sec);
        worker.detach();
        return;
    }
    worker.join();
}

webrtc::Priority ParseVideoNetworkPriority(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (unsigned char c : s) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    if (lower == "very_low" || lower == "verylow") {
        return webrtc::Priority::kVeryLow;
    }
    if (lower == "low") {
        return webrtc::Priority::kLow;
    }
    if (lower == "medium") {
        return webrtc::Priority::kMedium;
    }
    if (lower == "high") {
        return webrtc::Priority::kHigh;
    }
    return webrtc::Priority::kHigh;
}

webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfiguration(
    const PushStreamerConfig& config) {
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    webrtc::PeerConnectionInterface::IceServer stun;
    stun.urls.push_back(config.common.stun_server);
    rtc_config.servers.push_back(stun);
    if (!config.common.turn_server.empty()) {
        webrtc::PeerConnectionInterface::IceServer turn;
        turn.urls.push_back(config.common.turn_server);
        turn.username = config.common.turn_username;
        turn.password = config.common.turn_password;
        rtc_config.servers.push_back(turn);
    }
    rtc_config.disable_ipv6_on_wifi = true;
    rtc_config.max_ipv6_networks = 0;
    rtc_config.disable_link_local_networks = true;
    rtc_config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    rtc_config.tcp_candidate_policy = webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
    rtc_config.set_dscp(true);
    rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    rtc_config.prioritize_most_likely_ice_candidate_pairs = config.common.ice_prioritize_likely_pairs;
    return rtc_config;
}

webrtc::PeerConnectionInterface::RTCOfferAnswerOptions MakeOfferOptions() {
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions o;
    o.num_simulcast_layers = 1;
    o.offer_to_receive_audio = 0;
    return o;
}

void PrintOutboundVideoStats(
    const std::string& pc_tag,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report) {
        return;
    }
    const std::vector<const webrtc::RTCOutboundRtpStreamStats*> outbound =
        report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
    for (const webrtc::RTCOutboundRtpStreamStats* s : outbound) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        std::ostringstream line;
        line << "pc=" << pc_tag << " id=" << s->id();
        if (s->ssrc.has_value()) {
            line << " ssrc=" << *s->ssrc;
        }
        if (s->frames_encoded.has_value()) {
            line << " frames_encoded=" << *s->frames_encoded;
        }
        if (s->key_frames_encoded.has_value()) {
            line << " key_frames_encoded=" << *s->key_frames_encoded;
        }
        if (s->packets_sent.has_value()) {
            line << " packets_sent=" << *s->packets_sent;
        }
        if (s->bytes_sent.has_value()) {
            line << " bytes_sent=" << *s->bytes_sent;
        }
        if (s->retransmitted_packets_sent.has_value()) {
            line << " retrans_pkts=" << *s->retransmitted_packets_sent;
        }
        if (s->quality_limitation_reason.has_value()) {
            line << " ql_reason=" << *s->quality_limitation_reason;
        }
        RFLOW_LOG_TAG_I("OutboundVideoStats", "%s", line.str().c_str());
    }
}

}  // namespace rflow::service::impl::detail::push
