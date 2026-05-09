#ifndef __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__
#define __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/rtc/hw/codec_backend_capabilities.h"

namespace webrtc {
class VideoEncoderFactory;
class VideoDecoderFactory;
}

namespace rflow::rtc::hw {

enum class VideoCodecBackend {
  kBuiltin = 0,
  kRockchipMpp = 1,
  kAndroidMediaCodec = 2,
};

struct VideoBackendPreferences {
  VideoCodecBackend encoder_backend{VideoCodecBackend::kBuiltin};
  VideoCodecBackend decoder_backend{VideoCodecBackend::kBuiltin};
};

/// 一个 video backend 的全部声明式信息：
///   - 用 lambda 提供工厂构造和能力获取，registry 不需要直接 #include 任何 platform 头
///   - encoder_disabled_at_runtime / decoder_disabled_at_runtime 用于按环境变量
///     做 runtime 关停（典型如 WEBRTC_DISABLE_MPP_H264*）
struct VideoBackendDescriptor {
  VideoCodecBackend backend{VideoCodecBackend::kBuiltin};
  std::string       name;

  std::function<std::unique_ptr<webrtc::VideoEncoderFactory>()> create_encoder_factory;
  std::function<std::unique_ptr<webrtc::VideoDecoderFactory>()> create_decoder_factory;
  std::function<CodecBackendCapabilities()>                     get_capabilities;
  std::function<bool()>                                         encoder_disabled_at_runtime;
  std::function<bool()>                                         decoder_disabled_at_runtime;
};

/// 后端注册表：进程内单例，linkage 时由 backend_registry.cpp 的
/// EnsureBackendsRegistered() 完成 builtin + 编译期可达 platform backends 的注册。
/// 也允许应用层在运行期插入自定义 descriptor（用于测试 / 第三方 backend）。
class VideoBackendRegistry {
 public:
  static VideoBackendRegistry& Instance();

  /// 已存在同 backend 的会被覆盖；保证 Find(backend) 拿到的是最新一份。
  void Register(VideoBackendDescriptor desc);

  /// 返回 nullptr 表示尚未注册或已被运行期 disable。
  const VideoBackendDescriptor* Find(VideoCodecBackend backend) const;

  std::vector<CodecBackendCapabilities> SnapshotCapabilities() const;
};

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePreferredVideoEncoderFactory(
    const VideoBackendPreferences& prefs);

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePreferredVideoDecoderFactory(
    const VideoBackendPreferences& prefs);

std::vector<CodecBackendCapabilities> GetAvailableCodecBackendCapabilities();

}  // namespace rflow::rtc::hw

#endif  // __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__

