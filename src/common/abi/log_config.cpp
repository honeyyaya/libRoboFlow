#include "rflow/librflow_common.h"

#include "abi/object_access_ops.h"
#include "abi/object_layouts.h"

extern "C" {

librflow_log_config_t librflow_log_config_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_log_config_s>(
        rflow::kMagicLogConfig, [](librflow_log_config_s& config_obj) {
            config_obj.level = RFLOW_LOG_INFO;
            config_obj.enable = true;
            config_obj.cb = nullptr;
            config_obj.userdata = nullptr;
        });
}

void librflow_log_config_destroy(librflow_log_config_t config_obj) {
    rflow::common::abi::DestroyMagicObject(config_obj, rflow::kMagicLogConfig);
}

rflow_err_t librflow_log_config_set_level(librflow_log_config_t config_obj, rflow_log_level_t level) {
    RFLOW_SET_FIELD(config_obj, rflow::kMagicLogConfig, level, level);
}

rflow_err_t librflow_log_config_set_enable(librflow_log_config_t config_obj, bool enable) {
    RFLOW_SET_FIELD(config_obj, rflow::kMagicLogConfig, enable, enable);
}

rflow_err_t librflow_log_config_set_callback(librflow_log_config_t config_obj,
                                             librflow_log_cb_fn callback,
                                             void* callback_userdata) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicLogConfig);
    config_obj->cb = callback;
    config_obj->userdata = callback_userdata;
    return RFLOW_OK;
}

rflow_log_level_t librflow_log_config_get_level(librflow_log_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicLogConfig) return RFLOW_LOG_INFO;
    return config_obj->level;
}

bool librflow_log_config_get_enable(librflow_log_config_t config_obj) {
    if (!config_obj || config_obj->magic != rflow::kMagicLogConfig) return false;
    return config_obj->enable;
}

}  // extern "C"
