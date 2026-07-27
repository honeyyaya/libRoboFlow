#ifndef __RFLOW_COMMON_CONFIG_ICE_PARAM_OPS_H__
#define __RFLOW_COMMON_CONFIG_ICE_PARAM_OPS_H__

#include "util/string_ops.h"

#include "rflow/librflow_common.h"

namespace rflow::common::base {

constexpr uint32_t kIceServerUrlMaxLen      = 511u;
constexpr uint32_t kIceTurnCredentialMaxLen = 127u;
constexpr uint32_t kStreamParamTokenMaxLen  = 127u;

inline rflow_err_t CopyRequiredTrimmedToken(const char* value, uint32_t max_len, std::string* out) {
    if (!out) {
        return RFLOW_ERR_PARAM;
    }
    std::string t = TrimCopyToken(value);
    if (t.empty() || t.size() > max_len) {
        return RFLOW_ERR_PARAM;
    }
    *out = std::move(t);
    return RFLOW_OK;
}

inline rflow_err_t CopyRequiredIceUrl(const char* url, std::string* out) {
    return CopyRequiredTrimmedToken(url, kIceServerUrlMaxLen, out);
}

inline rflow_err_t CopyOptionalIceCredential(const char* value, std::string* out) {
    if (!out) {
        return RFLOW_ERR_PARAM;
    }
    if (!value) {
        out->clear();
        return RFLOW_OK;
    }
    std::string t = TrimCopyToken(value);
    if (t.size() > kIceTurnCredentialMaxLen) {
        return RFLOW_ERR_PARAM;
    }
    *out = std::move(t);
    return RFLOW_OK;
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_CONFIG_ICE_PARAM_OPS_H__
