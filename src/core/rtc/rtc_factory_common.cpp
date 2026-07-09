#include "rtc/rtc_factory_common.h"

#include "runtime/runtime_knobs.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "api/task_queue/default_task_queue_factory.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/field_trial.h"

#include <cctype>

namespace rflow::rtc {
namespace {

namespace knob = rflow::core::runtime;

std::mutex g_flexfec_trial_mu;
// nullopt ⇒ 不使用 SDK 固定值；仍读 RFLOW_ENABLE_FLEXFEC。
std::optional<bool> g_flexfec_explicit;

bool FlexfecTrialEnabledEffective() {
    std::optional<bool> local_copy;
    {
        std::lock_guard<std::mutex> lk(g_flexfec_trial_mu);
        local_copy = g_flexfec_explicit;
    }
    if (local_copy.has_value()) {
        return *local_copy;
    }
    return knob::ReadBool("RFLOW_ENABLE_FLEXFEC");
}

std::once_flag g_field_trials_once;
std::string    g_field_trials_storage;
std::once_flag g_webrtc_log_once;

webrtc::LoggingSeverity DefaultWebRtcLogSeverity() {
#if defined(NDEBUG)
    return webrtc::LS_NONE;
#else
    return webrtc::LS_WARNING;
#endif
}

webrtc::LoggingSeverity ParseWebRtcLogSeverity(const std::string& raw) {
    std::string s = raw;
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (s == "verbose" || s == "v") return webrtc::LS_VERBOSE;
    if (s == "info" || s == "i") return webrtc::LS_INFO;
    if (s == "warning" || s == "warn" || s == "w") return webrtc::LS_WARNING;
    if (s == "error" || s == "err" || s == "e") return webrtc::LS_ERROR;
    if (s == "none" || s == "off" || s == "0") return webrtc::LS_NONE;
    return DefaultWebRtcLogSeverity();
}

void EnsureWebrtcLoggingConfigured() {
    std::call_once(g_webrtc_log_once, []() {
        webrtc::LoggingSeverity sev = DefaultWebRtcLogSeverity();
        const std::string cfg = knob::ReadString("RFLOW_WEBRTC_LOG_SEVERITY");
        if (!cfg.empty()) {
            sev = ParseWebRtcLogSeverity(cfg);
        }
        webrtc::LogMessage::LogToDebug(sev);
    });
}

std::string ZeroPlayoutDelayTrialString() {
    int pacing_ms = knob::ReadInt("RFLOW_ZERO_PLAYOUT_MIN_PACING_MS");
    int queue_max = knob::ReadInt("RFLOW_MAX_DECODE_QUEUE_SIZE");

    // Optional guard for low-pacing mode to avoid decode queue buildup.
    if (knob::ReadBool("RFLOW_ENABLE_DECODE_QUEUE_GUARD")) {
        const int guard_cap = knob::ReadInt("RFLOW_DECODE_QUEUE_GUARD_CAP");
        if (pacing_ms <= 2 && queue_max > guard_cap) {
            queue_max = guard_cap;
        }
    }

    return "WebRTC-ZeroPlayoutDelay/min_pacing:" + std::to_string(pacing_ms) +
           "ms,max_decode_queue_size:" + std::to_string(queue_max) + "/";
}

}  // namespace

void EnsureWebrtcFieldTrialsInitialized() {
    EnsureWebrtcLoggingConfigured();
    std::call_once(g_field_trials_once, []() {
        g_field_trials_storage =
            "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/"
            "WebRTC-ForcePlayoutDelay/min_ms:0,max_ms:60/";
        g_field_trials_storage += ZeroPlayoutDelayTrialString();
        g_field_trials_storage +=
            "WebRTC-Pacer-KeyframeFlushing/Enabled/"
            "WebRTC-Pacer-FastRetransmissions/Enabled/";

        if (FlexfecTrialEnabledEffective()) {
            g_field_trials_storage +=
                "WebRTC-FlexFEC-03-Advertised/Enabled/"
                "WebRTC-FlexFEC-03/Enabled/";
        }
        // Generic RTP picture id for VP8/VP9-style generic frame descriptors (not H264 ULPFEC).
        g_field_trials_storage += "WebRTC-GenericPictureId/Enabled/";
        const std::string extra = knob::ReadString("RFLOW_FIELD_TRIALS_APPEND");
        if (!extra.empty()) {
            g_field_trials_storage += extra;
        }

        webrtc::field_trial::InitFieldTrialsFromString(g_field_trials_storage.c_str());
    });
}

webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateDummyAudioDeviceModule() {
    static std::unique_ptr<webrtc::TaskQueueFactory> task_queue_factory =
        webrtc::CreateDefaultTaskQueueFactory();
    // 每次新建 ADM，绑定当前线程/TaskQueue 亲和；勿用 static 单例跨 Factory/线程复用。
    return webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio,
                                             task_queue_factory.get());
}

void NotifyFlexfecTrialFromSdkConfig(bool explicitly_set, bool enabled) {
    std::lock_guard<std::mutex> lk(g_flexfec_trial_mu);
    if (explicitly_set) {
        g_flexfec_explicit = enabled;
    } else {
        g_flexfec_explicit.reset();
    }
}

void ResetFlexfecTrialSdkOverride(void) {
    std::lock_guard<std::mutex> lk(g_flexfec_trial_mu);
    g_flexfec_explicit.reset();
}

}  // namespace rflow::rtc
