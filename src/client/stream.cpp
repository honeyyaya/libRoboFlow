/**
 * @file   stream.cpp
 * @brief  open_stream / close_stream / get_stats
**/

#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <memory>

extern "C" {

robrt_err_t librobrt_open_stream(int32_t index,
                                  librobrt_stream_param_t param,
                                  librobrt_stream_cb_t    cb,
                                  librobrt_stream_handle_t* out_handle) {
    if (!out_handle) return ROBRT_ERR_PARAM;
    *out_handle = nullptr;

    if (param && param->magic != robrt::client::kMagicStreamParam) return ROBRT_ERR_PARAM;
    if (!cb || cb->magic != robrt::client::kMagicStreamCb)         return ROBRT_ERR_PARAM;

    auto& s = robrt::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != robrt::client::LifecycleState::kConnected) {
        robrt::set_last_error("must connect before open_stream");
        return ROBRT_ERR_STATE;
    }

    auto sh = std::make_shared<librobrt_stream_s>();
    sh->magic = robrt::client::kMagicStream;
    sh->index = index;
    sh->state.store(ROBRT_STREAM_OPENING);
    sh->cb    = *cb;

    auto* raw = sh.get();
    s.streams.emplace(raw, sh);

    // TODO: 实际经 core/rtc 订阅；这里直接置 OPENED
    sh->state.store(ROBRT_STREAM_OPENED);
    if (sh->cb.on_state) {
        sh->cb.on_state(raw, ROBRT_STREAM_OPENED, ROBRT_OK, sh->cb.userdata);
    }

    *out_handle = raw;
    ROBRT_LOGI("librobrt_open_stream index=%d (stub)", index);
    return ROBRT_OK;
}

robrt_err_t librobrt_close_stream(librobrt_stream_handle_t handle) {
    if (!handle) return ROBRT_OK;  // 幂等
    auto& s = robrt::client::state();
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
    ROBRT_LOGI("librobrt_close_stream OK");
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_get_stats(librobrt_stream_handle_t handle,
                                       librobrt_stream_stats_t*  out_stats) {
    if (!handle || handle->magic != robrt::client::kMagicStream) return ROBRT_ERR_PARAM;
    if (!out_stats) return ROBRT_ERR_PARAM;
    // TODO: 实现 pull 统计
    *out_stats = nullptr;
    return ROBRT_ERR_NOT_SUPPORT;
}

}  // extern "C"
