#include "media/push_streamer_internals.h"

#include "rtc/rtc_ice_config.h"

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
        rflow::common::util::TraceFlagEnabled("RFLOW_LATENCY_TRACE");
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
        RFLOW_LOG_TAG_W("PushStreamer", "RTC signaling Thread::Stop exceeded %ds; continuing shutdown",
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
    rflow::rtc::IceRtcServerConfig ice;
    ice.has_stun_server               = config.common.has_stun_server;
    ice.stun_server                   = config.common.stun_server;
    ice.has_turn_server               = config.common.has_turn_server;
    ice.turn_server                   = config.common.turn_server;
    ice.turn_username                 = config.common.turn_username;
    ice.turn_password                 = config.common.turn_password;
    ice.ice_prioritize_likely_pairs = config.common.ice_prioritize_likely_pairs;
    return rflow::rtc::BuildRtcConfiguration(ice);
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
        if (s->frame_width.has_value()) {
            line << " frame_w=" << *s->frame_width;
        }
        if (s->frame_height.has_value()) {
            line << " frame_h=" << *s->frame_height;
        }
        if (s->quality_limitation_reason.has_value()) {
            line << " ql_reason=" << *s->quality_limitation_reason;
        }
        if (s->quality_limitation_resolution_changes.has_value()) {
            line << " ql_res_changes=" << *s->quality_limitation_resolution_changes;
        }
        RFLOW_LOG_TAG_I("OutboundVideoStats", "%s", line.str().c_str());
    }
}

}  // namespace rflow::service::impl::detail::push
