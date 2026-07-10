#include "rtc.h"

#include "rtc/peer_connection_factory_deps.h"
#include "rtc/rtc_factory_common.h"
#include "runtime/runtime_knobs.h"

#include "base/logging.h"

#include <memory>
#include <utility>

#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"

namespace rflow::rtc {
namespace {

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

bool CreateFactoryOnSignalingThread(FactoryState& s,
                                    const rflow::rtc::PeerConnectionFactoryMediaOptions& media_opts) {
    webrtc::Thread* sig = s.signaling.get();
    if (!sig) return false;

    auto create = [&]() -> bool {
        webrtc::PeerConnectionFactoryDependencies deps;
        deps.network_thread   = s.network.get();
        deps.worker_thread    = s.worker.get();
        deps.signaling_thread = sig;
        // 不在 main 线程预建 ADM：Debug libwebrtc 要求 RegisterAudioCallback 与 ADM 构造同序列；
        // 由 CreateModularPeerConnectionFactory 在 signaling 线程上完成音频栈接线。
        rflow::rtc::PeerConnectionFactoryMediaOptions opts = media_opts;
        rflow::rtc::ConfigurePeerConnectionFactoryDependencies(deps, &opts);
        s.factory = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
        return s.factory != nullptr;
    };

    if (sig->IsCurrent()) {
        return create();
    }
    bool ok = false;
    sig->BlockingCall([&]() { ok = create(); });
    return ok;
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

VideoCodecBackendPreference DefaultDecoderBackendForInitialize() {
#if defined(WEBRTC_ANDROID)
    return VideoCodecBackendPreference::kAndroidMediaCodec;
#elif defined(RFLOW_HAVE_ROCKCHIP_MPP)
    const std::string backend =
        rflow::core::runtime::ReadString("RFLOW_DECODER_BACKEND");
    if (!backend.empty()) {
        if (backend == "builtin" || backend == "ffmpeg" || backend == "0") {
            return VideoCodecBackendPreference::kBuiltin;
        }
        if (backend == "mpp" || backend == "rockchip" || backend == "1") {
            return VideoCodecBackendPreference::kRockchipMpp;
        }
    }
    // 默认 MPP：避免 FFmpeg H264 软解在 frame_num 回绕（~3000 帧）后卡死。
    return VideoCodecBackendPreference::kRockchipMpp;
#else
    return VideoCodecBackendPreference::kBuiltin;
#endif
}

const char* DecoderBackendLabel(VideoCodecBackendPreference backend) {
    switch (backend) {
        case VideoCodecBackendPreference::kRockchipMpp:
            return "rockchip_mpp";
#if defined(WEBRTC_ANDROID)
        case VideoCodecBackendPreference::kAndroidMediaCodec:
            return "android_mediacodec";
#endif
        case VideoCodecBackendPreference::kBuiltin:
        default:
            return "builtin";
    }
}

}  // namespace

bool initialize() {
    EnsureWebrtcFieldTrialsInitialized();

    auto& s = State();
    if (s.factory) {
        return true;
    }
    if (!StartThreads(s)) {
        RFLOW_CORE_LOGE("[rtc] start internal webrtc threads failed");
        return false;
    }

    rflow::rtc::PeerConnectionFactoryMediaOptions media_opts;
    media_opts.decoder_backend = DefaultDecoderBackendForInitialize();
    if (!CreateFactoryOnSignalingThread(s, media_opts)) {
        RFLOW_CORE_LOGE("[rtc] CreateModularPeerConnectionFactory failed");
        StopThreads(s);
        return false;
    }
    RFLOW_CORE_LOGI("[rtc] peer_connection_factory ready decoder=%s",
                    DecoderBackendLabel(media_opts.decoder_backend));
    return true;
}

bool RecreatePeerConnectionFactory(const PeerConnectionFactoryMediaOptions& media_options) {
    auto& s = State();
    if (!s.threads_started && !StartThreads(s)) {
        RFLOW_CORE_LOGE("[rtc] RecreatePeerConnectionFactory: start threads failed");
        return false;
    }
    if (!CreateFactoryOnSignalingThread(s, media_options)) {
        RFLOW_CORE_LOGE("[rtc] RecreatePeerConnectionFactory failed");
        return false;
    }
    RFLOW_CORE_LOGI("[rtc] peer_connection_factory recreated");
    return true;
}

void shutdown() {
    auto& s = State();
    s.factory = nullptr;
    StopThreads(s);
    RFLOW_CORE_LOGI("[rtc] peer_connection_factory shutdown");
}

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory() {
    return State().factory;
}

webrtc::Thread* network_thread() { return State().network.get(); }

webrtc::Thread* worker_thread() { return State().worker.get(); }

webrtc::Thread* signaling_thread() { return State().signaling.get(); }

}  // namespace rflow::rtc
