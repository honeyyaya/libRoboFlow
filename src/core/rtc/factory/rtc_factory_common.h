#ifndef __RFLOW_CORE_RTC_FACTORY_RTC_FACTORY_COMMON_H__
#define __RFLOW_CORE_RTC_FACTORY_RTC_FACTORY_COMMON_H__

#include "abi/object_layouts.h"
#include "base/global_config_ops.h"

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

/// 将 SDK global_config 写入 State 并同步 FlexFEC / ICE ignore / log 等运行时项。
void ApplySdkGlobalConfig(librflow_global_config_s& dst, const librflow_global_config_s& src);

/// uninit 时清除 SDK 注入的 FlexFEC / ICE ignore 覆盖。
void ResetSdkGlobalConfigOverrides();

}  // namespace rflow::rtc

#endif  // __RFLOW_CORE_RTC_FACTORY_RTC_FACTORY_COMMON_H__
