#include "core/rtc/hw/android/native_dec_frame_buffer.h"

#include <unistd.h>

#include "api/video/i420_buffer.h"
#include "rtc_base/ref_counted_object.h"

namespace rflow::rtc {

webrtc::scoped_refptr<AndroidNativeDecFrameBuffer> AndroidNativeDecFrameBuffer::Create(
    AHardwareBuffer* hardware_buffer,
    int width,
    int height,
    int sync_fence_fd,
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> mapped_buffer) {
    if (!hardware_buffer || width <= 0 || height <= 0) {
        return nullptr;
    }
    return webrtc::scoped_refptr<AndroidNativeDecFrameBuffer>(
        new webrtc::RefCountedObject<AndroidNativeDecFrameBuffer>(
            hardware_buffer, width, height, sync_fence_fd, std::move(mapped_buffer)));
}

AndroidNativeDecFrameBuffer* AndroidNativeDecFrameBuffer::TryGet(
    const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer) {
    if (!buffer || buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
        return nullptr;
    }
    return dynamic_cast<AndroidNativeDecFrameBuffer*>(buffer.get());
}

AndroidNativeDecFrameBuffer::AndroidNativeDecFrameBuffer(
    AHardwareBuffer* hardware_buffer,
    int width,
    int height,
    int sync_fence_fd,
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> mapped_buffer)
    : hardware_buffer_(hardware_buffer),
      width_(width),
      height_(height),
      sync_fence_fd_(sync_fence_fd),
      mapped_buffer_(std::move(mapped_buffer)) {
#if __ANDROID_API__ >= 26
    if (hardware_buffer_) {
        AHardwareBuffer_acquire(hardware_buffer_);
    }
#endif
}

AndroidNativeDecFrameBuffer::~AndroidNativeDecFrameBuffer() {
    if (sync_fence_fd_ >= 0) {
        close(sync_fence_fd_);
        sync_fence_fd_ = -1;
    }
#if __ANDROID_API__ >= 26
    if (hardware_buffer_) {
        AHardwareBuffer_release(hardware_buffer_);
        hardware_buffer_ = nullptr;
    }
#endif
}

webrtc::VideoFrameBuffer::Type AndroidNativeDecFrameBuffer::type() const {
    return Type::kNative;
}

int AndroidNativeDecFrameBuffer::width() const {
    return width_;
}

int AndroidNativeDecFrameBuffer::height() const {
    return height_;
}

webrtc::scoped_refptr<webrtc::I420BufferInterface> AndroidNativeDecFrameBuffer::ToI420() {
    if (!mapped_buffer_) return nullptr;
    return mapped_buffer_->ToI420();
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> AndroidNativeDecFrameBuffer::GetMappedFrameBuffer(
    webrtc::ArrayView<Type> types) {
    if (!mapped_buffer_) return nullptr;
    for (Type type : types) {
        if (mapped_buffer_->type() == type) {
            return mapped_buffer_;
        }
    }
    if (mapped_buffer_->type() == Type::kNative) {
        return mapped_buffer_->GetMappedFrameBuffer(types);
    }
    return nullptr;
}

std::string AndroidNativeDecFrameBuffer::storage_representation() const {
    return "android_ahardwarebuffer";
}

}  // namespace rflow::rtc
