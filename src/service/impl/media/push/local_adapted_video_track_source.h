#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PUSH_LOCAL_ADAPTED_VIDEO_TRACK_SOURCE_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PUSH_LOCAL_ADAPTED_VIDEO_TRACK_SOURCE_H__

#include <optional>

#include "media/base/adapted_video_track_source.h"

namespace rflow::service::impl {

/// 本地视频源（采集 / 外部推帧）共用的 AdaptedVideoTrackSource 适配层。
class LocalAdaptedVideoTrackSource : public webrtc::AdaptedVideoTrackSource {
protected:
    LocalAdaptedVideoTrackSource() = default;
    ~LocalAdaptedVideoTrackSource() override = default;

public:
    webrtc::MediaSourceInterface::SourceState state() const override {
        return webrtc::MediaSourceInterface::kLive;
    }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    std::optional<bool> needs_denoising() const override { return std::nullopt; }
};

}  // namespace rflow::service::impl

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PUSH_LOCAL_ADAPTED_VIDEO_TRACK_SOURCE_H__
