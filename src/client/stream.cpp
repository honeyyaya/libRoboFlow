/**
 * @file   stream.cpp
 * @brief  open_stream / close_stream / get_stats
 *
 * 实现要点：
 *   - open_stream 在已 connect 的前提下，向 WebRtcPullManager 申请一路 WebRtcPullStream；
 *     通过 state_sink / frame_sink 把底层事件桥接回用户的 stream_cb；
 *   - close_stream 幂等：从 State::streams 取出 shared_ptr，释放 impl 时会触发其 Close。
 *   - 所有用户回调均在锁外触发，避免与 state.mu 形成重入死锁。
 */

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include <atomic>
#include <memory>
#include <utility>

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "impl/webrtc/webrtc_frame_converter.h"
#  include "impl/webrtc/webrtc_pull_manager.h"
#  include "impl/webrtc/webrtc_pull_stream.h"
#  include "api/video/video_frame.h"
#endif

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

    // 1. 构造 handle，登记到 State::streams；过程中做状态 & 去重检查
    auto sh   = std::make_shared<librflow_stream_s>();
    sh->magic = rflow::client::kMagicStream;
    sh->index = index;
    sh->state.store(RFLOW_STREAM_IDLE);
    sh->cb    = *cb;

    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.lifecycle != rflow::client::LifecycleState::kConnected) {
            rflow::set_last_error("must connect before open_stream");
            return RFLOW_ERR_STATE;
        }
        for (auto& [h, x] : s.streams) {
            if (x && x->index == index) {
                rflow::set_last_error("stream index already open");
                return RFLOW_ERR_STREAM_ALREADY_OPEN;
            }
        }
        s.streams.emplace(sh.get(), sh);
    }

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    // 2. 构造 sinks（弱引用 stream_s，避免与 impl 形成环）
    std::weak_ptr<librflow_stream_s> wsh = sh;
    const int32_t captured_index = index;
    // seq 计数器：跨回调持久，放 shared_ptr 便于闭包拷贝
    auto seq_counter = std::make_shared<std::atomic<uint32_t>>(0);

    auto state_sink = [wsh](rflow_stream_state_t st, rflow_err_t reason) {
        auto p = wsh.lock();
        if (!p) return;
        p->state.store(st);
        if (p->cb.on_state) {
            p->cb.on_state(p.get(), st, reason, p->cb.userdata);
        }
    };

    auto frame_sink = [wsh, captured_index, seq_counter](const webrtc::VideoFrame& vf) {
        auto p = wsh.lock();
        if (!p || !p->cb.on_video) return;
        const uint32_t seq = seq_counter->fetch_add(1, std::memory_order_relaxed);
        auto f = rflow::client::impl::MakeVideoFrameFromWebrtc(vf, captured_index, seq);
        if (!f) return;
        p->cb.on_video(p.get(), f, p->cb.userdata);
        librflow_video_frame_release(f);
    };

    // 3. 调用 Manager 建底层流（锁外；sinks 可能在此调用期间就同步触达一次）
    std::shared_ptr<rflow::client::impl::WebRtcPullStream> pull;
    rflow_err_t e = rflow::client::impl::WebRtcPullManager::Instance().OpenStream(
        index, param, std::move(state_sink), std::move(frame_sink), &pull);
    if (e != RFLOW_OK) {
        std::lock_guard<std::mutex> lk(s.mu);
        s.streams.erase(sh.get());
        sh->magic = 0;
        return e;
    }

    sh->impl = std::static_pointer_cast<void>(pull);
#else
    // 无 WebRTC 实现时：保留历史 stub 行为，直接 OPENED
    sh->state.store(RFLOW_STREAM_OPENED);
    if (sh->cb.on_state) {
        sh->cb.on_state(sh.get(), RFLOW_STREAM_OPENED, RFLOW_OK, sh->cb.userdata);
    }
#endif

    *out_handle = sh.get();
    RFLOW_LOGI("librflow_open_stream index=%d OK", index);
    return RFLOW_OK;
}

rflow_err_t librflow_close_stream(librflow_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;  // 幂等

    auto& s = rflow::client::state();

    std::shared_ptr<librflow_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_OK;
        sh = std::move(it->second);
        s.streams.erase(it);
    }

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    // 先析构底层实现（其 Close 会通过 state_sink 触达用户 on_state(CLOSED)）。
    if (auto impl = std::move(sh->impl)) {
        auto pull = std::static_pointer_cast<rflow::client::impl::WebRtcPullStream>(impl);
        pull->Close();
    } else
#endif
    {
        sh->state.store(RFLOW_STREAM_CLOSED);
        if (sh->cb.on_state) {
            sh->cb.on_state(handle, RFLOW_STREAM_CLOSED, RFLOW_OK, sh->cb.userdata);
        }
    }

    sh->magic = 0;
    RFLOW_LOGI("librflow_close_stream idx=%d OK", sh->index);
    return RFLOW_OK;
}

rflow_err_t librflow_stream_get_stats(librflow_stream_handle_t handle,
                                       librflow_stream_stats_t*  out_stats) {
    if (!handle || handle->magic != rflow::client::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;
    // TODO: 经 WebRtcPullStream/PeerConnection::GetStats 拉取并映射到 librflow_stream_stats_s
    *out_stats = nullptr;
    return RFLOW_ERR_NOT_SUPPORT;
}

}  // extern "C"
