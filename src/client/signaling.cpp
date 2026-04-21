/**
 * @file   signaling.cpp
 * @brief  send_notice / reply_to_service_req
**/

#include "robrt/Client/librobrt_client_api.h"

#include "internal/state.h"
#include "common/internal/logger.h"

extern "C" {

robrt_err_t librobrt_send_notice(int32_t index, const void* payload, uint32_t len) {
    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != robrt::client::LifecycleState::kConnected) return ROBRT_ERR_STATE;
    (void)index; (void)payload; (void)len;
    // TODO: 经 core/signal 发送
    return ROBRT_OK;
}

robrt_err_t librobrt_reply_to_service_req(uint64_t req_id, robrt_err_t status,
                                          const void* payload, uint32_t len) {
    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != robrt::client::LifecycleState::kConnected) return ROBRT_ERR_STATE;
    (void)req_id; (void)status; (void)payload; (void)len;
    // TODO: 经 core/signal 回包
    return ROBRT_OK;
}

}  // extern "C"
