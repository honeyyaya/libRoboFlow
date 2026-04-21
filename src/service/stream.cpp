/**
 * @file   stream.cpp
 * @brief  Service 端流管理 + 推帧接口（骨架）
**/

#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <memory>

namespace {

robrt_err_t require_connected_locked(robrt::service::State& s) {
    if (s.lifecycle != robrt::service::LifecycleState::kConnected) {
        robrt::set_last_error("service must be connected first");
        return ROBRT_ERR_STATE;
    }
    return ROBRT_OK;
}

}  // namespace

extern "C" {

robrt_err_t librobrt_svc_create_stream(robrt_stream_index_t          stream_idx,
                                        librobrt_svc_stream_param_t   param,
                                        librobrt_svc_stream_cb_t      cb,
                                        librobrt_svc_stream_handle_t* out_handle) {
    if (!out_handle) return ROBRT_ERR_PARAM;
    *out_handle = nullptr;

    if (!param || param->magic != robrt::service::kMagicStreamParam) return ROBRT_ERR_PARAM;
    if (cb && cb->magic != robrt::service::kMagicStreamCb)           return ROBRT_ERR_PARAM;

    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (auto e = require_connected_locked(s); e != ROBRT_OK) return e;

    auto sh = std::make_shared<librobrt_svc_stream_s>();
    sh->magic      = robrt::service::kMagicStream;
    sh->stream_idx = stream_idx;
    sh->param      = *param;
    sh->param.magic = robrt::service::kMagicStreamParam;
    if (cb) {
        sh->cb       = *cb;
        sh->cb.magic = robrt::service::kMagicStreamCb;
    }
    sh->state.store(ROBRT_STREAM_IDLE);
    sh->started = false;

    auto* raw = sh.get();
    s.streams.emplace(raw, sh);
    *out_handle = raw;
    ROBRT_LOGI("svc_create_stream idx=%d", stream_idx);
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_start_stream(librobrt_svc_stream_handle_t handle) {
    if (!handle || handle->magic != robrt::service::kMagicStream) return ROBRT_ERR_PARAM;
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return ROBRT_ERR_NOT_FOUND;

    auto& sh = it->second;
    sh->started = true;
    sh->state.store(ROBRT_STREAM_OPENED);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, ROBRT_STREAM_OPENED, ROBRT_OK, sh->cb.userdata);
    }
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stop_stream(librobrt_svc_stream_handle_t handle) {
    if (!handle || handle->magic != robrt::service::kMagicStream) return ROBRT_ERR_PARAM;
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return ROBRT_ERR_NOT_FOUND;

    auto& sh = it->second;
    sh->started = false;
    sh->state.store(ROBRT_STREAM_IDLE);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, ROBRT_STREAM_IDLE, ROBRT_OK, sh->cb.userdata);
    }
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_destroy_stream(librobrt_svc_stream_handle_t handle) {
    if (!handle) return ROBRT_OK;  // 幂等
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);

    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return ROBRT_OK;

    auto sh = it->second;
    s.streams.erase(it);
    sh->state.store(ROBRT_STREAM_CLOSED);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, ROBRT_STREAM_CLOSED, ROBRT_OK, sh->cb.userdata);
    }
    sh->magic = 0;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_push_video_frame(librobrt_svc_stream_handle_t handle,
                                           librobrt_svc_push_frame_t frame) {
    if (!handle || handle->magic != robrt::service::kMagicStream) return ROBRT_ERR_PARAM;
    if (!frame  || frame->magic  != robrt::service::kMagicPushFrame) return ROBRT_ERR_PARAM;

    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return ROBRT_ERR_NOT_FOUND;
    if (!it->second->started)  return ROBRT_ERR_STATE;

    // TODO: 交给编码器/转码器处理 + 发布给订阅端
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stream_set_bitrate(librobrt_svc_stream_handle_t handle,
                                             uint32_t bitrate_kbps) {
    if (!handle || handle->magic != robrt::service::kMagicStream) return ROBRT_ERR_PARAM;
    auto& s = robrt::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto it = s.streams.find(handle);
    if (it == s.streams.end()) return ROBRT_ERR_NOT_FOUND;
    it->second->param.bitrate_kbps = bitrate_kbps;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stream_get_stats(librobrt_svc_stream_handle_t handle,
                                           librobrt_stream_stats_t* out_stats) {
    if (!handle || handle->magic != robrt::service::kMagicStream) return ROBRT_ERR_PARAM;
    if (!out_stats) return ROBRT_ERR_PARAM;
    *out_stats = nullptr;
    return ROBRT_ERR_NOT_SUPPORT;
}

}  // extern "C"
