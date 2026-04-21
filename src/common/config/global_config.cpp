#include "robrt/librobrt_common.h"

#include "../internal/global_config_impl.h"

#include <new>

extern "C" {

librobrt_global_config_t librobrt_global_config_create(void) {
    auto* c = new (std::nothrow) librobrt_global_config_s();
    if (!c) return nullptr;
    c->magic       = robrt::kMagicGlobalConfig;
    c->has_log     = false;
    c->has_signal  = false;
    c->has_license = false;
    c->region      = ROBRT_REGION_CN;
    return c;
}

void librobrt_global_config_destroy(librobrt_global_config_t cfg) {
    if (!cfg || cfg->magic != robrt::kMagicGlobalConfig) return;
    cfg->magic = 0;
    delete cfg;
}

robrt_err_t librobrt_global_config_set_log(librobrt_global_config_t cfg,
                                            librobrt_log_config_t    log) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicGlobalConfig);
    if (log == nullptr) {
        cfg->has_log = false;
        return ROBRT_OK;
    }
    if (log->magic != robrt::kMagicLogConfig) return ROBRT_ERR_PARAM;
    cfg->has_log = true;
    cfg->log    = *log;
    cfg->log.magic = robrt::kMagicLogConfig;
    return ROBRT_OK;
}

robrt_err_t librobrt_global_config_set_signal(librobrt_global_config_t cfg,
                                               librobrt_signal_config_t sig) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicGlobalConfig);
    if (sig == nullptr) {
        cfg->has_signal = false;
        return ROBRT_OK;
    }
    if (sig->magic != robrt::kMagicSignalConfig) return ROBRT_ERR_PARAM;
    cfg->has_signal = true;
    cfg->signal    = *sig;
    cfg->signal.magic = robrt::kMagicSignalConfig;
    return ROBRT_OK;
}

robrt_err_t librobrt_global_config_set_license(librobrt_global_config_t cfg,
                                                librobrt_license_config_t lic) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicGlobalConfig);
    if (lic == nullptr) {
        cfg->has_license = false;
        return ROBRT_OK;
    }
    if (lic->magic != robrt::kMagicLicenseConfig) return ROBRT_ERR_PARAM;
    cfg->has_license = true;
    cfg->license    = *lic;
    cfg->license.magic = robrt::kMagicLicenseConfig;
    return ROBRT_OK;
}

robrt_err_t librobrt_global_config_set_config_path(librobrt_global_config_t cfg,
                                                    const char* path) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicGlobalConfig);
    cfg->config_path = path ? path : "";
    return ROBRT_OK;
}

robrt_err_t librobrt_global_config_set_region(librobrt_global_config_t cfg,
                                               robrt_region_t region) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicGlobalConfig);
    cfg->region = region;
    return ROBRT_OK;
}

}  // extern "C"
