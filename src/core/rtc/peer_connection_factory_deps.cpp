#include "core/rtc/peer_connection_factory_deps.h"

#include <memory>

#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/enable_media.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "core/rtc/hw/backend_registry.h"
#include "rtc_base/thread.h"

namespace rflow::rtc {

void ConfigurePeerConnectionFactoryDependencies(
    webrtc::PeerConnectionFactoryDependencies& deps,
    const PeerConnectionFactoryMediaOptions* media_options) {
    if (deps.task_queue_factory == nullptr) {
        deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
    }
    if (deps.audio_encoder_factory == nullptr) {
        deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
    }
    if (deps.audio_decoder_factory == nullptr) {
        deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
    }
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    if (deps.audio_processing == nullptr &&
        deps.audio_processing_builder == nullptr) {
        deps.audio_processing_builder =
            std::make_unique<webrtc::BuiltinAudioProcessingBuilder>();
    }
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
    if (deps.video_encoder_factory == nullptr) {
        rflow::rtc::hw::VideoBackendPreferences prefs;
        if (media_options) {
            prefs.encoder_backend =
                media_options->encoder_backend == VideoCodecBackendPreference::kRockchipMpp
                    ? rflow::rtc::hw::VideoCodecBackend::kRockchipMpp
                    : rflow::rtc::hw::VideoCodecBackend::kBuiltin;
        }
        deps.video_encoder_factory = rflow::rtc::hw::CreatePreferredVideoEncoderFactory(prefs);
    }
    if (deps.video_decoder_factory == nullptr) {
        rflow::rtc::hw::VideoBackendPreferences prefs;
        if (media_options) {
            prefs.decoder_backend =
                media_options->decoder_backend == VideoCodecBackendPreference::kRockchipMpp
                    ? rflow::rtc::hw::VideoCodecBackend::kRockchipMpp
                    : rflow::rtc::hw::VideoCodecBackend::kBuiltin;
        }
        deps.video_decoder_factory = rflow::rtc::hw::CreatePreferredVideoDecoderFactory(prefs);
    }
    webrtc::EnableMedia(deps);
}

void EnsureDedicatedPeerConnectionSignalingThread(
    webrtc::PeerConnectionFactoryDependencies& deps,
    std::unique_ptr<webrtc::Thread>* owned_signaling_thread) {
    if (!owned_signaling_thread || deps.signaling_thread != nullptr) {
        return;
    }
    auto th = webrtc::Thread::CreateWithSocketServer();
    th->SetName("webrtc_sig", nullptr);
    th->Start();
    deps.signaling_thread = th.get();
    *owned_signaling_thread = std::move(th);
}

}  // namespace rflow::rtc
