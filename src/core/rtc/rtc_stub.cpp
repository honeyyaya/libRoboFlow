#include "rtc.h"

#include "common/internal/logger.h"

namespace robrt::rtc {

bool initialize() {
    ROBRT_LOGI("rtc::initialize (stub)");
    return true;
}

void shutdown() {
    ROBRT_LOGI("rtc::shutdown (stub)");
}

}  // namespace robrt::rtc
