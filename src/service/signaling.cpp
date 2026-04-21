#include "robrt/Service/librobrt_service_api.h"

#include "internal/state.h"

extern "C" {

robrt_err_t librobrt_svc_send_notice(robrt_notice_index_t index, const void* payload, uint32_t len) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != robrt::service::LifecycleState::kConnected) return ROBRT_ERR_STATE;
    (void)index; (void)payload; (void)len;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_post_topic(const char* topic, const void* payload, uint32_t len) {
    if (!topic) return ROBRT_ERR_PARAM;
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != robrt::service::LifecycleState::kConnected) return ROBRT_ERR_STATE;
    (void)payload; (void)len;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_service_reply(uint64_t req_id, robrt_err_t status,
                                        const void* payload, uint32_t len) {
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != robrt::service::LifecycleState::kConnected) return ROBRT_ERR_STATE;
    (void)req_id; (void)status; (void)payload; (void)len;
    return ROBRT_OK;
}

}  // extern "C"
