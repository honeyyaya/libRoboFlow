/**
 * @file   peer_connection_factory.h
 * @brief  libwebrtc PeerConnectionFactory 与内部线程（仅 RFLOW_CLIENT_ENABLE_WEBRTC_IMPL 构建）；
 *        在 rflow::rtc::initialize 中创建，librflow_init 经 rflow::client::init_infrastructure 拉起。
 */

#ifndef __RFLOW_CORE_RTC_FACTORY_PEER_CONNECTION_FACTORY_H__
#define __RFLOW_CORE_RTC_FACTORY_PEER_CONNECTION_FACTORY_H__

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc/factory/peer_connection_factory_deps.h"

namespace webrtc {
class Thread;
}  // namespace webrtc

namespace rflow::rtc {

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory();
webrtc::Thread* network_thread();
webrtc::Thread* worker_thread();
webrtc::Thread* signaling_thread();

bool RecreatePeerConnectionFactory(const PeerConnectionFactoryMediaOptions& media_options);

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_FACTORY_PEER_CONNECTION_FACTORY_H__
