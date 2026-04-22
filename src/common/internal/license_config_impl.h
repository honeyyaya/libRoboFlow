#ifndef __RFLOW_INTERNAL_LICENSE_CONFIG_IMPL_H__
#define __RFLOW_INTERNAL_LICENSE_CONFIG_IMPL_H__

#include <string>
#include <vector>

#include "handle.h"

struct librflow_license_config_s {
    uint32_t             magic;
    std::string          file_path;     // 二选一
    std::vector<uint8_t> buffer;        // 二选一
};

#endif  // __RFLOW_INTERNAL_LICENSE_CONFIG_IMPL_H__
