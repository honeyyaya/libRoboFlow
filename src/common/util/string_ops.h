#ifndef __RFLOW_COMMON_UTIL_STRING_OPS_H__
#define __RFLOW_COMMON_UTIL_STRING_OPS_H__

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "rflow/librflow_common.h"

namespace rflow::common::base {

inline std::string TrimCopyToken(const char* s) {
    if (!s) {
        return {};
    }
    std::string t(s);
    const auto begin = std::find_if_not(t.begin(), t.end(),
                                        [](unsigned char c) { return std::isspace(c) != 0; });
    const auto rend =
        std::find_if_not(t.rbegin(), t.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= rend) {
        return {};
    }
    return std::string(begin, rend);
}

inline rflow_err_t CopyOutString(const std::string& s,
                                 char* buf,
                                 uint32_t buf_len,
                                 uint32_t* out_needed) {
    if (s.empty()) {
        return RFLOW_ERR_NOT_FOUND;
    }
    const auto sz = static_cast<uint32_t>(s.size());
    const auto needed = sz + 1u;
    if (out_needed) {
        *out_needed = needed;
    }
    if (!buf || buf_len == 0) {
        return RFLOW_ERR_TRUNCATED;
    }
    if (buf_len < needed) {
        const auto n = buf_len - 1u;
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
        return RFLOW_ERR_TRUNCATED;
    }
    std::memcpy(buf, s.data(), sz);
    buf[sz] = '\0';
    return RFLOW_OK;
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_UTIL_STRING_OPS_H__
