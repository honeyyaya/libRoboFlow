/**
 * @file   signal.h
 * @brief  内部信令通道抽象（WS / 自研 / 直连模式）
**/

#ifndef __ROBRT_CORE_SIGNAL_H__
#define __ROBRT_CORE_SIGNAL_H__

#include "robrt/librobrt_common.h"

namespace robrt::signal {

bool initialize();
void shutdown();

}  // namespace robrt::signal

#endif  // __ROBRT_CORE_SIGNAL_H__
