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

std::string ZeroPlayoutDelayTrialString() {
    int pacing_ms = 1;
    if (const char* p = std::getenv("WEBRTC_DEMO_ZERO_PLAYOUT_MIN_PACING_MS")) {
        const int v = std::atoi(p);
        if (v >= 0 && v <= 20) {
            pacing_ms = v;
        }
    }
    int q = 6;
    if (const char* p = std::getenv("WEBRTC_DEMO_MAX_DECODE_QUEUE_SIZE")) {
        const int v = std::atoi(p);
        if (v >= 4 && v <= 16) {
            q = v;
        }
    }
    return "WebRTC-ZeroPlayoutDelay/min_pacing:" + std::to_string(pacing_ms) +
           "ms,max_decode_queue_size:" + std::to_string(q) + "/";
}

}  // namespace

void EnsureWebrtcFieldTrialsInitialized() {
    std::call_once(g_field_trials_once, []() {
        g_field_trials_storage =
            "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/"
            "WebRTC-ForcePlayoutDelay/min_ms:0,max_ms:0/";
        g_field_trials_storage += ZeroPlayoutDelayTrialString();
        g_field_trials_storage +=
            "WebRTC-Pacer-KeyframeFlushing/Enabled/"
            "WebRTC-Pacer-FastRetransmissions/Enabled/";
        if (EnvTruthy("WEBRTC_DEMO_ENABLE_FLEXFEC")) {
            g_field_trials_storage +=
                "WebRTC-FlexFEC-03-Advertised/Enabled/"
                "WebRTC-FlexFEC-03/Enabled/";
        }
        if (const char* extra = std::getenv("WEBRTC_DEMO_FIELD_TRIALS_APPEND")) {
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
