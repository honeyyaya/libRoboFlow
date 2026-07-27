#include "rflow/librflow_common.h"

#include "abi/object_access_ops.h"
#include "abi/object_layouts.h"
#include "base/abi_string_copy.h"

extern "C" {

librflow_signal_config_t librflow_signal_config_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_signal_config_s>(
        rflow::kMagicSignalConfig, [](librflow_signal_config_s& config_obj) {
            config_obj.mode = RFLOW_SIGNAL_MODE_SERVER;
            config_obj.direct_port = 0;
        });
}

void librflow_signal_config_destroy(librflow_signal_config_t config_obj) {
    rflow::common::abi::DestroyMagicObject(config_obj, rflow::kMagicSignalConfig);
}

rflow_err_t librflow_signal_config_set_url(librflow_signal_config_t config_obj, const char* url) {
    RFLOW_SET_FIELD(config_obj, rflow::kMagicSignalConfig, url, url ? url : "");
}

rflow_err_t librflow_signal_config_set_mode(librflow_signal_config_t config_obj, rflow_signal_mode_t mode) {
    RFLOW_SET_FIELD(config_obj, rflow::kMagicSignalConfig, mode, mode);
}

rflow_err_t librflow_signal_config_set_direct_port(librflow_signal_config_t config_obj, uint16_t port) {
    RFLOW_SET_FIELD(config_obj, rflow::kMagicSignalConfig, direct_port, port);
}

rflow_err_t librflow_signal_config_get_url(librflow_signal_config_t config_obj,
                                           char* buffer,
                                           uint32_t buffer_len,
                                           uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicSignalConfig);
    return rflow::common::base::CopyOutString(config_obj->url, buffer, buffer_len, out_needed);
}

}  // extern "C"
