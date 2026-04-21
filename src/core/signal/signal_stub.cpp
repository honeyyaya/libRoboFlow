#include "signal.h"

#include "common/internal/logger.h"

namespace robrt::signal {

bool initialize() {
    ROBRT_LOGI("signal::initialize (stub)");
    return true;
}

void shutdown() {
    ROBRT_LOGI("signal::shutdown (stub)");
}

}  // namespace robrt::signal
