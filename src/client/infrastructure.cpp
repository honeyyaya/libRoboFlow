#include "internal/infrastructure.h"

#include "core/runtime/infra_lifecycle.h"
#include "core/rtc/rtc.h"

#include "common/public/last_error_api.h"
#include "common/public/logger_api.h"

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "impl/rtc_stream/rtc_stream_manager.h"
#endif

namespace {
constexpr const char* kErrorOrigin = "client/infrastructure";
}  // namespace

namespace rflow::client {

rflow_err_t init_infrastructure() {
    rflow::core::runtime::InitFailureStage failure_stage = rflow::core::runtime::InitFailureStage::kNone;
    const auto rc = rflow::core::runtime::InitInfrastructure(&failure_stage);
    if (rc != RFLOW_OK) {
        switch (failure_stage) {
            case rflow::core::runtime::InitFailureStage::kThread:
                rflow::set_last_error(rc, "thread::initialize failed", kErrorOrigin);
                break;
            case rflow::core::runtime::InitFailureStage::kRtc:
                rflow::set_last_error(rc, "rtc::initialize failed", kErrorOrigin);
                break;
            case rflow::core::runtime::InitFailureStage::kSignal:
                rflow::set_last_error(rc, "signal::initialize failed", kErrorOrigin);
                break;
            default:
                rflow::set_last_error(rc, "core infrastructure initialize failed", kErrorOrigin);
                break;
        }
        return rc;
    }
    return rc;
}

void shutdown_infrastructure() {
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    rflow::client::impl::RtcStreamManager::Instance().Shutdown();
#endif
    rflow::core::runtime::ShutdownInfrastructure();
}

rflow_err_t on_connect_succeeded(const std::string& signal_url,
                                 const std::string& device_id) {
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (signal_url.empty()) {
        rflow::set_last_error(
            RFLOW_ERR_PARAM,
            "librflow_connect: global_config.signal.url is empty; "
            "call librflow_signal_config_set_url + librflow_set_global_config before connect",
            kErrorOrigin);
        RFLOW_LOGE("[client] connect aborted: missing signal url in global_config");
        return RFLOW_ERR_PARAM;
    }
    if (!rflow::rtc::peer_connection_factory()) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "peer_connection_factory not ready after rtc::initialize",
                              kErrorOrigin);
        RFLOW_LOGE("[client] on_connect: rtc not ready");
        return RFLOW_ERR_STATE;
    }
    rflow_err_t e = rflow::client::impl::RtcStreamManager::Instance().Init(
        signal_url, device_id);
    if (e != RFLOW_OK) {
        rflow::set_last_error(e, "RtcStreamManager::Init failed", kErrorOrigin);
    }
    return e;
#else
    (void)signal_url;
    (void)device_id;
    return RFLOW_OK;
#endif
}

void on_disconnect() {
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    rflow::client::impl::RtcStreamManager::Instance().Shutdown();
#endif
}

}  // namespace rflow::client
