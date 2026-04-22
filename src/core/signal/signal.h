/**
 * @file   signal.h
 * @brief  内部信令通道抽象（WS / 自研 / 直连模式）
**/

#ifndef __RFLOW_CORE_SIGNAL_H__
#define __RFLOW_CORE_SIGNAL_H__

#include "rflow/librflow_common.h"

namespace rflow::signal {

bool initialize();
void shutdown();

}  // namespace rflow::signal

#endif  // __RFLOW_CORE_SIGNAL_H__
