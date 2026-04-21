#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <cstring>

namespace {

robrt_err_t copy_out(const std::string& s, char* buf, uint32_t buf_len) {
    if (!buf || buf_len == 0) return ROBRT_ERR_PARAM;
    const auto sz = static_cast<uint32_t>(s.size());
    const auto n  = (sz + 1 > buf_len) ? buf_len - 1 : sz;
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
    return (sz + 1 > buf_len) ? ROBRT_ERR_TRUNCATED : ROBRT_OK;
}

}  // namespace

extern "C" {

uint32_t librobrt_svc_license_info_get_expire_time(librobrt_svc_license_info_t info) {
    if (!info || info->magic != robrt::service::kMagicLicenseInfo) return 0;
    return info->expire_time;
}

robrt_err_t librobrt_svc_license_info_get_vendor_id(librobrt_svc_license_info_t info,
                                                     char* buf, uint32_t buf_len) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicLicenseInfo);
    return copy_out(info->vendor_id, buf, buf_len);
}

robrt_err_t librobrt_svc_license_info_get_product_key(librobrt_svc_license_info_t info,
                                                       char* buf, uint32_t buf_len) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicLicenseInfo);
    return copy_out(info->product_key, buf, buf_len);
}

void librobrt_svc_license_info_destroy(librobrt_svc_license_info_t info) {
    if (!info || info->magic != robrt::service::kMagicLicenseInfo) return;
    info->magic = 0;
    delete info;
}

}  // extern "C"
