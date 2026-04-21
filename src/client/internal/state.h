#ifndef __ROBRT_CLIENT_STATE_H__
#define __ROBRT_CLIENT_STATE_H__

#include "common/internal/global_config_impl.h"
#include "handles.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace robrt::client {

enum class LifecycleState {
    kUninit,
    kInited,
    kConnecting,
    kConnected,
};

struct State {
    std::mutex                      mu;
    LifecycleState                  lifecycle = LifecycleState::kUninit;
    librobrt_global_config_s        global_config{};  // shallow copy
    librobrt_connect_info_s         connect_info{};
    librobrt_connect_cb_s           connect_cb{};
    bool                            has_connect_cb = false;

    // stream_handle (raw ptr) -> shared_ptr for lifecycle control
    std::unordered_map<librobrt_stream_handle_t,
                       std::shared_ptr<librobrt_stream_s>> streams;
};

State& state();

}  // namespace robrt::client

#endif  // __ROBRT_CLIENT_STATE_H__
