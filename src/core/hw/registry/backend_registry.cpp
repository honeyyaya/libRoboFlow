#include "hw/registry/backend_registry.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace rflow::hw {
namespace {

void RegisterPlatformBackends();

class BackendRegistryImpl {
 public:
  void Register(BackendModule module) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::find_if(modules_.begin(), modules_.end(),
                           [&](const BackendModule& m) { return m.id == module.id; });
    if (it != modules_.end()) {
      *it = std::move(module);
    } else {
      modules_.push_back(std::move(module));
    }
  }

  const BackendModule* Find(HwBackendId id) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& m : modules_) {
      if (m.id == id) {
        return &m;
      }
    }
    return nullptr;
  }

  std::vector<CodecCapabilities> ListCapabilities() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<CodecCapabilities> out;
    out.reserve(modules_.size());
    for (const auto& m : modules_) {
      if (m.get_capabilities) {
        out.push_back(m.get_capabilities());
      }
    }
    return out;
  }

 private:
  mutable std::mutex mu_;
  std::vector<BackendModule> modules_;
};

BackendRegistryImpl& RegistryImpl() {
  static BackendRegistryImpl impl;
  return impl;
}

BackendModule MakeBuiltinModule() {
  BackendModule m;
  m.id = HwBackendId::kBuiltin;
  m.name = "builtin";
  m.is_available = [] { return true; };
  m.get_capabilities = [] {
    CodecCapabilities c;
    c.backend_name = "builtin";
    c.encoder_codecs = {"h264", "vp8", "vp9"};
    c.decoder_codecs = {"h264", "vp8", "vp9"};
    c.pixel_formats = {"I420", "NV12"};
    c.supports_dynamic_bitrate = true;
    c.supports_forced_idr = true;
    c.zero_copy_level = ZeroCopyLevel::kNone;
    return c;
  };
  return m;
}

}  // namespace

BackendRegistry& BackendRegistry::Instance() {
  static BackendRegistry registry;
  return registry;
}

void BackendRegistry::Register(BackendModule module) {
  RegistryImpl().Register(std::move(module));
}

const BackendModule* BackendRegistry::Find(HwBackendId id) const {
  return RegistryImpl().Find(id);
}

std::vector<CodecCapabilities> BackendRegistry::ListCapabilities() const {
  return RegistryImpl().ListCapabilities();
}

void EnsureHwBackendsRegistered() {
  static std::once_flag once;
  std::call_once(once, [] {
    BackendRegistry::Instance().Register(MakeBuiltinModule());
    RegisterPlatformBackends();
  });
}

}  // namespace rflow::hw

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
#include "platform/rockchip/rockchip_backend.h"
#endif

namespace rflow::hw {
namespace {

void RegisterPlatformBackends() {
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  rflow::hw::rockchip::RegisterRockchipBackend(BackendRegistry::Instance());
#endif
}

}  // namespace
}  // namespace rflow::hw
