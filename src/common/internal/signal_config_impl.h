#ifndef __ROBRT_INTERNAL_SIGNAL_CONFIG_IMPL_H__
#define __ROBRT_INTERNAL_SIGNAL_CONFIG_IMPL_H__

#include <string>

#include "handle.h"
#include "robrt/librobrt_common.h"

struct librobrt_signal_config_s {
    uint32_t            magic;
    std::string         url;
    robrt_signal_mode_t mode;
    uint16_t            direct_port;
};

#endif  // __ROBRT_INTERNAL_SIGNAL_CONFIG_IMPL_H__
