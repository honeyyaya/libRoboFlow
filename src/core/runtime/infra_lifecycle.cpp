#include "core/runtime/infra_lifecycle.h"

#include "core/rtc/rtc.h"
#include "core/runtime/runtime_knobs.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

namespace rflow::core::runtime {

rflow_err_t InitInfrastructure(InitFailureStage* out_failure_stage) {
    if (out_failure_stage) *out_failure_stage = InitFailureStage::kNone;

    if (!rflow::thread::initialize()) {
        if (out_failure_stage) *out_failure_stage = InitFailureStage::kThread;
        return RFLOW_ERR_FAIL;
    }
    if (!rflow::rtc::initialize()) {
        rflow::thread::shutdown();
        if (out_failure_stage) *out_failure_stage = InitFailureStage::kRtc;
        return RFLOW_ERR_FAIL;
    }
    if (!rflow::signal::initialize()) {
        rflow::rtc::shutdown();
        rflow::thread::shutdown();
        if (out_failure_stage) *out_failure_stage = InitFailureStage::kSignal;
        return RFLOW_ERR_FAIL;
    }

    // 启动 dump 一次，便于事故复现"当时跑的是什么参数"
    rflow::core::runtime::DumpToLog();

    return RFLOW_OK;
}

void ShutdownInfrastructure() {
    rflow::signal::shutdown();
    rflow::rtc::shutdown();
    rflow::thread::shutdown();
}

}  // namespace rflow::core::runtime
