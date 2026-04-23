/**
 * @file   rtc.h
 * @brief  内部 WebRTC 抽象层：进程级 init/shutdown（与 librflow_init / librflow_uninit 配对）
 *
 * 调用关系：
 *   - initialize()：创建并持有 PeerConnectionFactory、内部 network/worker/signaling 线程（WebRTC 实现）
 *     或仅打日志（rtc_stub）。
 *   - shutdown()：释放工厂与线程，与 initialize() 逆序。
 *
 * 若定义 RFLOW_RTC_WEBRTC_PEER_CONNECTION_API（Client + RFLOW_CLIENT_ENABLE_WEBRTC_IMPL），
 * 会一并包含 peer_connection_factory.h；initialize() 成功后可用 peer_connection_factory() 等。
 * Client 侧子系统顺序见 rflow::client::init_infrastructure()（infrastructure.cpp）。
**/

#ifndef __RFLOW_CORE_RTC_H__
#define __RFLOW_CORE_RTC_H__

#include "rflow/librflow_common.h"

namespace rflow::rtc {

bool initialize();
void shutdown();

}  // namespace rflow::rtc

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "core/rtc/peer_connection_factory.h"
#endif

#endif  // __RFLOW_CORE_RTC_H__
