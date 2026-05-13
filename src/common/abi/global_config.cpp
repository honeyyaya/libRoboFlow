#include "rflow/librflow_common.h"

#include "abi/object_layouts.h"

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

rflow_err_t librflow_global_config_set_enable_flexfec(librflow_global_config_t config_obj,
                                                      bool enabled) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    config_obj->has_enable_flexfec = true;
    config_obj->enable_flexfec     = enabled;
    return RFLOW_OK;
}

void librflow_global_config_reset_enable_flexfec(librflow_global_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicGlobalConfig) {
        return;
    }
    config_obj->has_enable_flexfec = false;
    config_obj->enable_flexfec     = true;
}

rflow_err_t librflow_global_config_get_enable_flexfec(librflow_global_config_t config_obj,
                                                      bool *out_explicit,
                                                      bool *out_enabled) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicGlobalConfig);
    if (!out_explicit) {
        return RFLOW_ERR_PARAM;
    }
    *out_explicit = config_obj->has_enable_flexfec;
    if (out_enabled && config_obj->has_enable_flexfec) {
        *out_enabled = config_obj->enable_flexfec;
    }
    return RFLOW_OK;
}

}  // extern "C"
