#ifndef __RFLOW_COMMON_STATE_SDK_STATE_H__
#define __RFLOW_COMMON_STATE_SDK_STATE_H__

#include "abi/object_layouts.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace rflow::common::base {

enum class SdkLifecycleState {
    kUninit,
    kInited,
    kConnecting,
    kConnected,
};

template <typename ConnectInfoT,
          typename ConnectCbT,
          typename StreamHandleT,
          typename StreamT>
struct SdkStateBase {
    std::mutex mu;
    SdkLifecycleState lifecycle = SdkLifecycleState::kUninit;
    librflow_global_config_s global_config{};
    ConnectInfoT connect_info{};
    ConnectCbT connect_cb{};
    bool has_connect_cb = false;
    std::unordered_map<StreamHandleT, std::shared_ptr<StreamT>> streams;
};

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_STATE_SDK_STATE_H__
