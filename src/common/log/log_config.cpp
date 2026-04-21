/**
 * @file   log_config.cpp
 * @brief  librobrt_log_config_* opaque 对象实现
**/

#include "robrt/librobrt_common.h"

#include "../internal/log_config_impl.h"

#include <new>

extern "C" {

librobrt_log_config_t librobrt_log_config_create(void) {
    auto* c = new (std::nothrow) librobrt_log_config_s();
    if (!c) return nullptr;
    c->magic    = robrt::kMagicLogConfig;
    c->level    = ROBRT_LOG_INFO;
    c->enable   = true;
    c->cb       = nullptr;
    c->userdata = nullptr;
    return c;
}

void librobrt_log_config_destroy(librobrt_log_config_t cfg) {
    if (cfg == nullptr || cfg->magic != robrt::kMagicLogConfig) return;
    cfg->magic = 0;
    delete cfg;
}

robrt_err_t librobrt_log_config_set_level(librobrt_log_config_t cfg, robrt_log_level_t level) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicLogConfig);
    cfg->level = level;
    return ROBRT_OK;
}

robrt_err_t librobrt_log_config_set_enable(librobrt_log_config_t cfg, bool enable) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicLogConfig);
    cfg->enable = enable;
    return ROBRT_OK;
}

robrt_err_t librobrt_log_config_set_callback(librobrt_log_config_t cfg,
                                              librobrt_log_cb_fn   cb,
                                              void*                userdata) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicLogConfig);
    cfg->cb       = cb;
    cfg->userdata = userdata;
    return ROBRT_OK;
}

robrt_log_level_t librobrt_log_config_get_level(librobrt_log_config_t cfg) {
    if (!cfg || cfg->magic != robrt::kMagicLogConfig) return ROBRT_LOG_INFO;
    return cfg->level;
}

bool librobrt_log_config_get_enable(librobrt_log_config_t cfg) {
    if (!cfg || cfg->magic != robrt::kMagicLogConfig) return false;
    return cfg->enable;
}

}  // extern "C"
