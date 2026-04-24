#ifndef __RFLOW_CORE_RTC_FACTORY_COMMON_H__
#define __RFLOW_CORE_RTC_FACTORY_COMMON_H__

#include "api/audio/audio_device.h"
#include "api/scoped_refptr.h"

namespace rflow::rtc {

void EnsureWebrtcFieldTrialsInitialized();
webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateDummyAudioDeviceModule();

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_FACTORY_COMMON_H__
