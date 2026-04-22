/**
 * @file   lifecycle.cpp
 * @brief  set_global_config / init / uninit / connect / disconnect
**/

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/global_config_impl.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include "core/rtc/rtc.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

namespace rflow::client {

State& state() {
    static State s;
    return s;
}

}  // namespace rflow::client

extern "C" {

rflow_err_t librflow_set_global_config(librflow_global_config_t cfg) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kUninit) {
        rflow::set_last_error("set_global_config must be called before init");
        return RFLOW_ERR_STATE;
    }
    if (!cfg || cfg->magic != rflow::kMagicGlobalConfig) {
        rflow::set_last_error("invalid global_config handle");
        return RFLOW_ERR_PARAM;
    }

    s.global_config = *cfg;
    s.global_config.magic = rflow::kMagicGlobalConfig;

    // 立即应用 log 配置（init 之前日志也可能产生）
    if (s.global_config.has_log) {
        rflow::logger_apply(s.global_config.log.level,
                            s.global_config.log.cb,
                            s.global_config.log.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t librflow_init(void) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kUninit) return RFLOW_OK;

    if (!rflow::thread::initialize()) return RFLOW_ERR_FAIL;
    if (!rflow::rtc::initialize())    { rflow::thread::shutdown(); return RFLOW_ERR_FAIL; }
    if (!rflow::signal::initialize()) { rflow::rtc::shutdown(); rflow::thread::shutdown(); return RFLOW_ERR_FAIL; }

    s.lifecycle = rflow::client::LifecycleState::kInited;
    RFLOW_LOGI("librflow_init OK");
    return RFLOW_OK;
}

rflow_err_t librflow_uninit(void) {
    auto& s = rflow::client::state();

    // disconnect 内部有独立锁逻辑，此处先脱锁调用，再做全局清理
    librflow_disconnect();

    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle == rflow::client::LifecycleState::kUninit) return RFLOW_OK;

    s.streams.clear();
    rflow::signal::shutdown();
    rflow::rtc::shutdown();
    rflow::thread::shutdown();

    s.lifecycle = rflow::client::LifecycleState::kUninit;
    RFLOW_LOGI("librflow_uninit OK");
    return RFLOW_OK;
}

rflow_err_t librflow_connect(librflow_connect_info_t info,
                              librflow_connect_cb_t   cb) {
    if (!info || info->magic != rflow::client::kMagicConnectInfo) return RFLOW_ERR_PARAM;
    if (!cb   || cb->magic   != rflow::client::kMagicConnectCb)   return RFLOW_ERR_PARAM;

    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle == rflow::client::LifecycleState::kUninit) {
        rflow::set_last_error("must call librflow_init before connect");
        return RFLOW_ERR_STATE;
    }
    if (s.lifecycle == rflow::client::LifecycleState::kConnected ||
        s.lifecycle == rflow::client::LifecycleState::kConnecting) {
        return RFLOW_ERR_STATE;
    }

    s.connect_info   = *info;
    s.connect_cb     = *cb;
    s.has_connect_cb = true;
    s.lifecycle      = rflow::client::LifecycleState::kConnecting;

    // TODO: 通过 core/signal + core/rtc 实际发起连接，此处先直连成功
    s.lifecycle = rflow::client::LifecycleState::kConnected;
    if (s.connect_cb.on_state) {
        s.connect_cb.on_state(RFLOW_CONN_CONNECTED, RFLOW_OK, s.connect_cb.userdata);
    }
    RFLOW_LOGI("librflow_connect OK (stub)");
    return RFLOW_OK;
}

rflow_err_t librflow_disconnect(void) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kConnected &&
        s.lifecycle != rflow::client::LifecycleState::kConnecting) {
        return RFLOW_OK;
    }

    // 幂等强清理：先强制关掉所有 stream
    for (auto& [h, sh] : s.streams) {
        if (sh && sh->cb.on_state) {
            sh->cb.on_state(h, RFLOW_STREAM_CLOSED, RFLOW_OK, sh->cb.userdata);
        }
    }
    s.streams.clear();

    if (s.has_connect_cb && s.connect_cb.on_state) {
        s.connect_cb.on_state(RFLOW_CONN_DISCONNECTED, RFLOW_OK, s.connect_cb.userdata);
    }
    s.has_connect_cb = false;
    s.lifecycle      = rflow::client::LifecycleState::kInited;
    RFLOW_LOGI("librflow_disconnect OK");
    return RFLOW_OK;
}

}  // extern "C"
