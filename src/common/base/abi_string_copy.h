#ifndef __RFLOW_COMMON_BASE_ABI_STRING_COPY_H__
#define __RFLOW_COMMON_BASE_ABI_STRING_COPY_H__

// 通用的 ABI 出参字符串拷贝语义。
// 调用方约定（与 librflow_*_get_xxx 系列保持一致）：
//   - 返回 RFLOW_ERR_NOT_FOUND : 源字符串为空，没有可拷贝内容；不写 out_needed。
//   - out_needed != nullptr  : 写入"完整拷贝（含终止符）所需字节数 = s.size() + 1"。
//   - buf == nullptr || buf_len == 0 : 仅查询长度场景，返回 RFLOW_ERR_TRUNCATED。
//   - buf_len < s.size() + 1 : 部分拷贝并写入终止符，返回 RFLOW_ERR_TRUNCATED。
//   - 否则                    : 完整拷贝并写入终止符，返回 RFLOW_OK。
//
// 该 helper 同时被 service / client 侧 api 实现以及 common/abi 内部 *_get_xxx
// 接口复用，避免每处都重复写"先算长度、再判断是否截断"的样板代码。

#include <cstring>
#include <string>

#include "rflow/librflow_common.h"

namespace rflow::common::base {

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

#endif  // __RFLOW_COMMON_BASE_ABI_STRING_COPY_H__
