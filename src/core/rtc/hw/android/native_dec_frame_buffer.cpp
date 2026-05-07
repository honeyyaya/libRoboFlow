#include "core/rtc/hw/android/native_dec_frame_buffer.h"

#include <unistd.h>

namespace rflow::rtc {

namespace {
constexpr char kStorageRepresentation[] = "android_ahardwarebuffer";
}  // namespace

webrtc::scoped_refptr<AndroidNativeDecFrameBuffer> AndroidNativeDecFrameBuffer::Create(
    AImage*          image,
    AHardwareBuffer* hardware_buffer,
    int              sync_fence_fd,
    int              width,
    int              height) {
    if (!hardware_buffer || width <= 0 || height <= 0) {
        return nullptr;
    }
    return webrtc::scoped_refptr<AndroidNativeDecFrameBuffer>(
        new AndroidNativeDecFrameBuffer(image, hardware_buffer, sync_fence_fd, width, height));
}

AndroidNativeDecFrameBuffer* AndroidNativeDecFrameBuffer::TryGet(
    const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer) {
    return TryGet(buffer.get());
}

AndroidNativeDecFrameBuffer* AndroidNativeDecFrameBuffer::TryGet(
    webrtc::VideoFrameBuffer* buffer) {
    if (!buffer || buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
        return nullptr;
    }
    if (buffer->storage_representation() != kStorageRepresentation) {
        return nullptr;
    }
    return static_cast<AndroidNativeDecFrameBuffer*>(buffer);
}

AndroidNativeDecFrameBuffer::AndroidNativeDecFrameBuffer(
    AImage*          image,
    AHardwareBuffer* hardware_buffer,
    int              sync_fence_fd,
    int              width,
    int              height)
    : image_(image),
      hardware_buffer_(hardware_buffer),
      sync_fence_fd_(sync_fence_fd),
      width_(width),
      height_(height) {
#if __ANDROID_API__ >= 26
    if (hardware_buffer_) {
        AHardwareBuffer_acquire(hardware_buffer_);
    }
#endif
    // image_ 由本对象接管：析构时 AImage_delete 才会让 ImageReader 槽位回收，
    // 否则在 GL 消费仍在使用 AHardwareBuffer 的瞬间提前释放会让纹理变黑/花屏。
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
    if (image_) {
        AImage_delete(image_);
        image_ = nullptr;
    }
#endif
}

void AndroidNativeDecFrameBuffer::AddRef() const {
    ref_count_.IncRef();
}

webrtc::RefCountReleaseStatus AndroidNativeDecFrameBuffer::Release() const {
    const auto status = ref_count_.DecRef();
    if (status == webrtc::RefCountReleaseStatus::kDroppedLastRef) {
        delete this;
    }
    return status;
}

webrtc::VideoFrameBuffer::Type AndroidNativeDecFrameBuffer::type() const {
    return Type::kNative;
}

int AndroidNativeDecFrameBuffer::width()  const { return width_;  }
int AndroidNativeDecFrameBuffer::height() const { return height_; }

webrtc::scoped_refptr<webrtc::I420BufferInterface> AndroidNativeDecFrameBuffer::ToI420() {
    // 仅支持 native 渲染路径；CPU 读取需消费侧自行 AHardwareBuffer_lock 并转换。
    return nullptr;
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer>
AndroidNativeDecFrameBuffer::GetMappedFrameBuffer(webrtc::ArrayView<Type> /*types*/) {
    return nullptr;
}

std::string AndroidNativeDecFrameBuffer::storage_representation() const {
    return kStorageRepresentation;
}

int AndroidNativeDecFrameBuffer::ConsumeSyncFenceFd() const {
    const int fd = sync_fence_fd_;
    sync_fence_fd_ = -1;
    return fd;
}

}  // namespace rflow::rtc
