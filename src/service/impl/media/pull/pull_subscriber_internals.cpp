#include "media/pull/pull_subscriber_internals.h"

#include "rtc/rtc_ice_config.h"
#include "rtc/rtc_session_common.h"

namespace rflow::service::impl::detail::pull {

webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfig() {
    webrtc::PeerConnectionInterface::RTCConfiguration config =
        rflow::rtc::BuildRtcConfiguration(rflow::rtc::IceRtcServerConfig{});
    rflow::core::rtc::ApplyReceiverPeerConnectionDefaults(config);
    return config;
}

}  // namespace rflow::service::impl::detail::pull
