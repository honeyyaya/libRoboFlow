#include "rtc.h"

#include "core/base/logging.h"

namespace rflow::rtc {

bool initialize() {
    RFLOW_CORE_LOGI("rtc::initialize (stub)");
    return true;
}

void shutdown() {
    RFLOW_CORE_LOGI("rtc::shutdown (stub)");
}

}  // namespace rflow::rtc
