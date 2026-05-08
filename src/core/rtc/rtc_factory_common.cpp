#include "core/rtc/rtc_factory_common.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include "api/task_queue/default_task_queue_factory.h"
#include "system_wrappers/include/field_trial.h"

namespace rflow::rtc {
namespace {

// Carry a small NetEQ floor so a single packet loss has time to be repaired
// by NACK retransmission before the frame is forwarded to the decoder.
// Targeting steady-state jb_avg ~25-35 ms: 20 ms floor lets clean frames
// glide through quickly while still surviving one short retransmission on
// a typical LAN / WiFi RTT. Setting this to 0 produced visible corruption.
constexpr double kReceiverVideoJitterBufferMinDelaySeconds = 0.02;  // 20 ms
// Hard cap NetEQ playout delay to [min, max] ms. min lets clean frames flow
// through immediately; max bounds the worst-case latency we will tolerate.
// 60 ms is roughly one LAN RTT plus a vsync of slack -- enough headroom for
// a single NACK retransmission to land before the frame is emitted, while
// keeping the steady-state target at the low end of the [20, 60] window.
constexpr int kReceiverForcedPlayoutMinMs = 0;
constexpr int kReceiverForcedPlayoutMaxMs = 60;
// Pace ZeroPlayoutDelay flushes at ~half a 60 fps frame interval. 2 ms is
// the documented sweet spot: low enough to not stall at high frame rates,
// high enough to coalesce bursts and avoid spin-loop pacer wakeups.
constexpr int kReceiverZeroPlayoutMinPacingMs = 2;
// Decode queue depth in ZeroPlayoutDelay mode. 3 keeps the smallest ring
// that still tolerates a normal amount of out-of-order delivery: 2 frames
// for the jitter buffer to slot retransmissions into plus 1 for the decoder
// in flight. Going to 2 here makes any reordered packet a frame drop.
constexpr int kReceiverMaxDecodeQueueSize = 3;
constexpr bool kReceiverEnablePacerFastRetransmissions = true;
// Enable FlexFEC-03 reception. Advertise the codec in our Answer SDP so the
// sender can opt in, and turn on the receive-side FEC depacketizer. FlexFEC
// is per-packet redundancy, so it complements (not replaces) NACK and lets
// us recover single-packet drops without spending an RTT on retransmission.
// Receiver-side has no downside if the sender does not send FEC; advertise
// is essentially free.
constexpr bool kReceiverEnableFlexFec = true;

bool EnvTruthy(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) {
        return false;
    }
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T';
}

int ReadEnvIntInRange(const char* name, int def, int lo, int hi) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) {
        return def;
    }
    int n = std::atoi(v);
    if (n < lo || n > hi) {
        return def;
    }
    return n;
}

// Demo 来源：rtc_demo_new::webrtc_receiver_client.cpp 同名常量（kReceiver*）。
// min_pacing=2ms 是 60fps 半帧间隔——足够低不卡高帧率，又能凝结突发避免 pacer spin-loop；
// max_decode_queue_size=3 是 jb 能容下 2 包乱序 + 1 解码 in-flight 的最小环，再低任何乱序都掉帧。
constexpr int kDefaultZeroPlayoutMinPacingMs = 2;
constexpr int kDefaultMaxDecodeQueueSize = 3;

std::string ZeroPlayoutDelayTrialString() {
    int pacing_ms = ReadEnvIntInRange("RFLOW_ZERO_PLAYOUT_MIN_PACING_MS",
                                      kDefaultZeroPlayoutMinPacingMs, 0, 20);
    int queue_max = ReadEnvIntInRange("RFLOW_MAX_DECODE_QUEUE_SIZE",
                                      kDefaultMaxDecodeQueueSize, 2, 16);

    // 低 pacing 模式下的可选保险：如果运维显式打开 guard，把队列再夹紧避免堆积。
    if (EnvTruthy("RFLOW_ENABLE_DECODE_QUEUE_GUARD")) {
        const int guard_cap = ReadEnvIntInRange("RFLOW_DECODE_QUEUE_GUARD_CAP", 6, 4, 12);
        if (pacing_ms <= 2 && queue_max > guard_cap) {
            queue_max = guard_cap;
        }
    }

    return "WebRTC-ZeroPlayoutDelay/min_pacing:" + std::to_string(pacing_ms) +
           "ms,max_decode_queue_size:" + std::to_string(queue_max) + "/";
}

// Demo 来源：rtc_demo_new::kReceiverForcedPlayoutMinMs/MaxMs（默认 0..60）。
// max>0 给 NACK 单包重传留余量；min=0 使干净帧瞬时通过；
// **max<=0 时不输出该 trial**——与 demo 中 `if (max_ms > 0)` 卫语句对齐，避免与 ZeroPlayoutDelay 冲突。
constexpr int kDefaultForcePlayoutMinMs = 0;
constexpr int kDefaultForcePlayoutMaxMs = 60;

