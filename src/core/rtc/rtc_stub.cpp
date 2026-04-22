#include "rtc.h"

#include "common/internal/logger.h"

namespace rflow::rtc {

bool initialize() {
    RFLOW_LOGI("rtc::initialize (stub)");
    return true;
}

void shutdown() {
    RFLOW_LOGI("rtc::shutdown (stub)");
}

}  // namespace rflow::rtc
