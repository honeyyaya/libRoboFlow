#ifndef __ROBRT_INTERNAL_LOGGER_H__
#define __ROBRT_INTERNAL_LOGGER_H__

#include "robrt/librobrt_common.h"

namespace robrt {

void logger_apply       (robrt_log_level_t level, librobrt_log_cb_fn cb, void* ud);
void logger_set_level   (robrt_log_level_t level);
void logger_set_callback(librobrt_log_cb_fn cb, void* ud);

void logf(robrt_log_level_t level, const char* fmt, ...);

}  // namespace robrt

#define ROBRT_LOGT(fmt, ...) ::robrt::logf(ROBRT_LOG_TRACE, fmt, ##__VA_ARGS__)
#define ROBRT_LOGD(fmt, ...) ::robrt::logf(ROBRT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define ROBRT_LOGI(fmt, ...) ::robrt::logf(ROBRT_LOG_INFO,  fmt, ##__VA_ARGS__)
#define ROBRT_LOGW(fmt, ...) ::robrt::logf(ROBRT_LOG_WARN,  fmt, ##__VA_ARGS__)
#define ROBRT_LOGE(fmt, ...) ::robrt::logf(ROBRT_LOG_ERROR, fmt, ##__VA_ARGS__)
#define ROBRT_LOGF(fmt, ...) ::robrt::logf(ROBRT_LOG_FATAL, fmt, ##__VA_ARGS__)

#endif  // __ROBRT_INTERNAL_LOGGER_H__
