#include "rtc.h"

#include "common/internal/logger.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "api/audio/audio_device.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/field_trial.h"

#if defined(WEBRTC_ANDROID)
#  include "decoder/android/android_hw_video_decoder_factory.h"
#endif

namespace rflow::rtc {
namespace {

std::once_flag g_field_trials_once;

void InitFieldTrialsOnce() {
    static const std::string trials = "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/";
    webrtc::field_trial::InitFieldTrialsFromString(trials.c_str());
}

struct FactoryState {
    std::unique_ptr<webrtc::Thread> network;
    std::unique_ptr<webrtc::Thread> worker;
    std::unique_ptr<webrtc::Thread> signaling;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    bool threads_started = false;
};

FactoryState& State() {
    static FactoryState g;
    return g;
}

// Dummy ADM + TaskQueueFactory 与 Factory 同生命周期；不访问真实麦克风/扬声器，
// 但不能为 null，否则 WebRtcVoiceEngine::Init() 会崩溃。
webrtc::scoped_refptr<webrtc::AudioDeviceModule> DummyAdm() {
    static std::unique_ptr<webrtc::TaskQueueFactory> task_queue_factory =
        webrtc::CreateDefaultTaskQueueFactory();
    static webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm =
        webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio,
                                          task_queue_factory.get());
    return adm;
}

bool StartThreads(FactoryState& s) {
    if (s.threads_started) return true;
    s.network   = webrtc::Thread::CreateWithSocketServer();
    s.worker    = webrtc::Thread::Create();
    s.signaling = webrtc::Thread::Create();
    if (!s.network || !s.worker || !s.signaling) return false;
    if (!s.network->Start() || !s.worker->Start() || !s.signaling->Start()) return false;
    s.threads_started = true;
    return true;
}

void StopThreads(FactoryState& s) {
    if (!s.threads_started) return;
    if (s.network)  s.network->Stop();
    if (s.worker)   s.worker->Stop();
    if (s.signaling) s.signaling->Stop();
    s.signaling.reset();
    s.worker.reset();
    s.network.reset();
    s.threads_started = false;
}

}  // namespace

bool initialize() {
    std::call_once(g_field_trials_once, InitFieldTrialsOnce);

    auto& s = State();
    if (s.factory) {
        return true;
    }
    if (!StartThreads(s)) {
        RFLOW_LOGE("[rtc] start internal webrtc threads failed");
        return false;
    }

    auto adm = DummyAdm();
    if (!adm) {
        RFLOW_LOGE("[rtc] create dummy ADM failed");
        StopThreads(s);
        return false;
    }

    std::unique_ptr<webrtc::VideoDecoderFactory> video_decoder_factory;
#if defined(WEBRTC_ANDROID)
    video_decoder_factory = CreateAndroidHwOrBuiltinVideoDecoderFactory();
#else
    video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
#endif

    s.factory = webrtc::CreatePeerConnectionFactory(
        s.network.get(), s.worker.get(), s.signaling.get(),
        adm,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        /*video_encoder_factory=*/nullptr,
        std::move(video_decoder_factory),
        /*audio_mixer=*/nullptr,
        /*audio_processing=*/nullptr);
    if (!s.factory) {
        RFLOW_LOGE("[rtc] CreatePeerConnectionFactory failed");
        StopThreads(s);
        return false;
    }
    RFLOW_LOGI("[rtc] peer_connection_factory ready");
    return true;
}

void shutdown() {
    auto& s = State();
    s.factory = nullptr;
    StopThreads(s);
    RFLOW_LOGI("[rtc] peer_connection_factory shutdown");
}

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory() {
    return State().factory;
}

webrtc::Thread* network_thread() { return State().network.get(); }

webrtc::Thread* signaling_thread() { return State().signaling.get(); }

}  // namespace rflow::rtc
