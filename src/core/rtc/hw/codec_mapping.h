#ifndef __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__
#define __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__

#include "rflow/librflow_common.h"

namespace rflow::rtc::hw {

inline const char* CodecToString(rflow_codec_t codec) {
    switch (codec) {
        case RFLOW_CODEC_H264:
            return "h264";
        case RFLOW_CODEC_H265:
            return "h265";
        case RFLOW_CODEC_MJPEG:
            return "mjpeg";
        default:
            return "h264";
    }
}

}  // namespace rflow::rtc::hw

#endif  // __RFLOW_CORE_RTC_HW_CODEC_MAPPING_H__
