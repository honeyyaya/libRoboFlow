#ifndef __RFLOW_CORE_RTC_FACTORY_COMMON_H__
#define __RFLOW_CORE_RTC_FACTORY_COMMON_H__

#include "api/audio/audio_device.h"
#include "api/scoped_refptr.h"

namespace rflow::rtc {

void EnsureWebrtcFieldTrialsInitialized();
webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateDummyAudioDeviceModule();

/// 生命周期：在对端 `*_set_global_config` 拷贝完 `librflow_global_config_s` 之后调用，
/// 在首次 `EnsureWebrtcFieldTrialsInitialized` 前生效。
void NotifyFlexfecTrialFromSdkConfig(bool explicitly_set, bool enabled);

/// process 结束前 uninit：清除 FlexFEC SDK 覆盖，还原为仅用 RFLOW_ENABLE_FLEXFEC 语义。
void ResetFlexfecTrialSdkOverride(void);

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_FACTORY_COMMON_H__
