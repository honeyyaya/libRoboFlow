#include "internal/infrastructure.h"

#include "core/rtc/rtc.h"
#include "core/signal/signal.h"
#include "core/thread/thread_pool.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "impl/webrtc/webrtc_pull_manager.h"
#endif

namespace rflow::client {

rflow_err_t init_infrastructure() {
    if (!rflow::thread::initialize()) {
        rflow::set_last_error("thread::initialize failed");
        return RFLOW_ERR_FAIL;
    }
    if (!rflow::rtc::initialize()) {
        rflow::set_last_error("rtc::initialize failed");
        rflow::thread::shutdown();
        return RFLOW_ERR_FAIL;
    }
    if (!rflow::signal::initialize()) {
        rflow::set_last_error("signal::initialize failed");
        rflow::rtc::shutdown();
        rflow::thread::shutdown();
        return RFLOW_ERR_FAIL;
    }
    return RFLOW_OK;
}

void shutdown_infrastructure() {
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    rflow::client::impl::WebRtcPullManager::Instance().Shutdown();
#endif
    rflow::signal::shutdown();
    rflow::rtc::shutdown();
    rflow::thread::shutdown();
}

rflow_err_t on_connect_succeeded(const std::string& signal_url,
                                 const std::string& device_id) {
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (!rflow::rtc::peer_connection_factory()) {
        rflow::set_last_error("peer_connection_factory not ready after rtc::initialize");
        RFLOW_LOGE("[client] on_connect: rtc not ready");
        return RFLOW_ERR_STATE;
    }
    rflow_err_t e = rflow::client::impl::WebRtcPullManager::Instance().Init(
        signal_url, device_id);
    if (e != RFLOW_OK) {
        rflow::set_last_error("WebRtcPullManager::Init failed");
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
    rflow::client::impl::WebRtcPullManager::Instance().Shutdown();
#endif
}

}  // namespace rflow::client
