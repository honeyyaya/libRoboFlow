#include "rtc.h"

#include "rtc/peer_connection_factory_deps.h"
#include "rtc/rtc_factory_common.h"

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
    EnsureWebrtcFieldTrialsInitialized();

    auto& s = State();
    if (s.factory) {
        return true;
    }
    if (!StartThreads(s)) {
        RFLOW_CORE_LOGE("[rtc] start internal webrtc threads failed");
        return false;
    }

    auto adm = CreateDummyAudioDeviceModule();
    if (!adm) {
        RFLOW_CORE_LOGE("[rtc] create dummy ADM failed");
        StopThreads(s);
        return false;
    }

    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread   = s.network.get();
    deps.worker_thread    = s.worker.get();
    deps.signaling_thread = s.signaling.get();
    deps.adm              = std::move(adm);

    rflow::rtc::PeerConnectionFactoryMediaOptions media_opts;
#if defined(WEBRTC_ANDROID)
    media_opts.decoder_backend = rflow::rtc::VideoCodecBackendPreference::kAndroidMediaCodec;
#endif
    rflow::rtc::ConfigurePeerConnectionFactoryDependencies(deps, &media_opts);

    s.factory = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
    if (!s.factory) {
        RFLOW_CORE_LOGE("[rtc] CreateModularPeerConnectionFactory failed");
        StopThreads(s);
        return false;
    }
    RFLOW_CORE_LOGI("[rtc] peer_connection_factory ready");
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

webrtc::Thread* signaling_thread() { return State().signaling.get(); }

}  // namespace rflow::rtc
