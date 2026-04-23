/**
 * @file   rtc_stream_frame_converter.h
 * @brief  rtc stream frame converter: webrtc::VideoFrame → librflow_video_frame_s
 *
 * 说明：
 *   - 固定输出 I420（YYY..UUU..VVV..），refcount 初值 1；
 *     上层回调用户 on_video 后调用 librflow_video_frame_release 归还。
 *   - utc_ms 采用 system_clock；pts_ms 使用 webrtc::VideoFrame::timestamp_us()/1000。
 *   - libwebrtc 不暴露关键帧标志，type 统一填 RFLOW_FRAME_UNKNOWN；
 *     如后续需区分 I/P，应从 RtpPacketInfo/RtpReceiver 统计拿。
 */

#ifndef __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__
#define __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__

#include <cstdint>

#include "rflow/librflow_common.h"

namespace webrtc { class VideoFrame; }

namespace rflow::client::impl {

/// 失败返回 nullptr；成功返回 refcount=1 的对象，调用方需 release。
librflow_video_frame_t MakeVideoFrameFromRtcFrame(const webrtc::VideoFrame& frame,
                                                  int32_t                   stream_index,
                                                  uint32_t                  seq);

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__
