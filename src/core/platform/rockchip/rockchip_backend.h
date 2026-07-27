#ifndef __RFLOW_CORE_PLATFORM_ROCKCHIP_BACKEND_H__
#define __RFLOW_CORE_PLATFORM_ROCKCHIP_BACKEND_H__

#include "hw/registry/backend_registry.h"

namespace rflow::hw::rockchip {

void RegisterRockchipBackend(BackendRegistry& registry);

}  // namespace rflow::hw::rockchip

#endif  // __RFLOW_CORE_PLATFORM_ROCKCHIP_BACKEND_H__
