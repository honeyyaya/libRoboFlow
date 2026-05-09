#include "signal/signal.h"

#include "core/base/logging.h"

namespace rflow::signal {

bool initialize() {
    RFLOW_CORE_LOGI("signal::initialize (stub)");
    return true;
}

void shutdown() {
    RFLOW_CORE_LOGI("signal::shutdown (stub)");
}

}  // namespace rflow::signal
