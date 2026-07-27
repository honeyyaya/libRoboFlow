#ifndef __RFLOW_COMMON_UTIL_BUFFER_SHRINK_UTILS_H__
#define __RFLOW_COMMON_UTIL_BUFFER_SHRINK_UTILS_H__

#include <cstring>
#include <optional>
#include <vector>

namespace rflow::common::base {

template <typename T = uint8_t>
inline void MaybeShrinkVectorIfIdle(std::vector<T>* buf,
                                    unsigned tick,
                                    unsigned period,
                                    size_t shrink_if_capacity_above,
                                    size_t max_live_size,
                                    std::optional<size_t> exact_live_size = std::nullopt) {
    if (!buf || buf->capacity() <= shrink_if_capacity_above) {
        return;
    }
    if (tick == 0 || (tick % period) != 0u) {
        return;
    }
    if (exact_live_size.has_value()) {
        if (buf->size() != *exact_live_size) {
            return;
        }
    } else if (buf->size() > max_live_size) {
        return;
    }
    std::vector<T> compact;
    if (!buf->empty()) {
        compact.assign(buf->begin(), buf->end());
    }
    buf->swap(compact);
}

template <typename T = uint8_t>
inline void MaybeShrinkVectorToLiveBytes(std::vector<T>* buf,
                                         unsigned tick,
                                         unsigned period,
                                         size_t shrink_if_capacity_above,
                                         size_t max_live_bytes,
                                         size_t live_bytes) {
    if (!buf) {
        return;
    }
    if ((tick % period) != 0u) {
        return;
    }
    if (buf->capacity() <= shrink_if_capacity_above || live_bytes > max_live_bytes) {
        return;
    }
    std::vector<T> compact(live_bytes);
    if (live_bytes > 0) {
        std::memcpy(compact.data(), buf->data(), live_bytes * sizeof(T));
    }
    buf->swap(compact);
}

}  // namespace rflow::common::base

namespace rflow::common::util {
using rflow::common::base::MaybeShrinkVectorIfIdle;
using rflow::common::base::MaybeShrinkVectorToLiveBytes;
}  // namespace rflow::common::util

#endif  // __RFLOW_COMMON_UTIL_BUFFER_SHRINK_UTILS_H__
