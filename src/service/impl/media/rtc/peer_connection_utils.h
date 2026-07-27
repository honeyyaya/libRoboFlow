#ifndef __RFLOW_SERVICE_IMPL_MEDIA_RTC_PEER_CONNECTION_UTILS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_RTC_PEER_CONNECTION_UTILS_H__

#include <string>

#include "api/peer_connection_interface.h"
#include "api/priority.h"
#include "api/scoped_refptr.h"

namespace webrtc {
class Thread;
}

namespace rflow::service::impl::media_util {

bool ClosePeerConnectionWithDeadline(
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const char* log_component,
    const char* log_tag,
    int timeout_sec);

void StopWebrtcThreadWithDeadline(webrtc::Thread* thread,
                                  const char* log_component,
                                  int timeout_sec);

webrtc::Priority ParseVideoNetworkPriority(const std::string& s);

webrtc::PeerConnectionInterface::RTCOfferAnswerOptions MakeVideoOfferOptions();

}  // namespace rflow::service::impl::media_util

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_RTC_PEER_CONNECTION_UTILS_H__
