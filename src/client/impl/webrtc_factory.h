/**
 * @file   webrtc_factory.h
 * @brief  PeerConnectionFactory 创建（旧 webrtc_factory_helper 移植，去 Qt）
 *
 * 进程内懒初始化单份 network / worker / signaling 线程及一份 Dummy ADM；
 * 多路流共享同一 factory。
 */

#ifndef __ROBRT_CLIENT_IMPL_WEBRTC_FACTORY_H__
#define __ROBRT_CLIENT_IMPL_WEBRTC_FACTORY_H__

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"

namespace webrtc {
class Thread;
}  // namespace webrtc

namespace robrt::client::impl {

// 创建 / 取回进程级共享的 PeerConnectionFactory。
// Android 侧 H.264 优先 NDK MediaCodec；其余平台/编码走内置解码工厂；不设编码工厂。
webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> CreatePeerConnectionFactory();

webrtc::Thread* PeerConnectionFactoryNetworkThread();
webrtc::Thread* PeerConnectionFactorySignalingThread();

}  // namespace robrt::client::impl

#endif  // __ROBRT_CLIENT_IMPL_WEBRTC_FACTORY_H__
