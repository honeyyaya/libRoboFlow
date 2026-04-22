#include "rflow/librflow_common.h"

#include "../internal/global_config_impl.h"

#include <new>

extern "C" {

librflow_global_config_t librflow_global_config_create(void) {
    auto* c = new (std::nothrow) librflow_global_config_s();
    if (!c) return nullptr;
    c->magic       = rflow::kMagicGlobalConfig;
    c->has_log     = false;
    c->has_signal  = false;
    c->has_license = false;
    c->region      = RFLOW_REGION_CN;
    return c;
}

void librflow_global_config_destroy(librflow_global_config_t cfg) {
    if (!cfg || cfg->magic != rflow::kMagicGlobalConfig) return;
    cfg->magic = 0;
    delete cfg;
}

rflow_err_t librflow_global_config_set_log(librflow_global_config_t cfg,
                                            librflow_log_config_t    log) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicGlobalConfig);
    if (log == nullptr) {
        cfg->has_log = false;
        return RFLOW_OK;
    }
    if (log->magic != rflow::kMagicLogConfig) return RFLOW_ERR_PARAM;
    cfg->has_log = true;
    cfg->log    = *log;
    cfg->log.magic = rflow::kMagicLogConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_signal(librflow_global_config_t cfg,
                                               librflow_signal_config_t sig) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicGlobalConfig);
    if (sig == nullptr) {
        cfg->has_signal = false;
        return RFLOW_OK;
    }
    if (sig->magic != rflow::kMagicSignalConfig) return RFLOW_ERR_PARAM;
    cfg->has_signal = true;
    cfg->signal    = *sig;
    cfg->signal.magic = rflow::kMagicSignalConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_license(librflow_global_config_t cfg,
                                                librflow_license_config_t lic) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicGlobalConfig);
    if (lic == nullptr) {
        cfg->has_license = false;
        return RFLOW_OK;
    }
    if (lic->magic != rflow::kMagicLicenseConfig) return RFLOW_ERR_PARAM;
    cfg->has_license = true;
    cfg->license    = *lic;
    cfg->license.magic = rflow::kMagicLicenseConfig;
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_config_path(librflow_global_config_t cfg,
                                                    const char* path) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicGlobalConfig);
    cfg->config_path = path ? path : "";
    return RFLOW_OK;
}

rflow_err_t librflow_global_config_set_region(librflow_global_config_t cfg,
                                               rflow_region_t region) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicGlobalConfig);
    cfg->region = region;
    return RFLOW_OK;
}

}  // extern "C"
