/**
 * @file   lifecycle.cpp
 * @brief  Service 端生命周期：set_global_config /
 *         init / uninit / connect / disconnect / get_license_info
**/

#include "rflow/Service/librflow_service_api.h"
#include "rflow/librflow_common.h"

#include "internal/handles.h"
#include "internal/state.h"
#include "internal/state_ops.h"

#include "abi/object_layouts.h"
#include "base/global_config_ops.h"
#include "public/last_error_api.h"
#include "public/logger_api.h"
#include "rtc/rtc_factory_common.h"

#include <new>

namespace {
constexpr const char* kErrorOrigin = "service/lifecycle";
}  // namespace

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
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "svc_set_global_config must be called before init",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    if (!rflow::common::base::IsValidGlobalConfigHandle(cfg)) {
        rflow::set_last_error(RFLOW_ERR_PARAM,
                              "svc_set_global_config: invalid global_config handle",
                              kErrorOrigin);
        return RFLOW_ERR_PARAM;
    }

    rflow::common::base::CopyGlobalConfig(s.global_config, *cfg);
    rflow::rtc::NotifyFlexfecTrialFromSdkConfig(s.global_config.has_enable_flexfec,
                                                 s.global_config.enable_flexfec);
    rflow::common::base::ApplyLogConfigIfPresent(s.global_config);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_init(void) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::service::LifecycleState::kUninit) return RFLOW_OK;

    if (auto rc = rflow::service::internal::InitSubsystems(); rc != RFLOW_OK) return rc;

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
    rflow::service::internal::ShutdownSubsystems();
    rflow::rtc::ResetFlexfecTrialSdkOverride();

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

    const auto rc = rflow::service::internal::ConnectStateTransition(s, *info, *cb);
    if (rc != RFLOW_OK) return rc;
    RFLOW_LOGI("librflow_svc_connect OK (stub)");
    return RFLOW_OK;
}

rflow_err_t librflow_svc_disconnect(void) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    const auto rc = rflow::service::internal::DisconnectStateTransition(s);
    if (rc != RFLOW_OK) return rc;
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

rflow_err_t librflow_svc_send_notice(rflow_notice_index_t index, const void* payload, uint32_t len) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)index;
    (void)payload;
    (void)len;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_post_topic(const char* topic, const void* payload, uint32_t len) {
    if (!topic) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)payload;
    (void)len;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_service_reply(uint64_t req_id, rflow_err_t status,
                                       const void* payload, uint32_t len) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)req_id;
    (void)status;
    (void)payload;
    (void)len;
    return RFLOW_OK;
}

}  // extern "C"
