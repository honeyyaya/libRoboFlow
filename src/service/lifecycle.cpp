/**
 * @file   lifecycle.cpp
 * @brief  Service 端生命周期：set_global_config /
 *         init / uninit / connect / disconnect / get_license_info
**/

#include "rflow/Service/librflow_service_api.h"
#include "rflow/librflow_common.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/global_config_impl.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include "core/rtc/rtc.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

#include <new>

namespace rflow::service {

State& state() {
    static State s;
    return s;
}

}  // namespace rflow::service

extern "C" {

rflow_err_t librflow_svc_set_global_config(librflow_global_config_t cfg) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::service::LifecycleState::kUninit) {
        rflow::set_last_error("svc_set_global_config must be called before init");
        return RFLOW_ERR_STATE;
    }
    if (!cfg || cfg->magic != rflow::kMagicGlobalConfig) return RFLOW_ERR_PARAM;

    s.global_config = *cfg;
    s.global_config.magic = rflow::kMagicGlobalConfig;

    if (s.global_config.has_log) {
        rflow::logger_apply(s.global_config.log.level,
                            s.global_config.log.cb,
                            s.global_config.log.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t librflow_svc_init(void) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::service::LifecycleState::kUninit) return RFLOW_OK;

    if (!rflow::thread::initialize()) return RFLOW_ERR_FAIL;
    if (!rflow::rtc::initialize())    { rflow::thread::shutdown(); return RFLOW_ERR_FAIL; }
    if (!rflow::signal::initialize()) { rflow::rtc::shutdown(); rflow::thread::shutdown(); return RFLOW_ERR_FAIL; }

    s.lifecycle = rflow::service::LifecycleState::kInited;
    RFLOW_LOGI("librflow_svc_init OK");
    return RFLOW_OK;
}

rflow_err_t librflow_svc_uninit(void) {
    librflow_svc_disconnect();

    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle == rflow::service::LifecycleState::kUninit) return RFLOW_OK;

    s.streams.clear();
    rflow::signal::shutdown();
    rflow::rtc::shutdown();
    rflow::thread::shutdown();

    s.lifecycle = rflow::service::LifecycleState::kUninit;
    RFLOW_LOGI("librflow_svc_uninit OK");
    return RFLOW_OK;
}

rflow_err_t librflow_svc_connect(librflow_svc_connect_info_t info,
                                  librflow_svc_connect_cb_t   cb) {
    if (!info || info->magic != rflow::service::kMagicConnectInfo) return RFLOW_ERR_PARAM;
    if (!cb   || cb->magic   != rflow::service::kMagicConnectCb)   return RFLOW_ERR_PARAM;

    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle == rflow::service::LifecycleState::kUninit) return RFLOW_ERR_STATE;
    if (s.lifecycle == rflow::service::LifecycleState::kConnected ||
        s.lifecycle == rflow::service::LifecycleState::kConnecting) {
        return RFLOW_ERR_STATE;
    }

    s.connect_info   = *info;
    if (s.connect_info.device_id.empty()) {
        s.connect_info.device_id = RFLOW_DEFAULT_DEVICE_ID;
    }
    s.connect_cb     = *cb;
    s.has_connect_cb = true;
    s.lifecycle      = rflow::service::LifecycleState::kConnecting;

    // TODO: 实际向云端注册 + 鉴权 + license 校验；此处直连成功
    s.lifecycle = rflow::service::LifecycleState::kConnected;
    if (s.connect_cb.on_state) {
        s.connect_cb.on_state(RFLOW_CONN_CONNECTED, RFLOW_OK, s.connect_cb.userdata);
    }
    if (s.connect_cb.on_bind_state) {
        s.connect_cb.on_bind_state(RFLOW_BIND_BOUND, nullptr, s.connect_cb.userdata);
    }
    RFLOW_LOGI("librflow_svc_connect OK (stub)");
    return RFLOW_OK;
}

rflow_err_t librflow_svc_disconnect(void) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::service::LifecycleState::kConnected &&
        s.lifecycle != rflow::service::LifecycleState::kConnecting) {
        return RFLOW_OK;
    }

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
    s.lifecycle      = rflow::service::LifecycleState::kInited;
    RFLOW_LOGI("librflow_svc_disconnect OK");
    return RFLOW_OK;
}

rflow_err_t librflow_svc_get_license_info(librflow_svc_license_info_t* out_info) {
    if (!out_info) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::service::LifecycleState::kConnected) {
        *out_info = nullptr;
        return RFLOW_ERR_STATE;
    }

    auto* info = new (std::nothrow) librflow_svc_license_info_s();
    if (!info) return RFLOW_ERR_NO_MEM;
    info->magic           = rflow::service::kMagicLicenseInfo;
    info->expire_time_sec = 0;
    info->loaded          = true;
    info->vendor_id       = s.connect_info.vendor_id;
    info->product_key     = s.connect_info.product_key;
    *out_info = info;
    return RFLOW_OK;
}

}  // extern "C"