std::string ForcePlayoutDelayTrialString() {
    const int min_ms = static_cast<int>(
        ReadEnvIntInRange("RFLOW_FORCE_PLAYOUT_MIN_MS", kDefaultForcePlayoutMinMs, 0, 1000));
    const int max_ms = static_cast<int>(
        ReadEnvIntInRange("RFLOW_FORCE_PLAYOUT_MAX_MS", kDefaultForcePlayoutMaxMs, 0, 2000));
    if (max_ms <= 0 || min_ms > max_ms) {
        return {};
    }
    return "WebRTC-ForcePlayoutDelay/min_ms:" + std::to_string(min_ms) +
           ",max_ms:" + std::to_string(max_ms) + "/";
}

// Demo 来源：rtc_demo_new::kReceiverEnableFlexFec=true（默认启用）。
// FlexFEC-03 是逐包冗余，与 NACK 互补——单包丢失可不花一个 RTT 立即恢复，且与 NACK 并存无冲突。
// 兼容旧脚本：env RFLOW_ENABLE_FLEXFEC 显式设值时按其语义生效（0/n/N/f/F→关；其它→开）；未设则默认开。
constexpr bool kDefaultEnableFlexFec = true;

bool ResolveFlexFecEnabled() {
    const char* v = std::getenv("RFLOW_ENABLE_FLEXFEC");
    if (!v || !v[0]) {
        return kDefaultEnableFlexFec;
    }
    const char c = v[0];
    return !(c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F');
}

constexpr bool kDefaultEnablePacerFastRetransmissions = true;

}  // namespace

void EnsureWebrtcFieldTrialsInitialized() {
    // 须在 CreatePeerConnectionFactory 之前注册；全局至多一次（见 field_trial.h）。
  // 与发送端保持一致：FrameTracking（RTP 扩展协商）+ FlexFEC-03（SDP 中带 flexfec-03，且启用 FEC 收包；可与 NACK 并存）。
  // static std::string g_field_trials_storage ="WebRTC-VideoFrameTrackingIdAdvertised/Enabled/";
  // g_field_trials_storage +="WebRTC-ForcePlayoutDelay/min_ms:100,max_ms:100/";
  // g_field_trials_storage += "WebRTC-ZeroPlayoutDelay/min_pacing:4ms,max_decode_queue_size:6/";
  // g_field_trials_storage +="WebRTC-Pacer-KeyframeFlushing/Enabled/";
  // g_field_trials_storage +="WebRTC-Pacer-FastRetransmissions/Enabled/";

  static const std::string g_field_trials_storage = [] {
    std::string s = "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/";
    // Cap NetEQ playout delay to [min, max] ms instead of pinning it to a
    // fixed value. min=0,max=30 lets the receiver follow the wire while still
    // tolerating short bursts of jitter.
    if (kReceiverForcedPlayoutMaxMs > 0 &&
        kReceiverForcedPlayoutMinMs <= kReceiverForcedPlayoutMaxMs) {
      s += "WebRTC-ForcePlayoutDelay/min_ms:" +
           std::to_string(kReceiverForcedPlayoutMinMs) + ",max_ms:" +
           std::to_string(kReceiverForcedPlayoutMaxMs) + "/";
    }
    s += "WebRTC-ZeroPlayoutDelay/min_pacing:" +
         std::to_string(kReceiverZeroPlayoutMinPacingMs) +
         "ms,max_decode_queue_size:" +
         std::to_string(kReceiverMaxDecodeQueueSize) + "/";
    s += "WebRTC-Pacer-KeyframeFlushing/Enabled/";
    if (kReceiverEnablePacerFastRetransmissions) {
      s += "WebRTC-Pacer-FastRetransmissions/Enabled/";
    }
    if (kReceiverEnableFlexFec) {
      // -Advertised: include flexfec-03 in the Answer SDP capability list.
      // Without this trial WebRTC strips FEC codecs from the offer side, so
      // the sender never sees that we can consume them.
      s += "WebRTC-FlexFEC-03-Advertised/Enabled/";
      // (no -Advertised suffix): turn on receive-side FlexFEC depacketization
      // and recovery. Pairs with the codec advertised above.
      s += "WebRTC-FlexFEC-03/Enabled/";
    }
    return s;
  }();
  static bool field_trials_inited = false;
  if (!field_trials_inited) {
      webrtc::field_trial::InitFieldTrialsFromString(g_field_trials_storage.c_str());
      field_trials_inited = true;
  }
}

webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateDummyAudioDeviceModule() {
    static std::unique_ptr<webrtc::TaskQueueFactory> task_queue_factory =
        webrtc::CreateDefaultTaskQueueFactory();
    static webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm =
        webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio,
                                          task_queue_factory.get());
    return adm;
}

}  // namespace rflow::rtc
