#ifndef PUSH_STREAMER_H
#define PUSH_STREAMER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace webrtc_demo {

struct PushStreamerCommonConfig {
    std::string stream_id{"stream_001"};
    int video_width{1280};
    int video_height{720};
    int video_fps{30};
    int video_device_index{0};
    std::string video_device_path;
    bool test_capture_only{false};
    bool test_encode_mode{false};
    bool signaling_subscriber_offer_only{false};
    /// 为 true 时跳过 V4L2 采集，改由业务侧主动 PushExternalI420/Nv12 投帧。
    /// 该模式下：video_device_index / video_device_path / capture_gate_* / mjpeg 相关
    /// 配置全部失效；视频宽高以实际 PushExternal* 的帧为准（首帧作为 SDP 参考）。
    bool use_external_video_source{false};
    std::string stun_server{"stun:stun.l.google.com:19302"};
    std::string turn_server;
    std::string turn_username;
    std::string turn_password;

    std::string bitrate_mode{"vbr"};
    int target_bitrate_kbps{1000};
    int min_bitrate_kbps{100};
    int max_bitrate_kbps{2000};
    std::string degradation_preference{"balanced"};
    bool ice_prioritize_likely_pairs{true};
    std::string video_network_priority{"high"};
    int video_encoding_max_framerate{0};

    std::string video_codec{"h264"};
    std::string h264_profile{"main"};
    std::string h264_level{"3.0"};
    int keyframe_interval{0};
    int capture_warmup_sec{0};
    int capture_gate_min_frames{0};
    int capture_gate_max_wait_sec{20};
};

struct PushStreamerBackendConfig {
    bool use_rockchip_mpp_h264{true};
    bool use_rockchip_mpp_mjpeg_decode{true};
    bool use_rockchip_dual_mpp_mjpeg_h264{false};
    bool mjpeg_queue_latest_only{false};
    int mjpeg_queue_max{8};
    int v4l2_buffer_count{2};
    int v4l2_poll_timeout_ms{50};
    int nv12_pool_slots{6};
    bool mjpeg_decode_inline{false};
    bool mjpeg_v4l2_ext_dma{false};
    bool mjpeg_rga_to_mpp{false};
};

/// 推流配置
struct PushStreamerConfig {
    PushStreamerCommonConfig common{};
    PushStreamerBackendConfig backend{};
};

/// SDP 回调：用于将 SDP 发送到信令服务器
using OnSdpCallback = std::function<void(const std::string& peer_id, const std::string& type,
                                         const std::string& sdp)>;

/// ICE 候选回调：用于将 ICE 候选发送到信令服务器
using OnIceCandidateCallback = std::function<void(const std::string& peer_id, const std::string& mid,
                                                  int mline_index, const std::string& candidate)>;

/// 帧回调：每收到一帧调用，参数为 (帧数, 宽, 高)，用于验证采集是否正常
using OnFrameCallback = std::function<void(unsigned int frame_count, int width, int height)>;

/// 连接状态回调
enum class ConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed
};
using OnConnectionStateCallback = std::function<void(ConnectionState state)>;

/// WebRTC 推流器
class PushStreamer {
public:
    explicit PushStreamer(const PushStreamerConfig& config);
    ~PushStreamer();

    PushStreamer(const PushStreamer&) = delete;
    PushStreamer& operator=(const PushStreamer&) = delete;

    /// 初始化并开始推流
    bool Start();

    /// 停止推流
    void Stop();

    /// 设置远端 SDP（Answer），用于 P2P 模式
    bool SetRemoteDescription(const std::string& type, const std::string& sdp);
    bool SetRemoteDescriptionForPeer(const std::string& peer_id, const std::string& type,
                                     const std::string& sdp);

    /// 添加远端 ICE 候选
    void AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate);
    void AddRemoteIceCandidateForPeer(const std::string& peer_id, const std::string& mid,
                                      int mline_index, const std::string& candidate);
    void CreateOfferForPeer(const std::string& peer_id);

    /// 回调设置
    void SetOnSdpCallback(OnSdpCallback cb);
    void SetOnIceCandidateCallback(OnIceCandidateCallback cb);
    void SetOnConnectionStateCallback(OnConnectionStateCallback cb);
    void SetOnFrameCallback(OnFrameCallback cb);

    /// 外部帧 push（use_external_video_source=true 且 Start() 成功后可调用）
    /// 线程安全，可在任意业务线程调用；timestamp_us 为媒体时间戳，0 表示 SDK 内部按单调钟生成。
    bool PushExternalI420(const uint8_t* data_y, int stride_y,
                          const uint8_t* data_u, int stride_u,
                          const uint8_t* data_v, int stride_v,
                          int width, int height, int64_t timestamp_us);
    bool PushExternalI420Contiguous(const uint8_t* buf, uint32_t size,
                                    int width, int height, int64_t timestamp_us);
    bool PushExternalNv12(const uint8_t* data_y, int stride_y,
                          const uint8_t* data_uv, int stride_uv,
                          int width, int height, int64_t timestamp_us);
    bool PushExternalNv12Contiguous(const uint8_t* buf, uint32_t size,
                                    int width, int height, int64_t timestamp_us);

    /// 获取采集帧数（需先 SetOnFrameCallback 或 --test-capture）
    unsigned int GetFrameCount() const;

    /// 获取解码帧数（--test-encode 模式下，接收端收到的 H264 解码后帧数）
    unsigned int GetDecodedFrameCount() const;

    /// 是否正在推流
    bool IsStreaming() const { return is_streaming_.load(std::memory_order_acquire); }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> is_streaming_{false};
};

}  // namespace webrtc_demo

#endif  // PUSH_STREAMER_H
