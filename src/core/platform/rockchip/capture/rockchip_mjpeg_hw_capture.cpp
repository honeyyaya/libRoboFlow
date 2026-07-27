#include "platform/rockchip/capture/rockchip_mjpeg_hw_capture.h"

#include "platform/rockchip/mjpeg_decoder.h"
#include "platform/rockchip/native_dec_frame_buffer.h"

namespace rflow::hw::capture::rockchip {
namespace {

class RockchipMjpegHwCapture final : public IMjpegHwCapture,
                                     public std::enable_shared_from_this<RockchipMjpegHwCapture> {
 public:
  bool Init() override { return decoder_->Init(); }
  void Close() override { decoder_->Close(); }

  void SetPipelineV4l2ExtDmabuf(bool enable) override { decoder_->SetPipelineV4l2ExtDmabuf(enable); }
  void SetPipelineRgaToMpp(bool enable) override { decoder_->SetPipelineRgaToMpp(enable); }

  void SetOutputBufferPoolLimit(int max_buffers, int width, int height) override {
    decoder_->SetOutputBufferPoolLimit(max_buffers, width, height);
  }

  bool AllocateInputDmabuf(size_t size,
                           void** out_handle,
                           int* out_fd,
                           uint8_t** out_ptr,
                           size_t* out_capacity) override {
    return decoder_->AllocateInputDmabuf(size, out_handle, out_fd, out_ptr, out_capacity);
  }

  void ReleaseInputDmabuf(void* handle) override { decoder_->ReleaseInputDmabuf(handle); }

  bool DecodeJpegToNV12(const uint8_t* jpeg,
                        size_t jpeg_len,
                        int expect_w,
                        int expect_h,
                        webrtc::NV12Buffer* out_nv12,
                        int dma_buf_fd,
                        size_t dma_buf_capacity) override {
    return decoder_->DecodeJpegToNV12(jpeg, jpeg_len, expect_w, expect_h, out_nv12, dma_buf_fd, dma_buf_capacity);
  }

  bool DecodeJpegToNativeFrame(const uint8_t* jpeg,
                               size_t jpeg_len,
                               int expect_w,
                               int expect_h,
                               webrtc::scoped_refptr<webrtc::VideoFrameBuffer>* out,
                               int dma_buf_fd,
                               size_t dma_buf_capacity,
                               int64_t dq_time_us,
                               int64_t v4l2_timestamp_us,
                               int64_t poll_wait_us,
                               int64_t dqbuf_ioctl_us,
                               int64_t decode_queue_wait_us) override {
    if (!out) {
      return false;
    }
    webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native;
    const bool ok = decoder_->DecodeJpegToNativeDecFrame(
        jpeg, jpeg_len, expect_w, expect_h, &native, dma_buf_fd, dma_buf_capacity, dq_time_us, v4l2_timestamp_us,
        poll_wait_us, dqbuf_ioctl_us, decode_queue_wait_us, decoder_);
    if (!ok || !native) {
      return false;
    }
    *out = native;
    return true;
  }

 private:
  std::shared_ptr<rflow::rtc::hw::rockchip_mpp::RkMppMjpegDecoder> decoder_{
      std::make_shared<rflow::rtc::hw::rockchip_mpp::RkMppMjpegDecoder>()};
};

}  // namespace

std::shared_ptr<IMjpegHwCapture> CreateRockchipMjpegHwCapture() {
  return std::make_shared<RockchipMjpegHwCapture>();
}

}  // namespace rflow::hw::capture::rockchip
