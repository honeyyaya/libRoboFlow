#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <cstring>

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

rflow_err_t librflow_svc_license_info_get_expire_time(librflow_svc_license_info_t info,
                                                     uint64_t* out_expire_sec) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    if (!out_expire_sec) return RFLOW_ERR_PARAM;
    if (!info->loaded)   return RFLOW_ERR_NOT_FOUND;
    *out_expire_sec = info->expire_time_sec;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_license_info_get_vendor_id(librflow_svc_license_info_t info,
                                                   char* buf, uint32_t buf_len,
                                                   uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    return copy_out(info->vendor_id, buf, buf_len, out_needed);
}

rflow_err_t librflow_svc_license_info_get_product_key(librflow_svc_license_info_t info,
                                                     char* buf, uint32_t buf_len,
                                                     uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    return copy_out(info->product_key, buf, buf_len, out_needed);
}

void librflow_svc_license_info_destroy(librflow_svc_license_info_t info) {
    if (!info || info->magic != rflow::service::kMagicLicenseInfo) return;
    info->magic = 0;
    delete info;
}

}  // extern "C"
