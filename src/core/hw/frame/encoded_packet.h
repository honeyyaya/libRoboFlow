#ifndef __RFLOW_CORE_HW_FRAME_ENCODED_PACKET_H__
#define __RFLOW_CORE_HW_FRAME_ENCODED_PACKET_H__

#include "hw/types/codec_id.h"

#include "rflow/librflow_common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rflow::hw {

class IEncodedPacket {
 public:
  virtual ~IEncodedPacket() = default;
  virtual const uint8_t* data() const = 0;
  virtual size_t size() const = 0;
  virtual bool is_keyframe() const = 0;
  virtual int64_t pts_us() const = 0;
  virtual HwCodecId codec() const = 0;
  virtual bool is_annex_b() const = 0;
};

/// 持有 Annex-B 码流副本的简单实现。
class EncodedPacketBuffer : public IEncodedPacket {
 public:
  EncodedPacketBuffer(std::vector<uint8_t> bytes, HwCodecId codec, bool keyframe, int64_t pts_us, bool annex_b = true);

  const uint8_t* data() const override;
  size_t size() const override;
  bool is_keyframe() const override;
  int64_t pts_us() const override;
  HwCodecId codec() const override;
  bool is_annex_b() const override;

 private:
  std::vector<uint8_t> bytes_;
  HwCodecId codec_;
  bool keyframe_;
  int64_t pts_us_;
  bool annex_b_;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_ENCODED_PACKET_H__
