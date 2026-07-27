/**
 * @file   capture_fps_pipeline_policy.h
 * @brief  按目标采集帧率（30 / 60 fps 等）选择 V4L2/MJPEG 推流管线 preset。
 *
 * 默认 30fps 走低延迟（latest_only + 浅队列）；≥45fps 走高保真（更深缓冲、
 * 不整队清空），在满帧率前提下仍保持较短 poll 超时。
 */
#ifndef __RFLOW_SERVICE_IMPL_MEDIA_POLICY_CAPTURE_FPS_PIPELINE_POLICY_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_POLICY_CAPTURE_FPS_PIPELINE_POLICY_H__

#include "media/push/push_streamer.h"

namespace rflow::service::impl::policy {

/// 30fps 与 60fps 两档 preset 的语义标签（阈值见 ClassifyCaptureFpsTier）。
enum class CaptureFpsTier {
    kLowLatency,    ///< ~30fps：latest_only，浅 V4L2 队列
    kHighFidelity,  ///< ~60fps：保留排队帧，允许解码短暂落后
};

/// 将请求 fps 映射到 30 / 60 档；≥45 视为高帧率档。
CaptureFpsTier ClassifyCaptureFpsTier(int requested_fps);

/// MJPEG 解码队列 stale 丢弃预算（毫秒）。环境变量 RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS
/// 若已设置则优先；否则按档位给默认值（60 档约 3 帧周期，30 档 25ms）。
int MjpegDecodeQueueMaxWaitMsForFps(int requested_fps);

/// 根据档位写入 PushStreamerBackendConfig 的采集/MJPEG 字段（不改动 MPP 开关）。
void ApplyCaptureFpsPipelineDefaults(PushStreamerBackendConfig& backend, int requested_fps);

/// ≥45fps 且 RFLOW_MEDIA_THREAD_AUTO 未关闭时，V4L2/MJPEG 线程自动提优先级（nice）。
bool ShouldAutoTuneMediaThreadsForFps(int requested_fps);

}  // namespace rflow::service::impl::policy

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_POLICY_CAPTURE_FPS_PIPELINE_POLICY_H__
