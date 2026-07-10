#ifndef __RFLOW_CORE_RTC_ICE_CONFIG_H__
#define __RFLOW_CORE_RTC_ICE_CONFIG_H__

#include "api/peer_connection_interface.h"

#include <string>
#include <vector>

namespace webrtc {
class PeerConnectionFactoryDependencies;
class Thread;
}  // namespace webrtc

namespace rflow::rtc {

/// STUN/TURN servers and ICE flags for one PeerConnection.
struct IceRtcServerConfig {
    std::string stun_server;
    std::string turn_server;
    std::string turn_username;
    std::string turn_password;
    bool ice_prioritize_likely_pairs{true};
    bool has_stun_server{false};
    bool has_turn_server{false};
};

/// Default STUN when stream_param does not set stun_server explicitly.
const char* DefaultStunServerUrl();

/// Parse "eth0, eth1" into {"eth0","eth1"}; invalid tokens are dropped.
std::vector<std::string> ParseCommaSeparatedInterfaceList(const char* csv);

/// Apply global_config ice_ignore_interfaces before first factory init.
void NotifyIceIgnoreInterfacesFromSdkConfig(const char* comma_separated_or_null);

/// Effective ignore list: SDK global override, else RFLOW_ICE_IGNORE_INTERFACES env.
std::vector<std::string> IceIgnoreInterfacesEffective();

void ResetIceIgnoreInterfacesSdkOverride();

/// Build RTCConfiguration (STUN/TURN + push/pull shared ICE flags).
webrtc::PeerConnectionInterface::RTCConfiguration BuildRtcConfiguration(
    const IceRtcServerConfig& ice,
    bool disable_link_local_networks = true,
    bool disable_ipv6_on_wifi = true);

/// Inject BasicNetworkManager with ignore list when configured.
void MaybeInjectFilteredNetworkManager(webrtc::PeerConnectionFactoryDependencies& deps);

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_ICE_CONFIG_H__
