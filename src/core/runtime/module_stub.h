#ifndef __RFLOW_CORE_RUNTIME_MODULE_STUB_H__
#define __RFLOW_CORE_RUNTIME_MODULE_STUB_H__

#include "base/logging.h"

// 生成无 WebRTC / 无信令栈时的 initialize/shutdown 占位实现。
#define RFLOW_DEFINE_MODULE_STUB(ns, log_prefix) \
    namespace ns {                               \
    bool initialize() {                          \
        RFLOW_CORE_LOGI(log_prefix "::initialize (stub)"); \
        return true;                             \
    }                                            \
    void shutdown() {                            \
        RFLOW_CORE_LOGI(log_prefix "::shutdown (stub)"); \
    }                                            \
    }  /* namespace ns */

#endif  // __RFLOW_CORE_RUNTIME_MODULE_STUB_H__
