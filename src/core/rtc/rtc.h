/**
 * @file   rtc.h
 * @brief  内部 WebRTC 抽象层（待接入 libwebrtc）
**/

#ifndef __RFLOW_CORE_RTC_H__
#define __RFLOW_CORE_RTC_H__

#include "rflow/librflow_common.h"

namespace rflow::rtc {

// 占位：后续在此封装 peer_connection / transport / codec factory 等。
bool initialize();
void shutdown();

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_H__
