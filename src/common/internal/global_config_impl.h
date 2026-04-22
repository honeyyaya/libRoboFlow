#ifndef __RFLOW_INTERNAL_GLOBAL_CONFIG_IMPL_H__
#define __RFLOW_INTERNAL_GLOBAL_CONFIG_IMPL_H__

#include <string>

#include "handle.h"
#include "log_config_impl.h"
#include "signal_config_impl.h"
#include "license_config_impl.h"

/**
 * global_config 内部持有子配置的 shallow copy。
 * 由于子配置 opaque 可能还被上层持有，我们将子配置内容按值复制存储，
 * 避免出现上层 destroy 子配置导致悬挂的问题。
 */
struct librflow_global_config_s {
    uint32_t                 magic;
    bool                     has_log;
    librflow_log_config_s    log;

    bool                     has_signal;
    librflow_signal_config_s signal;

    bool                     has_license;
    librflow_license_config_s license;

    std::string              config_path;
    rflow_region_t           region;
};

#endif  // __RFLOW_INTERNAL_GLOBAL_CONFIG_IMPL_H__
