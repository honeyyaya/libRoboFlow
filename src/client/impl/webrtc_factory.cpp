#include "webrtc_factory.h"

#include "common/internal/logger.h"

#include <memory>
#include <utility>

#include "api/audio/audio_device.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "rtc_base/thread.h"

#if defined(WEBRTC_ANDROID)
#  include "android_hw_video_decoder_factory.h"
#endif

namespace robrt::client::impl {
namespace {

struct FactoryThreads {
    std::unique_ptr<webrtc::Thread> network;
    std::unique_ptr<webrtc::Thread> worker;
    std::unique_ptr<webrtc::Thread> signaling;
    bool                            started = false;
};

FactoryThreads& Threads() {
    static FactoryThreads g;
    return g;
}

bool EnsureThreadsStarted(FactoryThreads& ctx) {
    if (ctx.started) return true;
    ctx.network   = webrtc::Thread::CreateWithSocketServer();
    ctx.worker    = webrtc::Thread::Create();
    ctx.signaling = webrtc::Thread::Create();
    if (!ctx.network || !ctx.worker || !ctx.signaling) return false;
    if (!ctx.network->Start() || !ctx.worker->Start() || !ctx.signaling->Start()) return false;
    ctx.started = true;
    return true;
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

}  // namespace

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> CreatePeerConnectionFactory() {
    FactoryThreads& th = Threads();
    if (!EnsureThreadsStarted(th)) {
        ROBRT_LOGE("[webrtc_factory] start internal threads failed");
        return nullptr;
    }

    auto adm = DummyAdm();
    if (!adm) {
        ROBRT_LOGE("[webrtc_factory] create dummy ADM failed");
        return nullptr;
    }

    std::unique_ptr<webrtc::VideoDecoderFactory> video_decoder_factory;
#if defined(WEBRTC_ANDROID)
    video_decoder_factory = CreateAndroidHwOrBuiltinVideoDecoderFactory();
#else
    video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
#endif

    // signaling 线程必须为独立线程；与 network 混用时 CreateAnswer 的 observer 回调会永不触发。
    auto factory = webrtc::CreatePeerConnectionFactory(
        th.network.get(), th.worker.get(), th.signaling.get(),
        adm,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        /*video_encoder_factory=*/nullptr,
        std::move(video_decoder_factory),
        /*audio_mixer=*/nullptr,
        /*audio_processing=*/nullptr);
    if (!factory) {
        ROBRT_LOGE("[webrtc_factory] CreatePeerConnectionFactory returned null");
    }
    return factory;
}

webrtc::Thread* PeerConnectionFactoryNetworkThread() {
    FactoryThreads& th = Threads();
    if (!EnsureThreadsStarted(th)) return nullptr;
    return th.network.get();
}

webrtc::Thread* PeerConnectionFactorySignalingThread() {
    FactoryThreads& th = Threads();
    if (!EnsureThreadsStarted(th)) return nullptr;
    return th.signaling.get();
}

}  // namespace robrt::client::impl
