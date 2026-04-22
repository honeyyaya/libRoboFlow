/**
 * @file   signaling.cpp
 * @brief  send_notice / reply_to_service_req
**/

#include "rflow/Client/librflow_client_api.h"

#include "internal/state.h"
#include "common/internal/logger.h"

extern "C" {

rflow_err_t librflow_send_notice(int32_t index, const void* payload, uint32_t len) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::client::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)index; (void)payload; (void)len;
    // TODO: 经 core/signal 发送
    return RFLOW_OK;
}

rflow_err_t librflow_reply_to_service_req(uint64_t req_id, rflow_err_t status,
                                          const void* payload, uint32_t len) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::client::LifecycleState::kConnected) return RFLOW_ERR_STATE;
    (void)req_id; (void)status; (void)payload; (void)len;
    // TODO: 经 core/signal 回包
    return RFLOW_OK;
}

}  // extern "C"
