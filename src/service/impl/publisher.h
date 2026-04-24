/**
 * @file   publisher.h
 * @brief  Service C ABI ↔ rflow::service::impl::PushStreamer 桥接层
 *
 * 负责：
 *   1. 根据 librflow_svc_stream_param_s 构造 rflow::service::impl::PushStreamerConfig（启用 external source）
 *   2. 管理 rflow::service::impl::SignalingClient：subscriber_join→排队→主 worker 线程 CreateOfferForPeer
 *   3. 把 answer / ice 回调路由回 PushStreamer
 *   4. subscriber_join/leave → 通过 ctx 中的 connect_cb 分发 on_pull_request / on_pull_release
 *   5. 业务层 librflow_svc_push_video_frame → PushStreamer::PushExternalI420/Nv12
 */

#ifndef RFLOW_SERVICE_IMPL_PUBLISHER_H_
#define RFLOW_SERVICE_IMPL_PUBLISHER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rflow/Service/librflow_service_api.h"

namespace rflow::service::impl {

class PushStreamer;
class SignalingClient;

struct PublisherPullCallbacks {
    librflow_svc_on_pull_request_fn on_pull_request{nullptr};
    librflow_svc_on_pull_release_fn on_pull_release{nullptr};
    void*                           userdata{nullptr};
};

class Publisher {
public:
    Publisher(int32_t stream_idx,
              rflow_codec_t in_codec,
              const std::string& stream_id_str,
              const std::string& signaling_url,
              const std::string& device_id,
              int width, int height, int fps,
              int target_kbps, int min_kbps, int max_kbps,
              const std::string& video_codec,
              bool use_internal_video_source,
              const std::string& video_device_path,
              int video_device_index,
              const PublisherPullCallbacks& cbs);
    ~Publisher();

    Publisher(const Publisher&)            = delete;
    Publisher& operator=(const Publisher&) = delete;

    /// 构造内部 PushStreamer/SignalingClient 并启动。返回 false 时内部已回滚。
    bool Start();
    /// 对等方 CreateOfferForPeer 的排队机制全部停止；PushStreamer 关闭。
    void Stop();

    bool PushI420(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us);
    bool PushNv12(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us);

    int32_t stream_idx() const { return stream_idx_; }
    rflow_codec_t in_codec() const { return in_codec_; }
    bool uses_external_video_source() const { return !use_internal_video_source_; }

private:
    void WorkerLoop();

    int32_t       stream_idx_;
    rflow_codec_t in_codec_;
    std::string   stream_id_str_;
    std::string   signaling_url_;
    std::string   device_id_;
    int           width_{0};
    int           height_{0};
    int           fps_{30};
    int           target_kbps_{1000};
    int           min_kbps_{100};
    int           max_kbps_{2000};
    std::string   video_codec_{"h264"};
    bool          use_internal_video_source_{false};
    std::string   video_device_path_;
    int           video_device_index_{0};
    PublisherPullCallbacks cbs_{};

    std::unique_ptr<PushStreamer>    streamer_;
    std::unique_ptr<SignalingClient> signaling_;

    std::thread             worker_;
    std::atomic<bool>       worker_run_{false};
    std::mutex              pending_mu_;
    std::condition_variable pending_cv_;
    std::vector<std::string> pending_subs_;
};

}  // namespace rflow::service::impl

#endif  // RFLOW_SERVICE_IMPL_PUBLISHER_H_
