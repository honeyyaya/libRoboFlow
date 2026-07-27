/**
 * @file   pull_subscriber_video_sink.h
 * @brief  PullSubscriber 内部使用的解码后帧 sink。
 *
 * 抽出主要为了把 pull_subscriber.cpp 主翻译单元从 ~700 行收敛到只关注 RTC 编排，
 * 同时给后续可能的"自定义 sink 注入"留出独立替换点。VideoSink 与 PullSubscriber::Impl
 * 之间没有 private 状态依赖：仅通过构造时注入 OnVideoFrameCallback / StatsCallback 通信。
 */
#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_VIDEO_SINK_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_VIDEO_SINK_H__

#include <cstdint>
#include <functional>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

#include "media/pull/pull_subscriber.h"

namespace rflow::service::impl::observers {

/// 远端解码视频帧 sink：在 PullSubscriber::Impl::OnTrack 时挂到 VideoTrackInterface 上。
/// 负责（可选）I420→ARGB 转换、E2E latency trace、media timing trace 等纯展示路径。
/// 注意：所有 trace 开关都是 cold-path，不影响主帧投递性能。
class VideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    using Callback = PullSubscriber::OnVideoFrameCallback;
    using StatsCallback = std::function<void(uint16_t trace_id, int64_t t_callback_done_us)>;

    explicit VideoSink(Callback cb, bool skip_argb_conversion, StatsCallback stats_cb = nullptr);

    void OnFrame(const webrtc::VideoFrame& frame) override;

private:
    Callback on_frame_;
    bool skip_argb_conversion_{false};
    StatsCallback on_frame_stats_;
    std::vector<uint8_t> argb_;
};

}  // namespace rflow::service::impl::observers

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PULL_PULL_SUBSCRIBER_VIDEO_SINK_H__
