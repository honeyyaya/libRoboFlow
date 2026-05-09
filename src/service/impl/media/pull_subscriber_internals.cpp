#include "media/pull_subscriber_internals.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "api/stats/rtcstats_objects.h"

#include "common/base/trace_switches.h"

namespace rflow::service::impl::detail::pull {

namespace {

// 与 api/video/video_timing.cc 中 TimingFrameInfo::ToString() 输出顺序一致。
std::vector<std::string> SplitCommaFields(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

bool ParseInt64Strict(const std::string& s, int64_t* out) {
    if (!out || s.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) {
        return false;
    }
    *out = static_cast<int64_t>(v);
    return true;
}

/// 由 TimingFrameInfo::ToString 的 15 段推导可解释的时延（ms）。
void PrintTimingFrameDerivedDeltas(const std::vector<std::string>& parts) {
    constexpr size_t kN = 15;
    if (parts.size() != kN) {
        return;
    }
    int64_t cap = 0;
    int64_t enc_s = 0;
    int64_t enc_f = 0;
    int64_t recv_s = 0;
    int64_t recv_f = 0;
    int64_t dec_s = 0;
    int64_t dec_f = 0;
    if (!ParseInt64Strict(parts[1], &cap) || !ParseInt64Strict(parts[2], &enc_s) ||
        !ParseInt64Strict(parts[3], &enc_f) || !ParseInt64Strict(parts[8], &recv_s) ||
        !ParseInt64Strict(parts[9], &recv_f) || !ParseInt64Strict(parts[10], &dec_s) ||
        !ParseInt64Strict(parts[11], &dec_f)) {
        std::cout << "[TimingDelta] 数值解析失败，跳过推导" << std::endl;
        return;
    }

    std::cout << "[TimingDelta] 接收端同一本地时钟（可与 decode_*、receive_* 直接相减）:" << std::endl;
    std::cout << "  first_RTP→decode_start = " << (dec_s - recv_s)
              << " ms（首包进机 → 解码开始；含网传抖动、抖动缓冲、拼帧与调度）" << std::endl;
    std::cout << "  last_RTP→decode_start = " << (dec_s - recv_f)
              << " ms（收齐该帧 → 解码开始；主要反映 JB/解码器前排队）" << std::endl;
    std::cout << "  decode_start→decode_finish = " << (dec_f - dec_s) << " ms（本帧解码耗时）" << std::endl;
    std::cout << "  RTP_last−first = " << (recv_f - recv_s) << " ms（该帧多包到达时间跨度）" << std::endl;

    if (cap >= 0) {
        std::cout << "[TimingDelta] 端到端（WebRTC 已把发端时间对齐到可与收端比较时；capture_time_ms>=0）:" << std::endl;
        std::cout << "  capture→decode_start = " << (dec_s - cap)
                  << " ms（发端采集 → 收端解码开始；最接近「编码前→解码前」里的采集到解码前）" << std::endl;
    } else {
        std::cout << "[TimingDelta] capture_time_ms<0：发收绝对时间尚未对齐，不能用 decode_start−capture 当整段端到端。"
                     " 可临时用 first_RTP→decode_start 看「到机后」延迟，或在业务层打统一时钟时间戳。"
                  << std::endl;
    }

    std::cout << "[TimingDelta] 发端侧相对量（各字段同一偏移下互减仍有意义，与收端大正数不是同一绝对时钟）:" << std::endl;
    std::cout << "  encode_start−capture = " << (enc_s - cap) << " ms" << std::endl;
    std::cout << "  encode_finish−encode_start = " << (enc_f - enc_s) << " ms（约等于本帧编码时长）" << std::endl;
}

void PrintGoogTimingFrameInfoLabeled(const std::string& raw) {
    static constexpr const char* kFieldZh[] = {
        "rtp_timestamp — RTP 时间戳（90kHz 时钟单位，不是毫秒）",
        "capture_time_ms — 发送端：采集时刻(ms)；收端 NTP 未对齐时常为负，相对关系仍可比",
        "encode_start_ms — 发送端：编码开始(ms)",
        "encode_finish_ms — 发送端：编码结束(ms)",
        "packetization_finish_ms — 发送端：组包完成 / 进入 pacer 前(ms)",
        "pacer_exit_ms — 发送端：该帧最后一包离开 pacer(ms)；未用时常为 -1",
        "network_timestamp_ms — 发送端/扩展：网内时间戳 1(ms)",
        "network2_timestamp_ms — 发送端/扩展：网内时间戳 2(ms)",
        "receive_start_ms — 接收端本地时钟：该帧第一个 RTP 包到达(ms)",
        "receive_finish_ms — 接收端本地时钟：该帧最后一个包收齐(ms)",
        "decode_start_ms — 接收端本地时钟：解码开始(ms)",
        "decode_finish_ms — 接收端本地时钟：解码结束(ms)",
        "render_time_ms — 接收端：建议渲染时刻(ms)",
        "is_outlier — 是否按帧大小判为异常上报(0/1)",
        "is_timer_triggered — 是否由周期定时器选中为 timing 帧(0/1)",
    };
    constexpr size_t kN = sizeof(kFieldZh) / sizeof(kFieldZh[0]);
    const std::vector<std::string> parts = SplitCommaFields(raw);
    if (parts.size() != kN) {
        std::cout << "[VideoTiming] TimingFrameInfo (raw): " << raw << std::endl;
        std::cout << "[VideoTiming] 字段数=" << parts.size() << "（期望 " << kN
                  << "），与当前 libwebrtc 的 ToString 格式不一致，未逐字段标注" << std::endl;
        return;
    }
    std::cout << "[VideoTiming] TimingFrameInfo 逐字段 (goog_timing_frame_info):" << std::endl;
    for (size_t i = 0; i < kN; ++i) {
        std::cout << "  [" << (i + 1) << "] " << kFieldZh[i] << " = " << parts[i] << std::endl;
    }
    PrintTimingFrameDerivedDeltas(parts);
}

}  // namespace

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
    std::cout << "[SIG_TIMING][pull] t_us=" << SignalingNowUs() << " " << msg << std::endl;
}

bool MediaTimingTraceEnabled() {
    static const bool enabled =
        rflow::common::util::TraceFlagEnabled("RFLOW_MEDIA_TIMING_TRACE");
    return enabled;
}

unsigned MediaTimingTraceEveryN() {
    static const unsigned every_n = []() {
        if (const char* v = std::getenv("RFLOW_MEDIA_TIMING_TRACE_EVERY_N")) {
            const int n = std::atoi(v);
            if (n >= 1 && n <= 600) {
                return static_cast<unsigned>(n);
            }
        }
        return 30u;
    }();
    return every_n;
}

webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfig() {
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    webrtc::PeerConnectionInterface::IceServer stun;
    stun.urls.push_back("stun:stun.l.google.com:19302");
    rtc_config.servers.push_back(stun);
    rtc_config.disable_ipv6_on_wifi = true;
    rtc_config.max_ipv6_networks = 0;
    rtc_config.disable_link_local_networks = true;
    rtc_config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    rtc_config.tcp_candidate_policy = webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
    rtc_config.set_dscp(true);
    rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    return rtc_config;
}

void PrintInboundVideoStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report) {
        return;
    }
    const std::vector<const webrtc::RTCInboundRtpStreamStats*> inbound =
        report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
    for (const webrtc::RTCInboundRtpStreamStats* s : inbound) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        std::cout << "[InboundVideoStats] id=" << s->id();
        if (s->ssrc.has_value()) {
            std::cout << " ssrc=" << *s->ssrc;
        }
        if (s->frames_decoded.has_value()) {
            std::cout << " frames_decoded=" << *s->frames_decoded;
        }
        if (s->frames_received.has_value()) {
            std::cout << " frames_received=" << *s->frames_received;
        }
        if (s->total_decode_time.has_value()) {
            std::cout << " total_decode_time_s=" << *s->total_decode_time;
        }
        if (s->total_processing_delay.has_value()) {
            std::cout << " total_processing_delay_s=" << *s->total_processing_delay;
        }
        if (s->jitter_buffer_delay.has_value()) {
            std::cout << " jitter_buffer_delay_s=" << *s->jitter_buffer_delay;
        }
        std::cout << std::endl;
        if (s->goog_timing_frame_info.has_value() && !s->goog_timing_frame_info->empty()) {
            PrintGoogTimingFrameInfoLabeled(*s->goog_timing_frame_info);
        } else {
            std::cout << "[VideoTiming] goog_timing_frame_info: (empty — 对端未带 video-timing 扩展、"
                         "或尚未选中用于上报的 timing frame；仍可见上行 total_processing_delay 等)"
                      << std::endl;
        }
    }
}

}  // namespace rflow::service::impl::detail::pull
