#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <cstring>
#include <new>
#include <string>

namespace {

rflow_err_t copy_out(const std::string& s, char* buf, uint32_t buf_len, uint32_t* out_needed) {
    if (s.empty()) return RFLOW_ERR_NOT_FOUND;

    const auto sz     = static_cast<uint32_t>(s.size());
    const auto needed = sz + 1u;
    if (out_needed) *out_needed = needed;

    if (!buf || buf_len == 0) return RFLOW_ERR_TRUNCATED;
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

}  // namespace

extern "C" {

librflow_svc_connect_info_t librflow_svc_connect_info_create(void) {
    auto* p = new (std::nothrow) librflow_svc_connect_info_s();
    if (!p) return nullptr;
    p->magic = rflow::service::kMagicConnectInfo;
    return p;
}

void librflow_svc_connect_info_destroy(librflow_svc_connect_info_t info) {
    if (!info || info->magic != rflow::service::kMagicConnectInfo) return;
    info->magic = 0;
    delete info;
}

#define SVC_SI_SET(field)                                                                     \
    rflow_err_t librflow_svc_connect_info_set_##field(librflow_svc_connect_info_t info,       \
                                                      const char* v) {                         \
        RFLOW_CHECK_HANDLE(info, rflow::service::kMagicConnectInfo);                          \
        info->field = v ? v : "";                                                             \
        return RFLOW_OK;                                                                       \
    }

SVC_SI_SET(device_id)
SVC_SI_SET(device_secret)
SVC_SI_SET(product_key)

#undef SVC_SI_SET

rflow_err_t librflow_svc_connect_info_set_vendor_id(librflow_svc_connect_info_t info,
                                                     const char* v) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicConnectInfo);
    info->vendor_id = v ? v : "";
    return RFLOW_OK;
}

#define SVC_CI_GET(field)                                                                     \
    rflow_err_t librflow_svc_connect_info_get_##field(librflow_svc_connect_info_t info,       \
                                                       char* buf, uint32_t buf_len,            \
                                                       uint32_t* out_needed) {                 \
        RFLOW_CHECK_HANDLE(info, rflow::service::kMagicConnectInfo);                          \
        return copy_out(info->field, buf, buf_len, out_needed);                                \
    }

SVC_CI_GET(device_id)
SVC_CI_GET(device_secret)
SVC_CI_GET(product_key)
SVC_CI_GET(vendor_id)

#undef SVC_CI_GET

}  // extern "C"
