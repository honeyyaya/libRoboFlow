#include "signal/signal.h"

#include "common/internal/logger.h"

namespace rflow::signal {

bool initialize() {
    RFLOW_LOGI("signal::initialize (stub)");
    return true;
}

void shutdown() {
    RFLOW_LOGI("signal::shutdown (stub)");
}

}  // namespace rflow::signal
