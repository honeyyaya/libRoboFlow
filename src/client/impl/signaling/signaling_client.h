/**
 * @file   signaling_client.h
 * @brief  简易 TCP + 行式 JSON 信令客户端（从旧工程移植）
 *
 * 仅负责收发 offer / answer / ice 三类消息；不感知业务 index。
 * 由 WebRtcPullStream / WebRtcPullManager 按会话独立持有。
 *
 * TODO:
 *   - 接入 rflow signal_config（url / 重连策略 / keepalive）；
 *   - 错误通过 rflow_err_t 向上透传，而非仅字符串回调。
 */

#ifndef __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__
#define __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace rflow::client::impl {

class SignalingClient {
 public:
    using OnSdpCallback = std::function<void(const std::string& type, const std::string& sdp)>;
    using OnIceCallback = std::function<void(const std::string& mid, int mline_index,
                                             const std::string& candidate)>;
    using OnErrorCallback = std::function<void(const std::string& msg)>;

    SignalingClient(std::string server_addr, std::string role);
    ~SignalingClient();

    SignalingClient(const SignalingClient&)            = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    bool Start();
    void Stop();

    void SendOffer (const std::string& sdp);
    void SendAnswer(const std::string& sdp);
    void SendIceCandidate(const std::string& mid, int mline_index, const std::string& candidate);

    void SetOnAnswer(OnSdpCallback cb)   { on_answer_ = std::move(cb); }
    void SetOnOffer (OnSdpCallback cb)   { on_offer_  = std::move(cb); }
    void SetOnIce   (OnIceCallback cb)   { on_ice_    = std::move(cb); }
    void SetOnError (OnErrorCallback cb) { on_error_  = std::move(cb); }

 private:
    bool Connect();
    void ReaderLoop();
    void ParseAndDispatch(const std::string& line);
    void SendLine(const std::string& line);

    std::string server_addr_;
    std::string host_;
    uint16_t    port_{0};
    std::string role_;

    OnSdpCallback   on_answer_;
    OnSdpCallback   on_offer_;
    OnIceCallback   on_ice_;
    OnErrorCallback on_error_;

    std::atomic<int>             sock_fd_{-1};
    std::atomic<bool>            running_{false};
    std::unique_ptr<std::thread> reader_thread_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_SIGNALING_CLIENT_H__
