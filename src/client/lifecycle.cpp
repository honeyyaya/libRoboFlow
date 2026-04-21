/**
 * @file   lifecycle.cpp
 * @brief  set_global_config / init / uninit / connect / disconnect
**/

#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/global_config_impl.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include "core/rtc/rtc.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

namespace robrt::client {

State& state() {
    static State s;
    return s;
}

}  // namespace robrt::client

extern "C" {

robrt_err_t librobrt_set_global_config(librobrt_global_config_t cfg) {
    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::client::LifecycleState::kUninit) {
        robrt::set_last_error("set_global_config must be called before init");
        return ROBRT_ERR_STATE;
    }
    if (!cfg || cfg->magic != robrt::kMagicGlobalConfig) {
        robrt::set_last_error("invalid global_config handle");
        return ROBRT_ERR_PARAM;
    }

    s.global_config = *cfg;
    s.global_config.magic = robrt::kMagicGlobalConfig;

    // 立即应用 log 配置（init 之前日志也可能产生）
    if (s.global_config.has_log) {
        robrt::logger_apply(s.global_config.log.level,
                            s.global_config.log.cb,
                            s.global_config.log.userdata);
    }
    return ROBRT_OK;
}

robrt_err_t librobrt_init(void) {
    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::client::LifecycleState::kUninit) return ROBRT_OK;

    if (!robrt::thread::initialize()) return ROBRT_ERR_FAIL;
    if (!robrt::rtc::initialize())    { robrt::thread::shutdown(); return ROBRT_ERR_FAIL; }
    if (!robrt::signal::initialize()) { robrt::rtc::shutdown(); robrt::thread::shutdown(); return ROBRT_ERR_FAIL; }

    s.lifecycle = robrt::client::LifecycleState::kInited;
    ROBRT_LOGI("librobrt_init OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_uninit(void) {
    auto& s = robrt::client::state();

    // disconnect 内部有独立锁逻辑，此处先脱锁调用，再做全局清理
    librobrt_disconnect();

    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle == robrt::client::LifecycleState::kUninit) return ROBRT_OK;

    s.streams.clear();
    robrt::signal::shutdown();
    robrt::rtc::shutdown();
    robrt::thread::shutdown();

    s.lifecycle = robrt::client::LifecycleState::kUninit;
    ROBRT_LOGI("librobrt_uninit OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_connect(librobrt_connect_info_t info,
                              librobrt_connect_cb_t   cb) {
    if (!info || info->magic != robrt::client::kMagicConnectInfo) return ROBRT_ERR_PARAM;
    if (!cb   || cb->magic   != robrt::client::kMagicConnectCb)   return ROBRT_ERR_PARAM;

    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle == robrt::client::LifecycleState::kUninit) {
        robrt::set_last_error("must call librobrt_init before connect");
        return ROBRT_ERR_STATE;
    }
    if (s.lifecycle == robrt::client::LifecycleState::kConnected ||
        s.lifecycle == robrt::client::LifecycleState::kConnecting) {
        return ROBRT_ERR_STATE;
    }

    s.connect_info   = *info;
    s.connect_cb     = *cb;
    s.has_connect_cb = true;
    s.lifecycle      = robrt::client::LifecycleState::kConnecting;

    // TODO: 通过 core/signal + core/rtc 实际发起连接，此处先直连成功
    s.lifecycle = robrt::client::LifecycleState::kConnected;
    if (s.connect_cb.on_state) {
        s.connect_cb.on_state(ROBRT_CONN_CONNECTED, ROBRT_OK, s.connect_cb.userdata);
    }
    ROBRT_LOGI("librobrt_connect OK (stub)");
    return ROBRT_OK;
}

robrt_err_t librobrt_disconnect(void) {
    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::client::LifecycleState::kConnected &&
        s.lifecycle != robrt::client::LifecycleState::kConnecting) {
        return ROBRT_OK;
    }

    // 幂等强清理：先强制关掉所有 stream
    for (auto& [h, sh] : s.streams) {
        if (sh && sh->cb.on_state) {
            sh->cb.on_state(h, ROBRT_STREAM_CLOSED, ROBRT_OK, sh->cb.userdata);
        }
    }
    s.streams.clear();

    if (s.has_connect_cb && s.connect_cb.on_state) {
        s.connect_cb.on_state(ROBRT_CONN_DISCONNECTED, ROBRT_OK, s.connect_cb.userdata);
    }
    s.has_connect_cb = false;
    s.lifecycle      = robrt::client::LifecycleState::kInited;
    ROBRT_LOGI("librobrt_disconnect OK");
    return ROBRT_OK;
}

}  // extern "C"
