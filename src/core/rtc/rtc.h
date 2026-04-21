/**
 * @file   rtc.h
 * @brief  内部 WebRTC 抽象层（待接入 libwebrtc）
**/

#ifndef __ROBRT_CORE_RTC_H__
#define __ROBRT_CORE_RTC_H__

#include "robrt/librobrt_common.h"

namespace robrt::rtc {

// 占位：后续在此封装 peer_connection / transport / codec factory 等。
bool initialize();
void shutdown();

}  // namespace robrt::rtc

#endif  // __ROBRT_CORE_RTC_H__
