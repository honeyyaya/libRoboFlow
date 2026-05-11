#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/ref_counter.h"

namespace rflow::rtc {

/**
 * @brief AHardwareBuffer 视频帧 buffer。
 *
 * 由 Android MediaCodec 解码器构造，持有：
 *   - AImage*       —— ImageReader 槽位，析构时 AImage_delete 才会释放给 codec 复用；
 *   - AHardwareBuffer* —— 业务侧渲染入口（GL/EGL EGLImage / Vulkan VkImage）；
 *   - sync_fence_fd —— GPU/解码 -> 消费侧的 fence；
 *
 * Fence 语义：
 *   - 默认 sync_fence_fd 由本 buffer 持有，在析构时 close。
 *   - 调用方若希望接管 fence（例如把 fd 传给 EGL_ANDROID_native_fence_sync），
 *     调用 ConsumeSyncFenceFd() 一次性领取 fd 并自行 close；后续再读取返回 -1。
 */
class AndroidNativeDecFrameBuffer : public webrtc::VideoFrameBuffer {
 public:
    static webrtc::scoped_refptr<AndroidNativeDecFrameBuffer> Create(
        AImage*          image,
        AHardwareBuffer* hardware_buffer,
        int              sync_fence_fd,
        int              width,
        int              height);

    static AndroidNativeDecFrameBuffer* TryGet(
        const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer);
    static AndroidNativeDecFrameBuffer* TryGet(webrtc::VideoFrameBuffer* buffer);

    AndroidNativeDecFrameBuffer(AImage*          image,
                                AHardwareBuffer* hardware_buffer,
                                int              sync_fence_fd,
                                int              width,
                                int              height);

    void                          AddRef() const override;
    webrtc::RefCountReleaseStatus Release() const override;

    webrtc::VideoFrameBuffer::Type type() const override;
    int  width()  const override;
    int  height() const override;
    webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> GetMappedFrameBuffer(
        webrtc::ArrayView<Type> types) override;
    std::string storage_representation() const override;

    AHardwareBuffer* hardware_buffer() const { return hardware_buffer_; }
    int  sync_fence_fd()      const { return sync_fence_fd_; }
    int  ConsumeSyncFenceFd() const;

 protected:
    ~AndroidNativeDecFrameBuffer() override;

 private:
    AImage*          image_           = nullptr;
    AHardwareBuffer* hardware_buffer_ = nullptr;
    mutable int      sync_fence_fd_   = -1;
    int              width_           = 0;
    int              height_          = 0;
    mutable webrtc::webrtc_impl::RefCounter ref_count_{0};
};

}  // namespace rflow::rtc
