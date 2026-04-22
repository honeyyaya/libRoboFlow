#include "rflow/Service/librflow_service_api.h"

#include "internal/state.h"

extern "C" {

rflow_err_t librflow_svc_send_notice(rflow_notice_index_t index, const void* payload, uint32_t len) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)index; (void)payload; (void)len;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_post_topic(const char* topic, const void* payload, uint32_t len) {
    if (!topic) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)payload; (void)len;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_service_reply(uint64_t req_id, rflow_err_t status,
                                        const void* payload, uint32_t len) {
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)req_id; (void)status; (void)payload; (void)len;
    return RFLOW_OK;
}

}  // extern "C"
