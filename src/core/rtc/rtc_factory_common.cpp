#include "core/rtc/rtc_factory_common.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include "api/task_queue/default_task_queue_factory.h"
#include "system_wrappers/include/field_trial.h"

namespace rflow::rtc {
namespace {

std::once_flag g_field_trials_once;
std::string    g_field_trials_storage;

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

// Demo 默认：min_pacing=2ms（~半个 60fps 帧间隔，避免高帧率 spin-loop）、
// max_decode_queue_size=3（jb 容许 2 包乱序 + 1 解码 in-flight 的最小环）。
std::string ZeroPlayoutDelayTrialString() {
    int pacing_ms = ReadEnvIntInRange("RFLOW_ZERO_PLAYOUT_MIN_PACING_MS", 2, 0, 20);
    int queue_max = ReadEnvIntInRange("RFLOW_MAX_DECODE_QUEUE_SIZE", 3, 2, 16);

    // Optional guard for low-pacing mode to avoid decode queue buildup.
    if (EnvTruthy("RFLOW_ENABLE_DECODE_QUEUE_GUARD")) {
        const int guard_cap = ReadEnvIntInRange("RFLOW_DECODE_QUEUE_GUARD_CAP", 6, 4, 12);
        if (pacing_ms <= 2 && queue_max > guard_cap) {
            queue_max = guard_cap;
        }
    }

    return "WebRTC-ZeroPlayoutDelay/min_pacing:" + std::to_string(pacing_ms) +
           "ms,max_decode_queue_size:" + std::to_string(queue_max) + "/";
}

// Demo 默认：playout 延迟夹在 [0, 60] ms。max>0 给 NACK 单包重传留余量；
// min=0 使干净帧瞬时通过；env RFLOW_FORCE_PLAYOUT_MIN_MS / _MAX_MS 可覆盖。
std::string ForcePlayoutDelayTrialString() {
    const int min_ms = static_cast<int>(
        ReadEnvIntInRange("RFLOW_FORCE_PLAYOUT_MIN_MS", 0, 0, 1000));
    const int max_ms = static_cast<int>(
        ReadEnvIntInRange("RFLOW_FORCE_PLAYOUT_MAX_MS", 60, 0, 2000));
    const int eff_min = (min_ms <= max_ms) ? min_ms : max_ms;
    return "WebRTC-ForcePlayoutDelay/min_ms:" + std::to_string(eff_min) +
           ",max_ms:" + std::to_string(max_ms) + "/";
}

}  // namespace

void EnsureWebrtcFieldTrialsInitialized() {
    std::call_once(g_field_trials_once, []() {
        g_field_trials_storage = "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/";
        g_field_trials_storage += ForcePlayoutDelayTrialString();
        g_field_trials_storage += ZeroPlayoutDelayTrialString();
        g_field_trials_storage +=
            "WebRTC-Pacer-KeyframeFlushing/Enabled/"
            "WebRTC-Pacer-FastRetransmissions/Enabled/";

        if (EnvTruthy("RFLOW_ENABLE_FLEXFEC")) {
            g_field_trials_storage +=
                "WebRTC-FlexFEC-03-Advertised/Enabled/"
                "WebRTC-FlexFEC-03/Enabled/";
        }
        if (const char* extra = std::getenv("RFLOW_FIELD_TRIALS_APPEND")) {
            if (extra[0] != '\0') {
                g_field_trials_storage += extra;
            }
        }

        webrtc::field_trial::InitFieldTrialsFromString(g_field_trials_storage.c_str());
    });
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
