#ifndef __RFLOW_SERVICE_STATE_H__
#define __RFLOW_SERVICE_STATE_H__

#include "common/internal/global_config_impl.h"
#include "handles.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace rflow::service {

enum class LifecycleState {
    kUninit,
    kInited,
    kConnecting,
    kConnected,
};

struct State {
    std::mutex                                 mu;
    LifecycleState                             lifecycle = LifecycleState::kUninit;
    librflow_global_config_s                   global_config{};
    librflow_svc_connect_info_s                connect_info{};
    librflow_svc_connect_cb_s                  connect_cb{};
    bool                                       has_connect_cb = false;

    std::unordered_map<librflow_svc_stream_handle_t,
                       std::shared_ptr<librflow_svc_stream_s>> streams;
};

State& state();

}  // namespace rflow::service

#endif  // __RFLOW_SERVICE_STATE_H__
