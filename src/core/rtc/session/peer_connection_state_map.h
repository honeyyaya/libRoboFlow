#ifndef __RFLOW_CORE_RTC_SESSION_PEER_CONNECTION_STATE_MAP_H__
#define __RFLOW_CORE_RTC_SESSION_PEER_CONNECTION_STATE_MAP_H__

#include "api/peer_connection_interface.h"
#include "media/connection_state.h"

namespace rflow::core::rtc {

inline rflow::common::media::ConnectionState MapPeerConnectionState(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
    using CS = rflow::common::media::ConnectionState;
    using PCS = webrtc::PeerConnectionInterface::PeerConnectionState;
    switch (state) {
        case PCS::kConnecting:
            return CS::Connecting;
        case PCS::kConnected:
            return CS::Connected;
        case PCS::kDisconnected:
            return CS::Disconnected;
        case PCS::kFailed:
            return CS::Failed;
        case PCS::kClosed:
            return CS::Closed;
        default:
            return CS::New;
    }
}

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_SESSION_PEER_CONNECTION_STATE_MAP_H__
