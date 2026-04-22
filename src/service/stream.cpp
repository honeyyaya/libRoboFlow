/**
 * @file   stream.cpp
 * @brief  Service 端流管理 + 推帧接口（骨架）
**/

#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <memory>

namespace {

rflow_err_t require_connected_locked(rflow::service::State& s) {
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) {
        rflow::set_last_error("service must be connected first");
        return RFLOW_ERR_STATE;
    }
    return RFLOW_OK;
}

}  // namespace

extern "C" {

rflow_err_t librflow_svc_create_stream(rflow_stream_index_t          stream_idx,
                                        librflow_svc_stream_param_t   param,
                                        librflow_svc_stream_cb_t      cb,
                                        librflow_svc_stream_handle_t* out_handle) {
    if (!out_handle) return RFLOW_ERR_PARAM;
    *out_handle = nullptr;

    if (!param || param->magic != rflow::service::kMagicStreamParam) return RFLOW_ERR_PARAM;
    if (cb && cb->magic != rflow::service::kMagicStreamCb)           return RFLOW_ERR_PARAM;

    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (auto e = require_connected_locked(s); e != RFLOW_OK) return e;

    auto sh = std::make_shared<librflow_svc_stream_s>();
    sh->magic      = rflow::service::kMagicStream;
    sh->stream_idx = stream_idx;
    sh->param      = *param;
    sh->param.magic = rflow::service::kMagicStreamParam;
    if (cb) {
        sh->cb       = *cb;
        sh->cb.magic = rflow::service::kMagicStreamCb;
    }
    sh->state.store(RFLOW_STREAM_IDLE);
    sh->started = false;

    auto* raw = sh.get();
    s.streams.emplace(raw, sh);
    *out_handle = raw;
    RFLOW_LOGI("svc_create_stream idx=%d", stream_idx);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_start_stream(librflow_svc_stream_handle_t handle) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;

    auto& sh = it->second;
    sh->started = true;
    sh->state.store(RFLOW_STREAM_OPENED);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, RFLOW_STREAM_OPENED, RFLOW_OK, sh->cb.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stop_stream(librflow_svc_stream_handle_t handle) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;

    auto& sh = it->second;
    sh->started = false;
    sh->state.store(RFLOW_STREAM_IDLE);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, RFLOW_STREAM_IDLE, RFLOW_OK, sh->cb.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t librflow_svc_destroy_stream(librflow_svc_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;  // 幂等
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return RFLOW_OK;

    auto sh = it->second;
    s.streams.erase(it);
    sh->state.store(RFLOW_STREAM_CLOSED);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, RFLOW_STREAM_CLOSED, RFLOW_OK, sh->cb.userdata);
    }
    sh->magic = 0;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_push_video_frame(librflow_svc_stream_handle_t handle,
                                           librflow_svc_push_frame_t frame) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    if (!frame  || frame->magic  != rflow::service::kMagicPushFrame) return RFLOW_ERR_PARAM;

    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;
    if (!it->second->started)  return RFLOW_ERR_STATE;

    // TODO: 交给编码器/转码器处理 + 发布给订阅端
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_set_bitrate(librflow_svc_stream_handle_t handle,
                                             uint32_t bitrate_kbps) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;
    it->second->param.bitrate_kbps = bitrate_kbps;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_get_stats(librflow_svc_stream_handle_t handle,
                                           librflow_stream_stats_t* out_stats) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;
    *out_stats = nullptr;
    return RFLOW_ERR_NOT_SUPPORT;
}

}  // extern "C"
