#include "hw/frame/simple_video_frame.h"

namespace rflow::hw {

SimpleVideoFrame::SimpleVideoFrame(std::shared_ptr<IFrameBuffer> buffer, FrameMetadata metadata)
    : buffer_(std::move(buffer)), metadata_(metadata) {}

std::shared_ptr<IFrameBuffer> SimpleVideoFrame::buffer() const { return buffer_; }

FrameMetadata SimpleVideoFrame::metadata() const { return metadata_; }

}  // namespace rflow::hw
