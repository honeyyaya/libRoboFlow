#ifndef __RFLOW_CORE_HW_FRAME_VIDEO_FRAME_H__
#define __RFLOW_CORE_HW_FRAME_VIDEO_FRAME_H__

#include "hw/frame/frame_buffer.h"
#include "hw/types/codec_id.h"

#include "rflow/librflow_common.h"

#include <cstdint>
#include <memory>

namespace rflow::hw {

struct FrameMetadata {
  int64_t pts_us{0};
  int64_t utc_ms{0};
  uint32_t seq{0};
  int32_t stream_index{0};
  rflow_frame_type_t frame_type{RFLOW_FRAME_UNKNOWN};
  HwCodecId codec{HwCodecId::kUnknown};
};

class IVideoFrame {
 public:
  virtual ~IVideoFrame() = default;
  virtual std::shared_ptr<IFrameBuffer> buffer() const = 0;
  virtual FrameMetadata metadata() const = 0;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_VIDEO_FRAME_H__
