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

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <memory>
#include <string>

#if defined(RFLOW_SVC_WEBRTC_IMPL)
#  include "service/impl/publisher.h"
#endif

namespace {

rflow_err_t require_connected_locked(rflow::service::State& s) {
    if (s.lifecycle != rflow::service::LifecycleState::kConnected) {
        rflow::set_last_error("service must be connected first");
        return RFLOW_ERR_STATE;
    }
    return RFLOW_OK;
}

#if defined(RFLOW_SVC_WEBRTC_IMPL)

std::string CodecToString(rflow_codec_t c) {
    switch (c) {
        case RFLOW_CODEC_H264:  return "h264";
        case RFLOW_CODEC_H265:  return "h265";
        case RFLOW_CODEC_MJPEG: return "mjpeg";
        default:                return "h264";
    }
}

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
    {
        // 构造 Publisher：output 分辨率与 fps 优先，fallback 到 src_* / 默认。
        uint32_t w = sh->param.has_out_size ? sh->param.out_w :
                     (sh->param.has_src_size ? sh->param.src_w : 0);
        uint32_t h = sh->param.has_out_size ? sh->param.out_h :
                     (sh->param.has_src_size ? sh->param.src_h : 0);
        uint32_t fps = sh->param.has_fps ? sh->param.fps : 30;
        uint32_t kbps_t = sh->param.has_bitrate ? sh->param.bitrate_kbps : 0;
        uint32_t kbps_max = sh->param.has_bitrate ? sh->param.max_bitrate_kbps : 0;
        if (kbps_max == 0) kbps_max = kbps_t;
        uint32_t kbps_min = sh->param.has_dynamic_bitrate ? sh->param.lowest_kbps : 0;
        const std::string video_codec = CodecToString(
            sh->param.has_out_codec ? sh->param.out_codec : RFLOW_CODEC_H264);

        const std::string signal_url = s.global_config.has_signal
                                           ? s.global_config.signal.url
                                           : std::string();
        std::string device_id = s.connect_info.device_id;
        if (device_id.empty()) {
            device_id = RFLOW_DEFAULT_DEVICE_ID;
        }
        // stream_id 字符串：使用 device_id + ":" + stream_idx 作为唯一房间标识。
        const std::string stream_id_str = device_id + ":" + std::to_string(stream_idx);

        rflow::service::impl::PublisherPullCallbacks cbs{};
        if (s.has_connect_cb) {
            cbs.on_pull_request = s.connect_cb.on_pull_request;
            cbs.on_pull_release = s.connect_cb.on_pull_release;
            cbs.userdata        = s.connect_cb.userdata;
        }

        auto pub = std::make_shared<rflow::service::impl::Publisher>(
            stream_idx,
            sh->param.has_in_codec ? sh->param.in_codec : RFLOW_CODEC_I420,
            stream_id_str, signal_url, device_id,
            static_cast<int>(w), static_cast<int>(h), static_cast<int>(fps),
            static_cast<int>(kbps_t), static_cast<int>(kbps_min), static_cast<int>(kbps_max),
            video_codec, cbs);
        sh->impl = pub;  // shared_ptr<void>
    }
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
    std::shared_ptr<librflow_svc_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;
        sh = it->second;
    }

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        if (!pub->Start()) {
            rflow::set_last_error("publisher start failed (signaling unreachable?)");
            return RFLOW_ERR_CONN_NETWORK;
        }
    }
#endif

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
    std::shared_ptr<librflow_svc_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;
        sh = it->second;
    }

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        pub->Stop();
    }
#endif

    sh->started = false;
    sh->state.store(RFLOW_STREAM_IDLE);
    if (sh->cb.on_state) {
        sh->cb.on_state(handle, RFLOW_STREAM_IDLE, RFLOW_OK, sh->cb.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t librflow_svc_destroy_stream(librflow_svc_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;
    auto& s = rflow::service::state();
    std::shared_ptr<librflow_svc_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_OK;
        sh = it->second;
        s.streams.erase(it);
    }

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    if (auto pub = AsPublisher(sh->impl)) {
        pub->Stop();
    }
    sh->impl.reset();
#endif

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
    std::shared_ptr<librflow_svc_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_ERR_NOT_FOUND;
        sh = it->second;
    }
    if (!sh->started) return RFLOW_ERR_STATE;

#if defined(RFLOW_SVC_WEBRTC_IMPL)
    auto pub = AsPublisher(sh->impl);
    if (!pub) return RFLOW_ERR_STATE;
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
            rflow::set_last_error("input codec not supported by external source (I420/NV12 only)");
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
