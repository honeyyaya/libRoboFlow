#ifndef __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_DEPS_H__
#define __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_DEPS_H__

#include "core/rtc/rtc_factory_common.h"

#include <memory>

#include "api/peer_connection_interface.h"

namespace webrtc {
class Thread;
}

namespace rflow::rtc {

enum class VideoCodecBackendPreference {
    kBuiltin = 0,
    kRockchipMpp = 1,
};

struct PeerConnectionFactoryMediaOptions {
    VideoCodecBackendPreference encoder_backend{VideoCodecBackendPreference::kBuiltin};
    VideoCodecBackendPreference decoder_backend{VideoCodecBackendPreference::kBuiltin};
};

void ConfigurePeerConnectionFactoryDependencies(
    webrtc::PeerConnectionFactoryDependencies& deps,
    const PeerConnectionFactoryMediaOptions* media_options = nullptr);

void EnsureDedicatedPeerConnectionSignalingThread(
    webrtc::PeerConnectionFactoryDependencies& deps,
    std::unique_ptr<webrtc::Thread>* owned_signaling_thread);

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_DEPS_H__
