#include "robrt/librobrt_common.h"

#include "../internal/license_config_impl.h"

#include <new>

extern "C" {

librobrt_license_config_t librobrt_license_config_create(void) {
    auto* c = new (std::nothrow) librobrt_license_config_s();
    if (!c) return nullptr;
    c->magic = robrt::kMagicLicenseConfig;
    return c;
}

void librobrt_license_config_destroy(librobrt_license_config_t cfg) {
    if (!cfg || cfg->magic != robrt::kMagicLicenseConfig) return;
    cfg->magic = 0;
    delete cfg;
}

robrt_err_t librobrt_license_config_set_file(librobrt_license_config_t cfg, const char* path) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicLicenseConfig);
    cfg->file_path = path ? path : "";
    cfg->buffer.clear();
    return ROBRT_OK;
}

robrt_err_t librobrt_license_config_set_buffer(librobrt_license_config_t cfg,
                                                const void* data,
                                                uint32_t    len) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicLicenseConfig);
    if ((data == nullptr) != (len == 0)) return ROBRT_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    cfg->buffer.assign(p, p + len);
    cfg->file_path.clear();
    return ROBRT_OK;
}

}  // extern "C"
