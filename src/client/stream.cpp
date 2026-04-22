/**
 * @file   stream.cpp
 * @brief  open_stream / close_stream / get_stats
**/

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <memory>

extern "C" {

rflow_err_t librflow_open_stream(int32_t index,
                                  librflow_stream_param_t param,
                                  librflow_stream_cb_t    cb,
                                  librflow_stream_handle_t* out_handle) {
    if (!out_handle) return RFLOW_ERR_PARAM;
    *out_handle = nullptr;

    if (param && param->magic != rflow::client::kMagicStreamParam) return RFLOW_ERR_PARAM;
    if (!cb || cb->magic != rflow::client::kMagicStreamCb)         return RFLOW_ERR_PARAM;

    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kConnected) {
        rflow::set_last_error("must connect before open_stream");
        return RFLOW_ERR_STATE;
    }

    auto sh = std::make_shared<librflow_stream_s>();
    sh->magic = rflow::client::kMagicStream;
    sh->index = index;
    sh->state.store(RFLOW_STREAM_OPENING);
    sh->cb    = *cb;

    auto* raw = sh.get();
    s.streams.emplace(raw, sh);

    // TODO: 实际经 core/rtc 订阅；这里直接置 OPENED
    sh->state.store(RFLOW_STREAM_OPENED);
    if (sh->cb.on_state) {
        sh->cb.on_state(raw, RFLOW_STREAM_OPENED, RFLOW_OK, sh->cb.userdata);
    }

    *out_handle = raw;
    RFLOW_LOGI("librflow_open_stream index=%d (stub)", index);
    return RFLOW_OK;
}

rflow_err_t librflow_close_stream(librflow_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;  // 幂等
    auto& s = rflow::client::state();
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
    RFLOW_LOGI("librflow_close_stream OK");
    return RFLOW_OK;
}

rflow_err_t librflow_stream_get_stats(librflow_stream_handle_t handle,
                                       librflow_stream_stats_t*  out_stats) {
    if (!handle || handle->magic != rflow::client::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;
    // TODO: 实现 pull 统计
    *out_stats = nullptr;
    return RFLOW_ERR_NOT_SUPPORT;
}

}  // extern "C"
