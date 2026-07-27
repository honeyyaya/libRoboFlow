#include "hw/frame/encoded_packet.h"

namespace rflow::hw {

EncodedPacketBuffer::EncodedPacketBuffer(std::vector<uint8_t> bytes,
                                         HwCodecId codec,
                                         bool keyframe,
                                         int64_t pts_us,
                                         bool annex_b)
    : bytes_(std::move(bytes)), codec_(codec), keyframe_(keyframe), pts_us_(pts_us), annex_b_(annex_b) {}

const uint8_t* EncodedPacketBuffer::data() const { return bytes_.data(); }

size_t EncodedPacketBuffer::size() const { return bytes_.size(); }

bool EncodedPacketBuffer::is_keyframe() const { return keyframe_; }

int64_t EncodedPacketBuffer::pts_us() const { return pts_us_; }

HwCodecId EncodedPacketBuffer::codec() const { return codec_; }

bool EncodedPacketBuffer::is_annex_b() const { return annex_b_; }

}  // namespace rflow::hw
