#ifndef __RFLOW_INTERNAL_LOG_CONFIG_IMPL_H__
#define __RFLOW_INTERNAL_LOG_CONFIG_IMPL_H__

#include "handle.h"
#include "rflow/librflow_common.h"

struct librflow_log_config_s {
    uint32_t           magic;
    rflow_log_level_t  level;
    bool               enable;
    librflow_log_cb_fn cb;
    void*              userdata;
};

#endif  // __RFLOW_INTERNAL_LOG_CONFIG_IMPL_H__
