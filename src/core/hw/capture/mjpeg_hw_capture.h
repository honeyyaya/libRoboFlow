#ifndef __RFLOW_CORE_HW_CAPTURE_MJPEG_HW_CAPTURE_H__
#define __RFLOW_CORE_HW_CAPTURE_MJPEG_HW_CAPTURE_H__

#include <cstddef>
#include <cstdint>
#include <memory>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

namespace webrtc {
class I420Buffer;
class NV12Buffer;
}

namespace rflow::hw::capture {

/// V4L2 MJPEG 硬件解码门面（采集链路）；业务层不依赖 platform/rockchip。
class IMjpegHwCapture {
 public:
  virtual ~IMjpegHwCapture() = default;

  virtual bool Init() = 0;
  virtual void Close() = 0;

  virtual void SetPipelineV4l2ExtDmabuf(bool enable) = 0;
  virtual void SetPipelineRgaToMpp(bool enable) = 0;
  virtual void SetOutputBufferPoolLimit(int max_buffers, int width, int height) = 0;

  virtual bool AllocateInputDmabuf(size_t size,
                                 void** out_handle,
                                 int* out_fd,
                                 uint8_t** out_ptr,
                                 size_t* out_capacity) = 0;
  virtual void ReleaseInputDmabuf(void* handle) = 0;

  virtual bool DecodeJpegToNV12(const uint8_t* jpeg,
                                size_t jpeg_len,
                                int expect_w,
                                int expect_h,
                                webrtc::NV12Buffer* out_nv12,
                                int dma_buf_fd = -1,
                                size_t dma_buf_capacity = 0) = 0;

  /// 零拷贝 native 输出（底层为 MPP DRM buffer，表现为 webrtc kNative）。
  virtual bool DecodeJpegToNativeFrame(const uint8_t* jpeg,
                                       size_t jpeg_len,
                                       int expect_w,
                                       int expect_h,
                                       webrtc::scoped_refptr<webrtc::VideoFrameBuffer>* out,
                                       int dma_buf_fd = -1,
                                       size_t dma_buf_capacity = 0,
                                       int64_t dq_time_us = 0,
                                       int64_t v4l2_timestamp_us = 0,
                                       int64_t poll_wait_us = 0,
                                       int64_t dqbuf_ioctl_us = 0,
                                       int64_t decode_queue_wait_us = 0) = 0;
};

/// 编译期可用时返回 Rockchip MPP 实现，否则 nullptr。
std::shared_ptr<IMjpegHwCapture> CreateMjpegHwCapture();

}  // namespace rflow::hw::capture

#endif  // __RFLOW_CORE_HW_CAPTURE_MJPEG_HW_CAPTURE_H__
