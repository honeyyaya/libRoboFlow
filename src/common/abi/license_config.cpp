#include "rflow/librflow_common.h"

#include "abi/object_access_ops.h"
#include "abi/object_layouts.h"

extern "C" {

librflow_license_config_t librflow_license_config_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_license_config_s>(rflow::kMagicLicenseConfig);
}

void librflow_license_config_destroy(librflow_license_config_t config_obj) {
    rflow::common::abi::DestroyMagicObject(config_obj, rflow::kMagicLicenseConfig);
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
