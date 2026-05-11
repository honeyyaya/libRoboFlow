/**
 * @file   stream.cpp
 * @brief  Service 端流管理 + 推帧接口
 *
 * WebRTC 实现（RFLOW_SVC_WEBRTC_IMPL=ON）打开时：
 *   - create_stream  → 构造 rflow::service::impl::Publisher（PushStreamer + SignalingClient）
 *   - start_stream   → Publisher::Start()：连接信令、开启推流
 *   - push_video_frame → 按 in_codec 路由到 Publisher::PushI420 / PushNv12
 *   - stop_stream / destroy_stream → Publisher::Stop() + 资源释放
 * stub 构建下（默认）退化为记录状态 + 返回 OK；push_video_frame 不做媒体搬运。
**/

#include "rflow/Service/librflow_service_api.h"
#include "rflow/librflow_common.h"

#include "internal/handles.h"
#include "internal/state.h"
#include "internal/state_ops.h"
#include "internal/stream_startup_policy.h"

#include "base/stream_handle_ops.h"
#include "media/stream_stats.h"
#include "public/last_error_api.h"
#include "public/logger_api.h"

#include <algorithm>
#include <memory>
#include <string>

#if defined(RFLOW_SVC_WEBRTC_IMPL)
#  include "impl/publisher.h"
#endif

namespace {

constexpr const char* kErrorOrigin = "service/stream";

rflow_err_t require_connected_locked(rflow::service::State& s) {
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "service must be connected first",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    return RFLOW_OK;
}

#if defined(RFLOW_SVC_WEBRTC_IMPL)

std::shared_ptr<rflow::service::impl::Publisher>
AsPublisher(const std::shared_ptr<void>& impl) {
    return std::static_pointer_cast<rflow::service::impl::Publisher>(impl);
}

#endif

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
    sh->magic       = rflow::service::kMagicStream;
    sh->stream_idx  = stream_idx;
    sh->param       = *param;
    sh->param.magic = rflow::service::kMagicStreamParam;
    if (cb) {
        sh->cb       = *cb;
        sh->cb.magic = rflow::service::kMagicStreamCb;
    }
    sh->state.store(RFLOW_STREAM_IDLE);
    sh->started = false;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    sh->impl = rflow::service::internal::CreatePublisherImplForStream(*sh, s, stream_idx);
#endif

    auto* raw = sh.get();
    s.streams.emplace(raw, sh);
    *out_handle = raw;
    RFLOW_LOGI("svc_create_stream idx=%d", stream_idx);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_start_stream(librflow_svc_stream_handle_t handle) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    auto sh = rflow::common::base::LookupStreamUnlocked(s, handle);
    if (!sh) return RFLOW_ERR_NOT_FOUND;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        if (!pub->Start()) {
            rflow::set_last_error(RFLOW_ERR_CONN_NETWORK,
                                  "publisher start failed (signaling unreachable?)",
                                  kErrorOrigin);
            return RFLOW_ERR_CONN_NETWORK;
        }
    }
#endif

    rflow::service::internal::MarkStreamStarted(sh, handle);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stop_stream(librflow_svc_stream_handle_t handle) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    auto sh = rflow::common::base::LookupStreamUnlocked(s, handle);
    if (!sh) return RFLOW_ERR_NOT_FOUND;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        pub->Stop();
    }
#endif

    rflow::service::internal::MarkStreamStopped(sh, handle);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_destroy_stream(librflow_svc_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;
    auto& s = rflow::service::state();
    auto sh = rflow::common::base::RemoveStreamUnlocked(s, handle);
    if (!sh) return RFLOW_OK;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        pub->Stop();
    }
    sh->impl.reset();
