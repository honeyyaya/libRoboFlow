#ifndef __RFLOW_COMMON_MEDIA_VIDEO_FRAME_SOURCE_H__
#define __RFLOW_COMMON_MEDIA_VIDEO_FRAME_SOURCE_H__

// 框架无关的 video frame source 角色接口。
//
// 现状：service 端两个 producer（CameraVideoTrackSource / ExternalPushVideoTrackSource）
// 都直接继承 webrtc::AdaptedVideoTrackSource，把"产品语义（采集 / 业务推帧）"和
// "WebRTC adapter 语义"纠缠在一起。
//
// 该接口不试图替换 WebRTC adapter，而是把"我是个视频源 + 我有这些通用产品级元信息"
// 这一角色独立显式化：
//   * Camera 之类的设备采集源 → IVideoCaptureSource
//   * 业务通过 push_video_frame 投递的源 → IVideoExternalSource
//
// 上层只想做"心跳/告警/可观测性"时，可以仅依赖 IVideoFrameSource 而不感知 WebRTC。
// 后续若要替换非 WebRTC 后端，也可以从这一层切入。
//
// 该接口仅做标识 + 简单 metadata 暴露，刻意保持薄；不引入任何媒体生命周期管理，
// 避免和现有 webrtc::AdaptedVideoTrackSource 体系产生重复。

#include <cstdint>

namespace rflow::common::media {

enum class VideoFrameSourceKind {
  kUnknown = 0,
  kV4l2Capture,         ///< Linux V4L2 直采 / WebRTC VCM
  kExternalPush,        ///< 业务 librflow_svc_push_video_frame 投递
};

class IVideoFrameSource {
 public:
  virtual ~IVideoFrameSource() = default;

  /// 此 source 的"产品语义"分类。
  virtual VideoFrameSourceKind frame_source_kind() const noexcept = 0;

  /// source 已派发到下游的有效帧累计数；用于上层心跳/告警/调试。
  /// 该计数不要求严格原子精度，但必须单调递增。
  virtual std::uint32_t dispatched_frame_count() const noexcept = 0;
};

/// 设备采集源：要 Start 后才会产帧；可查询协商后的真实采集分辨率/帧率。
class IVideoCaptureSource : public IVideoFrameSource {
 public:
  /// 协商后的最终采集尺寸（与 Start 请求可能不一致；驱动可能强制 fallback）。
  /// @return false 表示尚未 Start 或协商未成功；不写出参。
  virtual bool GetNegotiatedCaptureSize(int* width, int* height) const = 0;

  /// 协商后的最终采集帧率；语义同 GetNegotiatedCaptureSize。
  virtual bool GetNegotiatedCaptureFramerate(int* out_fps) const = 0;

  VideoFrameSourceKind frame_source_kind() const noexcept override {
    return VideoFrameSourceKind::kV4l2Capture;
  }
};

/// 业务推帧源：上层主动 push，不内置采集线程。
class IVideoExternalSource : public IVideoFrameSource {
 public:
  VideoFrameSourceKind frame_source_kind() const noexcept override {
    return VideoFrameSourceKind::kExternalPush;
  }
};

}  // namespace rflow::common::media

#endif  // __RFLOW_COMMON_MEDIA_VIDEO_FRAME_SOURCE_H__
