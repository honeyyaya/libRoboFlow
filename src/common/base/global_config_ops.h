#ifndef __RFLOW_COMMON_BASE_GLOBAL_CONFIG_OPS_H__
#define __RFLOW_COMMON_BASE_GLOBAL_CONFIG_OPS_H__

#include "abi/object_layouts.h"
#include "base/logger.h"

namespace rflow::common::base {

inline bool IsValidGlobalConfigHandle(librflow_global_config_t cfg) {
    return cfg && cfg->magic == rflow::kMagicGlobalConfig;
}

inline void CopyGlobalConfig(librflow_global_config_s& dst, const librflow_global_config_s& src) {
    dst = src;
    dst.magic = rflow::kMagicGlobalConfig;
}

inline void ApplyLogConfigIfPresent(const librflow_global_config_s& cfg) {
    if (!cfg.has_log) return;
    rflow::logger_apply(cfg.log.level, cfg.log.cb, cfg.log.userdata);
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_BASE_GLOBAL_CONFIG_OPS_H__
