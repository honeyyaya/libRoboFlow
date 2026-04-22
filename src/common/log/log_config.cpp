/**
 * @file   log_config.cpp
 * @brief  librflow_log_config_* opaque 对象实现
**/

#include "rflow/librflow_common.h"

#include "../internal/log_config_impl.h"

#include <new>

extern "C" {

librflow_log_config_t librflow_log_config_create(void) {
    auto* c = new (std::nothrow) librflow_log_config_s();
    if (!c) return nullptr;
    c->magic    = rflow::kMagicLogConfig;
    c->level    = RFLOW_LOG_INFO;
    c->enable   = true;
    c->cb       = nullptr;
    c->userdata = nullptr;
    return c;
}

void librflow_log_config_destroy(librflow_log_config_t cfg) {
    if (cfg == nullptr || cfg->magic != rflow::kMagicLogConfig) return;
    cfg->magic = 0;
    delete cfg;
}

rflow_err_t librflow_log_config_set_level(librflow_log_config_t cfg, rflow_log_level_t level) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicLogConfig);
    cfg->level = level;
    return RFLOW_OK;
}

rflow_err_t librflow_log_config_set_enable(librflow_log_config_t cfg, bool enable) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicLogConfig);
    cfg->enable = enable;
    return RFLOW_OK;
}

rflow_err_t librflow_log_config_set_callback(librflow_log_config_t cfg,
                                              librflow_log_cb_fn   cb,
                                              void*                userdata) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicLogConfig);
    cfg->cb       = cb;
    cfg->userdata = userdata;
    return RFLOW_OK;
}

rflow_log_level_t librflow_log_config_get_level(librflow_log_config_t cfg) {
    if (!cfg || cfg->magic != rflow::kMagicLogConfig) return RFLOW_LOG_INFO;
    return cfg->level;
}

bool librflow_log_config_get_enable(librflow_log_config_t cfg) {
    if (!cfg || cfg->magic != rflow::kMagicLogConfig) return false;
    return cfg->enable;
}

}  // extern "C"