#endif

    rflow::service::internal::MarkStreamDestroyed(sh, handle);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_push_video_frame(librflow_svc_stream_handle_t handle,
                                           librflow_svc_push_frame_t frame) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    if (!frame  || frame->magic  != rflow::service::kMagicPushFrame) return RFLOW_ERR_PARAM;

    auto& s = rflow::service::state();
    auto sh = rflow::common::base::LookupStreamUnlocked(s, handle);
    if (!sh) return RFLOW_ERR_NOT_FOUND;
    if (!sh->started) return RFLOW_ERR_STATE;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    auto pub = AsPublisher(sh->impl);
    if (!pub) return RFLOW_ERR_STATE;
    if (!pub->uses_external_video_source()) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "stream uses SDK internal video capture; push_video_frame is disabled",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    if (frame->data.empty()) return RFLOW_ERR_PARAM;

    const int width  = frame->has_size ? static_cast<int>(frame->width)
                                        : (sh->param.has_src_size ? static_cast<int>(sh->param.src_w) : 0);
    const int height = frame->has_size ? static_cast<int>(frame->height)
                                        : (sh->param.has_src_size ? static_cast<int>(sh->param.src_h) : 0);
    if (width <= 0 || height <= 0) return RFLOW_ERR_PARAM;

    const int64_t ts_us = frame->has_pts_ms ? static_cast<int64_t>(frame->pts_ms) * 1000
                                             : 0;

    const rflow_codec_t codec = frame->has_codec ? frame->codec : pub->in_codec();
    const uint8_t* data = frame->data.data();
    const uint32_t size = static_cast<uint32_t>(frame->data.size());
    switch (codec) {
        case RFLOW_CODEC_I420:
            return pub->PushI420(data, size, width, height, ts_us)
                       ? RFLOW_OK : RFLOW_ERR_FAIL;
        case RFLOW_CODEC_NV12:
            return pub->PushNv12(data, size, width, height, ts_us)
                       ? RFLOW_OK : RFLOW_ERR_FAIL;
        default:
            rflow::set_last_error(RFLOW_ERR_STREAM_CODEC_UNSUPP,
                                  "input codec not supported by external source (I420/NV12 only)",
                                  kErrorOrigin);
            return RFLOW_ERR_STREAM_CODEC_UNSUPP;
    }
#else
    (void)sh;
    return RFLOW_OK;  // stub 构建：不做媒体搬运
#endif
}

rflow_err_t librflow_svc_stream_set_bitrate(librflow_svc_stream_handle_t handle,
                                             uint32_t bitrate_kbps) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    auto& s = rflow::service::state();
    std::lock_guard<std::mutex> lk(s.mu);
    auto sh = rflow::service::internal::FindStreamByHandleLocked(s, handle);
    if (!sh) return RFLOW_ERR_NOT_FOUND;
    sh->param.bitrate_kbps = bitrate_kbps;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_get_stats(librflow_svc_stream_handle_t handle,
                                           librflow_stream_stats_t* out_stats) {
    if (!handle || handle->magic != rflow::service::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;
    *out_stats = nullptr;

    auto& s = rflow::service::state();
    auto sh = rflow::common::base::LookupStreamUnlocked(s, handle);
    if (!sh) return RFLOW_ERR_NOT_FOUND;

    auto stats = rflow::common::media::AllocStreamStats();
    if (!stats) return RFLOW_ERR_FAIL;

    auto* ms = const_cast<librflow_stream_stats_s*>(stats);

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    auto pub = AsPublisher(sh->impl);
    const uint64_t pushed = pub ? pub->video_frames_pushed() : 0;
    rflow::common::media::FillStreamStatsBase(*ms, sh->started_at, pushed);

    bool collected = false;
    if (pub) {
        collected = pub->CollectStats(ms);
    }
    if (!collected) {
        rflow::common::media::ApplyStreamStatsFpsFallbackFromFrameCount(*ms, pushed);
    }
#else
    rflow::common::media::FillStreamStatsBase(*ms, sh->started_at, 0);
#endif

    *out_stats = stats;
    return RFLOW_OK;
}

}  // extern "C"
