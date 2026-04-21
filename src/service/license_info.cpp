#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <cstring>

namespace {

robrt_err_t copy_out(const std::string& s, char* buf, uint32_t buf_len, uint32_t* out_needed) {
    if (s.empty()) return ROBRT_ERR_NOT_FOUND;

    const auto sz     = static_cast<uint32_t>(s.size());
    const auto needed = sz + 1u;
    if (out_needed) *out_needed = needed;

    if (!buf || buf_len == 0) return ROBRT_ERR_TRUNCATED;
    if (buf_len < needed) {
        const auto n = buf_len - 1u;
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
        return ROBRT_ERR_TRUNCATED;
    }
    std::memcpy(buf, s.data(), sz);
    buf[sz] = '\0';
    return ROBRT_OK;
}

}  // namespace

extern "C" {

robrt_err_t librobrt_svc_license_info_get_expire_time(librobrt_svc_license_info_t info,
                                                     uint64_t* out_expire_sec) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicLicenseInfo);
    if (!out_expire_sec) return ROBRT_ERR_PARAM;
    if (!info->loaded)   return ROBRT_ERR_NOT_FOUND;
    *out_expire_sec = info->expire_time_sec;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_license_info_get_vendor_id(librobrt_svc_license_info_t info,
                                                   char* buf, uint32_t buf_len,
                                                   uint32_t* out_needed) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicLicenseInfo);
    return copy_out(info->vendor_id, buf, buf_len, out_needed);
}

robrt_err_t librobrt_svc_license_info_get_product_key(librobrt_svc_license_info_t info,
                                                     char* buf, uint32_t buf_len,
                                                     uint32_t* out_needed) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicLicenseInfo);
    return copy_out(info->product_key, buf, buf_len, out_needed);
}

void librobrt_svc_license_info_destroy(librobrt_svc_license_info_t info) {
    if (!info || info->magic != robrt::service::kMagicLicenseInfo) return;
    info->magic = 0;
    delete info;
}

}  // extern "C"
