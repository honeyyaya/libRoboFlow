#ifndef __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__
#define __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__

#include "hw/types/codec_id.h"
#include "rflow/librflow_common.h"

namespace rflow::rtc::hw {

inline const char* CodecToString(rflow_codec_t codec) {
  return ::rflow::hw::CodecName(::rflow::hw::CodecFromAbi(codec));
}

}  // namespace rflow::rtc::hw

#endif  // __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__
