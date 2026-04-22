#include "rflow/librflow_common.h"

#include "../internal/license_config_impl.h"

#include <new>

extern "C" {

librflow_license_config_t librflow_license_config_create(void) {
    auto* c = new (std::nothrow) librflow_license_config_s();
    if (!c) return nullptr;
    c->magic = rflow::kMagicLicenseConfig;
    return c;
}

void librflow_license_config_destroy(librflow_license_config_t cfg) {
    if (!cfg || cfg->magic != rflow::kMagicLicenseConfig) return;
    cfg->magic = 0;
    delete cfg;
}

rflow_err_t librflow_license_config_set_file(librflow_license_config_t cfg, const char* path) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicLicenseConfig);
    cfg->file_path = path ? path : "";
    cfg->buffer.clear();
    return RFLOW_OK;
}

rflow_err_t librflow_license_config_set_buffer(librflow_license_config_t cfg,
                                                const void* data,
                                                uint32_t    len) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicLicenseConfig);
    if ((data == nullptr) != (len == 0)) return RFLOW_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    cfg->buffer.assign(p, p + len);
    cfg->file_path.clear();
    return RFLOW_OK;
}

}  // extern "C"
