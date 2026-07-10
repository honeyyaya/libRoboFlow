#include "rflow/librflow_common.h"

#include "abi/object_layouts.h"
#include "base/abi_string_copy.h"

#include <new>

extern "C" {

librflow_global_config_t librflow_global_config_create(void) {
    auto* config_obj = new (std::nothrow) librflow_global_config_s();
    if (!config_obj) return nullptr;
    config_obj->magic = rflow::kMagicGlobalConfig;
    config_obj->has_log = false;
    config_obj->has_signal = false;
    config_obj->has_license = false;
    config_obj->region = RFLOW_REGION_CN;
    return config_obj;
}

void librflow_global_config_destroy(librflow_global_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicGlobalConfig) return;
    config_obj->magic = 0;
    delete config_obj;
}

rflow_err_t librflow_global_config_set_log(librflow_global_config_t config_obj,
                                           librflow_log_config_t log_config) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (log_config == nullptr) {
        config_obj->has_log = false;
        return RFLOW_OK;
    }
    if (log_config->magic != rflow::kMagicLogConfig) return RFLOW_ERR_PARAM;
    config_obj->has_log = true;
    config_obj->log = *log_config;
    config_obj->log.magic = rflow::kMagicLogConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_signal(librflow_global_config_t config_obj,
                                              librflow_signal_config_t signal_config) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (signal_config == nullptr) {
        config_obj->has_signal = false;
        return RFLOW_OK;
    }
    if (signal_config->magic != rflow::kMagicSignalConfig) return RFLOW_ERR_PARAM;
    config_obj->has_signal = true;
    config_obj->signal = *signal_config;
    config_obj->signal.magic = rflow::kMagicSignalConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_license(librflow_global_config_t config_obj,
                                               librflow_license_config_t license_config) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (license_config == nullptr) {
        config_obj->has_license = false;
        return RFLOW_OK;
    }
    if (license_config->magic != rflow::kMagicLicenseConfig) return RFLOW_ERR_PARAM;
    config_obj->has_license = true;
    config_obj->license = *license_config;
    config_obj->license.magic = rflow::kMagicLicenseConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_config_path(librflow_global_config_t config_obj,
                                                   const char* path) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    config_obj->config_path = path ? path : "";
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_region(librflow_global_config_t config_obj,
                                              rflow_region_t region) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    config_obj->region = region;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_flexfec(librflow_global_config_t config_obj,
                                               rflow_global_flexfec_t mode) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (mode != RFLOW_GLOBAL_FLEXFEC_DEFAULT && mode != RFLOW_GLOBAL_FLEXFEC_OFF &&
        mode != RFLOW_GLOBAL_FLEXFEC_ON) {
        return RFLOW_ERR_PARAM;
    }
    config_obj->flexfec = mode;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_get_flexfec(librflow_global_config_t config_obj,
                                               rflow_global_flexfec_t *out_mode) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (!out_mode) {
        return RFLOW_ERR_PARAM;
    }
    *out_mode = config_obj->flexfec;
    return RFLOW_OK;
}

namespace {

constexpr uint32_t kIceIgnoreInterfacesMaxLen = 255u;

}  // namespace

rflow_err_t librflow_global_config_set_ice_ignore_interfaces(librflow_global_config_t config_obj,
                                                             const char* interfaces_csv) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (!interfaces_csv || interfaces_csv[0] == '\0') {
        config_obj->has_ice_ignore_interfaces = false;
        config_obj->ice_ignore_interfaces.clear();
        return RFLOW_OK;
    }
    const std::string csv(interfaces_csv);
    if (csv.size() > kIceIgnoreInterfacesMaxLen) {
        return RFLOW_ERR_PARAM;
    }
    config_obj->has_ice_ignore_interfaces = true;
    config_obj->ice_ignore_interfaces     = csv;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_get_ice_ignore_interfaces(librflow_global_config_t config_obj,
                                                             char* buf,
                                                             uint32_t buf_len,
                                                             uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (!config_obj->has_ice_ignore_interfaces) {
        return RFLOW_ERR_NOT_FOUND;
    }
    return rflow::common::base::CopyOutString(config_obj->ice_ignore_interfaces, buf, buf_len,
                                              out_needed);
}

}  // extern "C"
