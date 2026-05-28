#ifndef __RFLOW_COMMON_ABI_OBJECT_LAYOUTS_H__
#define __RFLOW_COMMON_ABI_OBJECT_LAYOUTS_H__

#include <string>
#include <vector>

#include "abi/handle.h"
#include "rflow/librflow_common.h"

struct librflow_log_config_s {
    uint32_t magic;
    rflow_log_level_t level;
    bool enable;
    librflow_log_cb_fn cb;
    void* userdata;
};

struct librflow_signal_config_s {
    uint32_t magic;
    std::string url;
    rflow_signal_mode_t mode;
    uint16_t direct_port;
};

struct librflow_license_config_s {
    uint32_t magic;
    std::string file_path;      // 二选一
    std::vector<uint8_t> buffer;  // 二选一
};

/**
 * global_config 内部持有子配置的 shallow copy。
 * 由于子配置 opaque 可能还被上层持有，我们将子配置内容按值复制存储，
 * 避免出现上层 destroy 子配置导致悬挂的问题。
 */
struct librflow_global_config_s {
    uint32_t magic;
    bool has_log;
    librflow_log_config_s log;

    bool has_signal;
    librflow_signal_config_s signal;

    bool has_license;
    librflow_license_config_s license;

    std::string config_path;
    rflow_region_t region;

    rflow_global_flexfec_t flexfec{RFLOW_GLOBAL_FLEXFEC_DEFAULT};
};

#endif  // __RFLOW_COMMON_ABI_OBJECT_LAYOUTS_H__
