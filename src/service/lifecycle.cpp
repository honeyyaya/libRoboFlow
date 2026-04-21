/**
 * @file   lifecycle.cpp
 * @brief  Service 端生命周期：set_global_config / set_talk_config /
 *         init / uninit / connect / disconnect / get_license_info
**/

#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/global_config_impl.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include "core/rtc/rtc.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

#include <new>

namespace robrt::service {

State& state() {
    static State s;
    return s;
}

}  // namespace robrt::service

extern "C" {

robrt_err_t librobrt_svc_set_global_config(librobrt_global_config_t cfg) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::service::LifecycleState::kUninit) {
        robrt::set_last_error("svc_set_global_config must be called before init");
        return ROBRT_ERR_STATE;
    }
    if (!cfg || cfg->magic != robrt::kMagicGlobalConfig) return ROBRT_ERR_PARAM;

    s.global_config = *cfg;
    s.global_config.magic = robrt::kMagicGlobalConfig;

    if (s.global_config.has_log) {
        robrt::logger_apply(s.global_config.log.level,
                            s.global_config.log.cb,
                            s.global_config.log.userdata);
    }
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_set_talk_config(librobrt_svc_talk_config_t cfg) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle == robrt::service::LifecycleState::kConnected) {
        robrt::set_last_error("talk config cannot be modified after connect");
        return ROBRT_ERR_STATE;
    }
    if (!cfg || cfg->magic != robrt::service::kMagicTalkConfig) return ROBRT_ERR_PARAM;
    s.talk_config     = *cfg;
    s.talk_config.magic = robrt::service::kMagicTalkConfig;
    s.has_talk_config = true;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_init(void) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::service::LifecycleState::kUninit) return ROBRT_OK;

    if (!robrt::thread::initialize()) return ROBRT_ERR_FAIL;
    if (!robrt::rtc::initialize())    { robrt::thread::shutdown(); return ROBRT_ERR_FAIL; }
    if (!robrt::signal::initialize()) { robrt::rtc::shutdown(); robrt::thread::shutdown(); return ROBRT_ERR_FAIL; }

    s.lifecycle = robrt::service::LifecycleState::kInited;
    ROBRT_LOGI("librobrt_svc_init OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_uninit(void) {
    librobrt_svc_disconnect();

    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle == robrt::service::LifecycleState::kUninit) return ROBRT_OK;

    s.streams.clear();
    robrt::signal::shutdown();
    robrt::rtc::shutdown();
    robrt::thread::shutdown();

    s.lifecycle = robrt::service::LifecycleState::kUninit;
    ROBRT_LOGI("librobrt_svc_uninit OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_connect(librobrt_svc_connect_info_t info,
                                  librobrt_svc_connect_cb_t   cb) {
    if (!info || info->magic != robrt::service::kMagicConnectInfo) return ROBRT_ERR_PARAM;
    if (!cb   || cb->magic   != robrt::service::kMagicConnectCb)   return ROBRT_ERR_PARAM;

    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle == robrt::service::LifecycleState::kUninit) return ROBRT_ERR_STATE;
    if (s.lifecycle == robrt::service::LifecycleState::kConnected ||
        s.lifecycle == robrt::service::LifecycleState::kConnecting) {
        return ROBRT_ERR_STATE;
    }

    s.connect_info   = *info;
    s.connect_cb     = *cb;
    s.has_connect_cb = true;
    s.lifecycle      = robrt::service::LifecycleState::kConnecting;

    // TODO: 实际向云端注册 + 鉴权 + license 校验；此处直连成功
    s.lifecycle = robrt::service::LifecycleState::kConnected;
    if (s.connect_cb.on_state) {
        s.connect_cb.on_state(ROBRT_CONN_CONNECTED, ROBRT_OK, s.connect_cb.userdata);
    }
    if (s.connect_cb.on_bind_state) {
        s.connect_cb.on_bind_state(ROBRT_BIND_BOUND, nullptr, s.connect_cb.userdata);
    }
    ROBRT_LOGI("librobrt_svc_connect OK (stub)");
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_disconnect(void) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::service::LifecycleState::kConnected &&
        s.lifecycle != robrt::service::LifecycleState::kConnecting) {
        return ROBRT_OK;
    }

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
    s.lifecycle      = robrt::service::LifecycleState::kInited;
    ROBRT_LOGI("librobrt_svc_disconnect OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_get_license_info(librobrt_svc_license_info_t* out_info) {
    if (!out_info) return ROBRT_ERR_PARAM;
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::service::LifecycleState::kConnected) {
        *out_info = nullptr;
        return ROBRT_ERR_STATE;
    }

    auto* info = new (std::nothrow) librobrt_svc_license_info_s();
    if (!info) return ROBRT_ERR_NO_MEM;
    info->magic       = robrt::service::kMagicLicenseInfo;
    info->expire_time = 0;
    info->vendor_id   = s.connect_info.vendor_id;
    info->product_key = s.connect_info.product_key;
    *out_info = info;
    return ROBRT_OK;
}

}  // extern "C"
