#ifndef __RFLOW_CORE_HW_FRAME_FRAME_BUFFER_H__
#define __RFLOW_CORE_HW_FRAME_FRAME_BUFFER_H__

#include "hw/registry/backend_id.h"
#include "hw/types/memory_type.h"
#include "hw/types/pixel_format.h"

#include <cstdint>
#include <memory>

namespace rflow::hw {

struct PlaneView {
  const uint8_t* data{nullptr};
  uint32_t stride{0};
  uint32_t width{0};
  uint32_t height{0};
};

/// 平台无关视频帧缓冲抽象。生命周期由 std::shared_ptr 管理。
class IFrameBuffer {
 public:
  virtual ~IFrameBuffer() = default;

  virtual HwMemoryType memory_type() const = 0;
  virtual HwPixelFormat pixel_format() const = 0;
  virtual HwBackendId backend_id() const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual uint32_t plane_count() const = 0;
  virtual PlaneView plane(uint32_t index) const = 0;

  /// dmabuf fd；非 kDmabufFd 时返回 -1。
  virtual int dmabuf_fd(int plane = 0) const;
  /// 厂商原生 handle + tag；无则返回 nullptr。
  virtual void* native_handle(HwNativeHandleTag* out_tag) const;

  /// 阻塞映射为 CPU 可读缓冲；失败返回 nullptr。
  virtual std::shared_ptr<IFrameBuffer> MapToCpu(HwPixelFormat want) const;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_FRAME_BUFFER_H__
