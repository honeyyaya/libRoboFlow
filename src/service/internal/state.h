#ifndef __ROBRT_SERVICE_STATE_H__
#define __ROBRT_SERVICE_STATE_H__

#include "common/internal/global_config_impl.h"
#include "handles.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace robrt::service {

enum class LifecycleState {
    kUninit,
    kInited,
    kConnecting,
    kConnected,
};

struct State {
    std::mutex                                 mu;
    LifecycleState                             lifecycle = LifecycleState::kUninit;
    librobrt_global_config_s                   global_config{};
    librobrt_svc_connect_info_s                connect_info{};
    librobrt_svc_connect_cb_s                  connect_cb{};
    bool                                       has_connect_cb = false;

    std::unordered_map<librobrt_svc_stream_handle_t,
                       std::shared_ptr<librobrt_svc_stream_s>> streams;
};

State& state();

}  // namespace robrt::service

#endif  // __ROBRT_SERVICE_STATE_H__
