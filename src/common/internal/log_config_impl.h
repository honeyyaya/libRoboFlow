#ifndef __ROBRT_INTERNAL_LOG_CONFIG_IMPL_H__
#define __ROBRT_INTERNAL_LOG_CONFIG_IMPL_H__

#include "handle.h"
#include "robrt/librobrt_common.h"

struct librobrt_log_config_s {
    uint32_t           magic;
    robrt_log_level_t  level;
    bool               enable;
    librobrt_log_cb_fn cb;
    void*              userdata;
};

#endif  // __ROBRT_INTERNAL_LOG_CONFIG_IMPL_H__
