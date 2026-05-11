#include "rflow/librflow_common.h"

#include "abi/object_layouts.h"
#include "base/abi_string_copy.h"

#include <new>

extern "C" {

librflow_signal_config_t librflow_signal_config_create(void) {
    auto* config_obj = new (std::nothrow) librflow_signal_config_s();
    if (!config_obj) return nullptr;
    config_obj->magic = rflow::kMagicSignalConfig;
    config_obj->mode = RFLOW_SIGNAL_MODE_SERVER;
    config_obj->direct_port = 0;
    return config_obj;
}

void librflow_signal_config_destroy(librflow_signal_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicSignalConfig) return;
    config_obj->magic = 0;
    delete config_obj;
}

rflow_err_t librflow_signal_config_set_url(librflow_signal_config_t config_obj, const char* url) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicSignalConfig);
    config_obj->url = url ? url : "";
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_set_mode(librflow_signal_config_t config_obj, rflow_signal_mode_t mode) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicSignalConfig);
    config_obj->mode = mode;
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_set_direct_port(librflow_signal_config_t config_obj, uint16_t port) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicSignalConfig);
    config_obj->direct_port = port;
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_get_url(librflow_signal_config_t config_obj,
                                           char* buffer,
                                           uint32_t buffer_len,
                                           uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicSignalConfig);
    return rflow::common::base::CopyOutString(config_obj->url, buffer, buffer_len, out_needed);
}

}  // extern "C"
