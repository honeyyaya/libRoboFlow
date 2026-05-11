#ifndef __RFLOW_CORE_BASE_LOGGING_H__
#define __RFLOW_CORE_BASE_LOGGING_H__

#include "public/logger_api.h"

// Core layer logging facade: keep core-side call sites decoupled from
// common logger naming/details.
#define RFLOW_CORE_LOGD(...) RFLOW_LOGD(__VA_ARGS__)
#define RFLOW_CORE_LOGI(...) RFLOW_LOGI(__VA_ARGS__)
#define RFLOW_CORE_LOGW(...) RFLOW_LOGW(__VA_ARGS__)
#define RFLOW_CORE_LOGE(...) RFLOW_LOGE(__VA_ARGS__)

#endif  // __RFLOW_CORE_BASE_LOGGING_H__
