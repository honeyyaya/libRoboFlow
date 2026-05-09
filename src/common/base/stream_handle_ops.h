#ifndef __RFLOW_COMMON_BASE_STREAM_HANDLE_OPS_H__
#define __RFLOW_COMMON_BASE_STREAM_HANDLE_OPS_H__

#include "rflow/librflow_common.h"

#include <mutex>

// 通用的 stream-handle 模板 helper：
//   - LookupSharedFromMap          : unordered_map<HandleT, shared_ptr<StreamT>> 上的统一查找
//   - EmitStreamStateChange        : stream.state.store(...) + cb.on_state(...) 派发
//   - BroadcastStreamStateToAllCb  : 在 disconnect/shutdown 时对所有 stream 广播状态
//   - LookupStreamUnlocked         : 加锁查 state.streams 并把 shared_ptr 取到锁外
//   - RemoveStreamUnlocked         : 加锁查 + erase，把 shared_ptr 取到锁外
//
// StreamT 形态约束：
//   - 提供 .state.store(rflow_stream_state_t)（atomic-like）
//   - 提供 .cb.on_state（C 回调指针，签名为 (HandleT, rflow_stream_state_t, rflow_err_t, void*)）
//   - 提供 .cb.userdata
//
// StateT 形态约束（仅 LookupStreamUnlocked / RemoveStreamUnlocked）：
//   - .mu     : std::mutex
//   - .streams: unordered_map<HandleT, std::shared_ptr<StreamT>>
namespace rflow::common::base {

template <typename MapT, typename KeyT>
inline auto LookupSharedFromMap(MapT& map, const KeyT& key)
    -> typename MapT::mapped_type {
    auto it = map.find(key);
    if (it == map.end()) return {};
    return it->second;
}

template <typename StreamT, typename HandleT>
inline void EmitStreamStateChange(StreamT& stream,
                                  HandleT handle,
                                  rflow_stream_state_t new_state,
                                  rflow_err_t reason) {
    stream.state.store(new_state);
    if (stream.cb.on_state) {
        stream.cb.on_state(handle, new_state, reason, stream.cb.userdata);
    }
}

template <typename MapT>
inline void BroadcastStreamStateToAllCb(MapT& streams,
                                        rflow_stream_state_t new_state,
                                        rflow_err_t reason) {
    for (auto& kv : streams) {
        const auto& handle = kv.first;
        const auto& sh = kv.second;
        if (sh && sh->cb.on_state) {
            sh->cb.on_state(handle, new_state, reason, sh->cb.userdata);
        }
    }
}

template <typename StateT, typename HandleT>
inline auto LookupStreamUnlocked(StateT& state, HandleT handle)
    -> typename decltype(state.streams)::mapped_type {
    std::lock_guard<std::mutex> lk(state.mu);
    return LookupSharedFromMap(state.streams, handle);
}

template <typename StateT, typename HandleT>
inline auto RemoveStreamUnlocked(StateT& state, HandleT handle)
    -> typename decltype(state.streams)::mapped_type {
    std::lock_guard<std::mutex> lk(state.mu);
    auto sh = LookupSharedFromMap(state.streams, handle);
    if (sh) {
        state.streams.erase(handle);
    }
    return sh;
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_BASE_STREAM_HANDLE_OPS_H__
