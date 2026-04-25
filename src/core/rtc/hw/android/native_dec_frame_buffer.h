#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <android/hardware_buffer.h>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

namespace rflow::rtc {

class AndroidNativeDecFrameBuffer : public webrtc::VideoFrameBuffer {
 public:
    static webrtc::scoped_refptr<AndroidNativeDecFrameBuffer> Create(
        AHardwareBuffer* hardware_buffer,
        int width,
        int height,
        int sync_fence_fd = -1,
        webrtc::scoped_refptr<webrtc::VideoFrameBuffer> mapped_buffer = nullptr);

    static AndroidNativeDecFrameBuffer* TryGet(
        const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer);

    AndroidNativeDecFrameBuffer(AHardwareBuffer* hardware_buffer,
                                int width,
                                int height,
                                int sync_fence_fd,
                                webrtc::scoped_refptr<webrtc::VideoFrameBuffer> mapped_buffer);

    webrtc::VideoFrameBuffer::Type type() const override;
    int width() const override;
    int height() const override;
    webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> GetMappedFrameBuffer(
        webrtc::ArrayView<Type> types) override;
    std::string storage_representation() const override;

    AHardwareBuffer* hardware_buffer() const { return hardware_buffer_; }
    int sync_fence_fd() const { return sync_fence_fd_; }

 protected:
    ~AndroidNativeDecFrameBuffer() override;

 private:
    AHardwareBuffer* hardware_buffer_;
    int width_;
    int height_;
    int sync_fence_fd_;
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> mapped_buffer_;
};

}  // namespace rflow::rtc
