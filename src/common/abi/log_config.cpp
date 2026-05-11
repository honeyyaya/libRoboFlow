#include "rflow/librflow_common.h"

#include "abi/object_layouts.h"

#include <new>

extern "C" {

librflow_log_config_t librflow_log_config_create(void) {
    auto* config_obj = new (std::nothrow) librflow_log_config_s();
    if (!config_obj) return nullptr;
    config_obj->magic = rflow::kMagicLogConfig;
    config_obj->level = RFLOW_LOG_INFO;
    config_obj->enable = true;
    config_obj->cb = nullptr;
    config_obj->userdata = nullptr;
    return config_obj;
}

void librflow_log_config_destroy(librflow_log_config_t config_obj) {
    if (config_obj == nullptr || config_obj->magic != rflow::kMagicLogConfig) return;
    config_obj->magic = 0;
    delete config_obj;
}

rflow_err_t librflow_log_config_set_level(librflow_log_config_t config_obj, rflow_log_level_t level) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicLogConfig);
    config_obj->level = level;
    return RFLOW_OK;
}

rflow_err_t librflow_log_config_set_enable(librflow_log_config_t config_obj, bool enable) {
    RFLOW_CHECK_HANDLE(config_obj, rflow::kMagicLogConfig);
    config_obj->enable = enable;
    return RFLOW_OK;
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
