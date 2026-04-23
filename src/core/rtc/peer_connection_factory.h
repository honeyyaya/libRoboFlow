/**
 * @file   peer_connection_factory.h
 * @brief  libwebrtc PeerConnectionFactory 与内部线程（仅 RFLOW_CLIENT_ENABLE_WEBRTC_IMPL 构建）；
 *        在 rflow::rtc::initialize 中创建，librflow_init 经 rflow::client::init_infrastructure 拉起。
 */

#ifndef __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_H__
#define __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_H__

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"

namespace webrtc {
class Thread;
}  // namespace webrtc

namespace rflow::rtc {

// 与 rtc::initialize() 创建、rtc::shutdown() 释放的进程级单例；未初始化时返回 nullptr。
webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory();
webrtc::Thread* network_thread();
webrtc::Thread* signaling_thread();

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_PEER_CONNECTION_FACTORY_H__
