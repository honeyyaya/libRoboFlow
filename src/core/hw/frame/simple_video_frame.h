#ifndef __RFLOW_CORE_HW_FRAME_SIMPLE_VIDEO_FRAME_H__
#define __RFLOW_CORE_HW_FRAME_SIMPLE_VIDEO_FRAME_H__

#include "hw/frame/video_frame.h"

#include <memory>

namespace rflow::hw {

class SimpleVideoFrame : public IVideoFrame {
 public:
  SimpleVideoFrame(std::shared_ptr<IFrameBuffer> buffer, FrameMetadata metadata);

  std::shared_ptr<IFrameBuffer> buffer() const override;
  FrameMetadata metadata() const override;

 private:
  std::shared_ptr<IFrameBuffer> buffer_;
  FrameMetadata metadata_;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_SIMPLE_VIDEO_FRAME_H__
