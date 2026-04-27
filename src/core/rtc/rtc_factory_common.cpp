#include "core/rtc/rtc_factory_common.h"

#include <memory>
#include <mutex>
#include <string>

#include "api/task_queue/default_task_queue_factory.h"
#include "system_wrappers/include/field_trial.h"

namespace rflow::rtc {
namespace {

std::once_flag g_field_trials_once;
std::string    g_field_trials_storage;

}  // namespace

void EnsureWebrtcFieldTrialsInitialized() {
    std::call_once(g_field_trials_once, []() {
        // g_field_trials_storage =
        //     "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/"
        //     "WebRTC-ForcePlayoutDelay/min_ms:0,max_ms:0/";
        g_field_trials_storage =
            "WebRTC-ZeroPlayoutDelay/min_pacing:1ms,max_decode_queue_size:4/"
            "WebRTC-Pacer-KeyframeFlushing/Enabled/"
            "WebRTC-Pacer-FastRetransmissions/Enabled/";
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
