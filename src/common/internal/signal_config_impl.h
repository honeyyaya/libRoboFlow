#ifndef __RFLOW_INTERNAL_SIGNAL_CONFIG_IMPL_H__
#define __RFLOW_INTERNAL_SIGNAL_CONFIG_IMPL_H__

#include <string>

#include "handle.h"
#include "rflow/librflow_common.h"

struct librflow_signal_config_s {
    uint32_t            magic;
    std::string         url;
    rflow_signal_mode_t mode;
    uint16_t            direct_port;
};

#endif  // __RFLOW_INTERNAL_SIGNAL_CONFIG_IMPL_H__
