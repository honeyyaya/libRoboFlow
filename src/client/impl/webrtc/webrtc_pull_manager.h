/**
 * @file   webrtc_pull_manager.h
 * @brief  多路拉流管理类：聚合 PeerConnectionFactory + 多个 WebRtcPullStream
 *
 * 设计约束：
 *   - 一个进程持有一份 factory；按业务 index 管理多路 WebRtcPullStream；
 *   - 与对外 C API（librflow_open_stream / librflow_close_stream）一一对应；
 *   - 当前协议无流级路由字段，**每路流独立一条信令连接**（role 内带 index 以便服务端区分）；
 *     未来若信令协议扩展 stream_id，可改为单连接多路复用。
 *
 * TODO:
 *   - 通过 librflow_set_global_config 注入 signal_url；目前使用内置默认值。
 *   - 生命周期挂入 rflow::client::State，避免进程级单例。
 *   - 将 FrameSink / StateSink 与 librflow_stream_cb 打通。
 */

#ifndef __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__
#define __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "rflow/librflow_common.h"

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"

namespace rflow::client::impl {

class WebRtcPullStream;

class WebRtcPullManager {
 public:
    static WebRtcPullManager& Instance();

    WebRtcPullManager(const WebRtcPullManager&)            = delete;
    WebRtcPullManager& operator=(const WebRtcPullManager&) = delete;

    // 懒初始化 factory；可重复调用。
    rflow_err_t Init();
    // 释放全部 stream 与 factory。
    void Shutdown();

    // 设置信令地址（"host:port"）。TODO: 替换为 signal_config 全量接入。
    void SetSignalingUrl(std::string url);

    // 打开一路订阅流；index 不可重复。
    rflow_err_t OpenStream(int32_t index, std::shared_ptr<WebRtcPullStream>* out);
    // 关闭一路订阅流；幂等。
    rflow_err_t CloseStream(int32_t index);

    std::shared_ptr<WebRtcPullStream> FindStream(int32_t index);

 private:
    WebRtcPullManager()  = default;
    ~WebRtcPullManager() = default;

    std::mutex mu_;
    bool       inited_ = false;

    std::string                                                     signaling_url_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>   factory_;
    std::unordered_map<int32_t, std::shared_ptr<WebRtcPullStream>>  streams_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_WEBRTC_PULL_MANAGER_H__
