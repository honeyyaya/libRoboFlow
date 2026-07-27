#include "hw/frame/frame_bridge.h"

#include "hw/frame/simple_video_frame.h"
#include "hw/types/codec_id.h"
#include "media/frame_types.h"

namespace rflow::hw {

namespace {

class CpuHostFrameBuffer : public IFrameBuffer {
 public:
  CpuHostFrameBuffer(HwPixelFormat fmt, int width, int height, std::vector<const uint8_t*> planes, std::vector<uint32_t> strides)
      : fmt_(fmt), width_(width), height_(height), planes_(std::move(planes)), strides_(std::move(strides)) {}

  HwMemoryType memory_type() const override { return HwMemoryType::kCpuHost; }
  HwPixelFormat pixel_format() const override { return fmt_; }
  HwBackendId backend_id() const override { return HwBackendId::kBuiltin; }
  int width() const override { return width_; }
  int height() const override { return height_; }
  uint32_t plane_count() const override { return static_cast<uint32_t>(planes_.size()); }

  PlaneView plane(uint32_t index) const override {
    PlaneView view;
    if (index >= planes_.size()) {
      return view;
    }
    view.data = planes_[index];
    view.stride = index < strides_.size() ? strides_[index] : 0;
    view.width = width_;
    view.height = (index == 0) ? static_cast<uint32_t>(height_) : static_cast<uint32_t>(height_ / 2);
    return view;
  }

 private:
  HwPixelFormat fmt_;
  int width_;
  int height_;
  std::vector<const uint8_t*> planes_;
  std::vector<uint32_t> strides_;
};

}  // namespace

bool ExportToAbi(const IVideoFrame& frame, librflow_video_frame_s* out) {
  if (!out) {
    return false;
  }
  const auto buffer = frame.buffer();
  if (!buffer) {
    return false;
  }
  const FrameMetadata meta = frame.metadata();
  out->codec = CodecToAbi(meta.codec);
  out->type = meta.frame_type;
  out->width = static_cast<uint32_t>(buffer->width());
  out->height = static_cast<uint32_t>(buffer->height());
  out->pts_ms = meta.pts_us > 0 ? static_cast<uint64_t>(meta.pts_us / 1000) : 0;
  out->utc_ms = static_cast<uint64_t>(meta.utc_ms);
  out->seq = meta.seq;
  out->stream_index = meta.stream_index;

  switch (buffer->memory_type()) {
    case HwMemoryType::kCpuHost:
      out->backend = RFLOW_VIDEO_FRAME_BACKEND_CPU_PLANAR;
      break;
    case HwMemoryType::kDmabufFd:
    case HwMemoryType::kOpaqueNative:
      out->backend = RFLOW_VIDEO_FRAME_BACKEND_HARDWARE_BUFFER;
      break;
    default:
      out->backend = RFLOW_VIDEO_FRAME_BACKEND_UNKNOWN;
      break;
  }

  out->native_handle_type = RFLOW_NATIVE_HANDLE_NONE;
  out->plane_count = buffer->plane_count();
  for (uint32_t i = 0; i < out->plane_count && i < 3; ++i) {
    const PlaneView p = buffer->plane(i);
    out->plane_data[i] = p.data;
    out->plane_strides[i] = p.stride;
    out->plane_widths[i] = p.width;
    out->plane_heights[i] = p.height;
  }
  return true;
}

std::shared_ptr<IVideoFrame> ImportFromAbi(const librflow_video_frame_s* in) {
  if (!in || in->plane_count == 0) {
    return nullptr;
  }
  HwPixelFormat fmt = HwPixelFormat::kUnknown;
  if (in->plane_count >= 3) {
    fmt = HwPixelFormat::kI420;
  } else if (in->plane_count == 2) {
    fmt = HwPixelFormat::kNv12;
  }
  std::vector<const uint8_t*> planes;
  std::vector<uint32_t> strides;
  planes.reserve(in->plane_count);
  strides.reserve(in->plane_count);
  for (uint32_t i = 0; i < in->plane_count && i < 3; ++i) {
    planes.push_back(in->plane_data[i]);
    strides.push_back(in->plane_strides[i]);
  }
  auto buffer = std::make_shared<CpuHostFrameBuffer>(fmt, static_cast<int>(in->width), static_cast<int>(in->height),
                                                       std::move(planes), std::move(strides));
  FrameMetadata meta;
  meta.codec = CodecFromAbi(in->codec);
  meta.frame_type = in->type;
  meta.pts_us = static_cast<int64_t>(in->pts_ms) * 1000;
  meta.utc_ms = static_cast<int64_t>(in->utc_ms);
  meta.seq = in->seq;
  meta.stream_index = in->stream_index;
  return std::make_shared<SimpleVideoFrame>(std::move(buffer), meta);
}

}  // namespace rflow::hw
