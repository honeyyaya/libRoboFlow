/**
 * @file   rtc_stream_frame_converter.h
 * @brief  Convert webrtc::VideoFrame into librflow_video_frame_s.
 *
 * Notes:
 *   - Preserve the incoming layout whenever possible:
 *       * NV12 stays NV12 (2 planes)
 *       * I420 stays I420 (3 planes)
 *       * Other layouts fall back to ToI420()
 *   - utc_ms uses system_clock; pts_ms uses VideoFrame::timestamp_us() / 1000.
 *   - Frame type is not exposed reliably on this path, so the SDK keeps
 *     RFLOW_FRAME_UNKNOWN.
 */

#ifndef __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__
#define __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__

#include <cstdint>

#include "rflow/librflow_common.h"

namespace webrtc { class VideoFrame; }

namespace rflow::client::impl {

/// Returns nullptr on failure. On success the returned frame has refcount=1.
librflow_video_frame_t MakeVideoFrameFromRtcFrame(const webrtc::VideoFrame& frame,
                                                  int32_t                   stream_index,
                                                  uint32_t                  seq);

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_RTC_STREAM_FRAME_CONVERTER_H__
