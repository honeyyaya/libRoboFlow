#ifndef __RFLOW_INTERNAL_LOGGER_H__
#define __RFLOW_INTERNAL_LOGGER_H__

#include "rflow/librflow_common.h"

namespace rflow {

void logger_apply       (rflow_log_level_t level, librflow_log_cb_fn cb, void* ud);
void logger_set_level   (rflow_log_level_t level);
void logger_set_callback(librflow_log_cb_fn cb, void* ud);

void logf(rflow_log_level_t level, const char* fmt, ...);

}  // namespace rflow

#define RFLOW_LOGT(fmt, ...) ::rflow::logf(RFLOW_LOG_TRACE, fmt, ##__VA_ARGS__)
#define RFLOW_LOGD(fmt, ...) ::rflow::logf(RFLOW_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define RFLOW_LOGI(fmt, ...) ::rflow::logf(RFLOW_LOG_INFO,  fmt, ##__VA_ARGS__)
#define RFLOW_LOGW(fmt, ...) ::rflow::logf(RFLOW_LOG_WARN,  fmt, ##__VA_ARGS__)
#define RFLOW_LOGE(fmt, ...) ::rflow::logf(RFLOW_LOG_ERROR, fmt, ##__VA_ARGS__)
#define RFLOW_LOGF(fmt, ...) ::rflow::logf(RFLOW_LOG_FATAL, fmt, ##__VA_ARGS__)

#endif  // __RFLOW_INTERNAL_LOGGER_H__
