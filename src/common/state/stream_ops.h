#ifndef __RFLOW_COMMON_STATE_STREAM_OPS_H__
#define __RFLOW_COMMON_STATE_STREAM_OPS_H__

#include "rflow/librflow_common.h"

#include <mutex>

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

template <typename StateT, typename HandleT>
inline auto FindStreamByHandleLocked(StateT& state, HandleT handle)
    -> typename decltype(state.streams)::mapped_type {
    return LookupSharedFromMap(state.streams, handle);
}

template <typename StreamT, typename HandleT>
inline void MarkStreamClosed(StreamT& stream, HandleT handle) {
    EmitStreamStateChange(stream, handle, RFLOW_STREAM_CLOSED, RFLOW_OK);
    stream.magic = 0;
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_STATE_STREAM_OPS_H__
