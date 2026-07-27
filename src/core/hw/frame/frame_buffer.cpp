#include "hw/frame/frame_buffer.h"

namespace rflow::hw {

int IFrameBuffer::dmabuf_fd(int /*plane*/) const { return -1; }

void* IFrameBuffer::native_handle(HwNativeHandleTag* out_tag) const {
  if (out_tag) {
    *out_tag = HwNativeHandleTag::kNone;
  }
  return nullptr;
}

std::shared_ptr<IFrameBuffer> IFrameBuffer::MapToCpu(HwPixelFormat /*want*/) const {
  return nullptr;
}

}  // namespace rflow::hw
