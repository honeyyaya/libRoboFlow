/**
 * @file   webrtc_pull_manager.h
 * @brief  多路拉流管理类：多路 WebRtcPullStream（PeerConnectionFactory 由 core/rtc 在 init 时创建）
 *
 * 设计约束：
 *   - 一个进程持有一份 factory；按业务 index 去重管理多路 WebRtcPullStream；
 *   - stream 所有权：
 *       强引用在 client::State（按 handle 保管 shared_ptr<librflow_stream_s>）；
 *       Manager 仅保留 weak_ptr 用于 index 去重 + Shutdown 扫描，不持强引用；
 *   - 当前协议无流级路由字段，**每路流独立一条信令连接**；
 *     register 中携带 device_id + stream_index，server 兼容忽略未知字段。
 *
 * TODO:
 *   - signal_url / STUN / TURN 全量接入 signal_config；
 *   - 合并到 SDK 调度线程池，避免每流起一条 reader thread。
 */

#ifndef __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__
#define __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "rflow/librflow_common.h"

namespace webrtc { class VideoFrame; }
struct librflow_stream_param_s;

namespace rflow::client::impl {

class WebRtcPullStream;

class WebRtcPullManager {
 public:
    using FrameSink = std::function<void(const webrtc::VideoFrame& frame)>;
    using StateSink = std::function<void(rflow_stream_state_t state, rflow_err_t reason)>;

    static WebRtcPullManager& Instance();

    WebRtcPullManager(const WebRtcPullManager&)            = delete;
    WebRtcPullManager& operator=(const WebRtcPullManager&) = delete;

    // 由 rflow::client::on_connect_succeeded 调用。signal_url 为空则沿用旧默认值。
    // device_id 用于信令 register 消息的设备维度鉴权（当前协议仅 fire-and-forget 携带）。
    // 可重复 Init，再次调用会覆盖配置。
    rflow_err_t Init(std::string signal_url, std::string device_id);

    // 关全部 WebRtcPullStream，不释放 core 侧 PeerConnectionFactory。Init 可再触发。
    void Shutdown();

    // 打开一路订阅流；index 全局去重。
    //   param 可为空，将沿用 SDK 默认策略（当前协议无协商字段，仅记录 hint）。
    //   sinks 闭包生命周期需 >= stream（stream 析构前持有）。
    rflow_err_t OpenStream(int32_t index,
                           const struct ::librflow_stream_param_s* param,
                           StateSink state_sink,
                           FrameSink frame_sink,
                           std::shared_ptr<WebRtcPullStream>* out);

 private:
    WebRtcPullManager()  = default;
    ~WebRtcPullManager() = default;

    std::mutex  mu_;
    bool        inited_ = false;
    std::string signaling_url_;
    std::string device_id_;

    // index → 当前打开的流（弱引用，只用于去重 + Shutdown 扫描）。
    std::unordered_map<int32_t, std::weak_ptr<WebRtcPullStream>> open_indices_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__
