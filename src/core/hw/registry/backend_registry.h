#ifndef __RFLOW_CORE_HW_REGISTRY_BACKEND_REGISTRY_H__
#define __RFLOW_CORE_HW_REGISTRY_BACKEND_REGISTRY_H__

#include "hw/codec/capabilities.h"
#include "hw/codec/decoder.h"
#include "hw/codec/encoder.h"
#include "hw/registry/backend_id.h"

#include <functional>
#include <memory>
#include <vector>

namespace rflow::hw {

enum class CallbackDispatch : uint8_t {
  kCallerThread = 0,
  kHardwareThread = 1,
  kAppExecutor = 2,
};

struct BackendCreateOptions {
  CallbackDispatch dispatch{CallbackDispatch::kAppExecutor};
};

struct BackendModule {
  HwBackendId id{HwBackendId::kBuiltin};
  std::string name;

  std::function<std::unique_ptr<IVideoEncoder>(const EncoderConfig&, const BackendCreateOptions&)>
      create_encoder;
  std::function<std::unique_ptr<IVideoDecoder>(const DecoderConfig&, const BackendCreateOptions&)>
      create_decoder;
  std::function<CodecCapabilities()> get_capabilities;
  std::function<bool()> is_available;
};

class BackendRegistry {
 public:
  static BackendRegistry& Instance();

  void Register(BackendModule module);
  const BackendModule* Find(HwBackendId id) const;
  std::vector<CodecCapabilities> ListCapabilities() const;

 private:
  BackendRegistry() = default;
};

void EnsureHwBackendsRegistered();

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_REGISTRY_BACKEND_REGISTRY_H__
