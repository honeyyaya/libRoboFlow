#include "rflow/librflow_common.h"

#include "common/abi/object_layouts.h"

#include <new>

extern "C" {

librflow_license_config_t librflow_license_config_create(void) {
    auto* config_obj = new (std::nothrow) librflow_license_config_s();
    if (!config_obj) return nullptr;
    config_obj->magic = rflow::kMagicLicenseConfig;
    return config_obj;
}

void librflow_license_config_destroy(librflow_license_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicLicenseConfig) return;
    config_obj->magic = 0;
    delete config_obj;
}

rflow_err_t librflow_license_config_set_file(librflow_license_config_t config_obj, const char* path) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicLicenseConfig);
    config_obj->file_path = path ? path : "";
    config_obj->buffer.clear();
    return RFLOW_OK;
}

rflow_err_t librflow_license_config_set_buffer(librflow_license_config_t config_obj,
                                               const void* data,
                                               uint32_t len) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicLicenseConfig);
    if ((data == nullptr) != (len == 0)) return RFLOW_ERR_PARAM;
    const auto* byte_data = static_cast<const uint8_t*>(data);
    config_obj->buffer.assign(byte_data, byte_data + len);
    config_obj->file_path.clear();
    return RFLOW_OK;
}

}  // extern "C"
