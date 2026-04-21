#include "robrt/librobrt_common.h"

#include "../internal/signal_config_impl.h"

#include <cstring>
#include <new>

extern "C" {

librobrt_signal_config_t librobrt_signal_config_create(void) {
    auto* c = new (std::nothrow) librobrt_signal_config_s();
    if (!c) return nullptr;
    c->magic       = robrt::kMagicSignalConfig;
    c->mode        = ROBRT_SIGNAL_MODE_SERVER;
    c->direct_port = 0;
    return c;
}

void librobrt_signal_config_destroy(librobrt_signal_config_t cfg) {
    if (!cfg || cfg->magic != robrt::kMagicSignalConfig) return;
    cfg->magic = 0;
    delete cfg;
}

robrt_err_t librobrt_signal_config_set_url(librobrt_signal_config_t cfg, const char* url) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicSignalConfig);
    cfg->url = url ? url : "";
    return ROBRT_OK;
}

robrt_err_t librobrt_signal_config_set_mode(librobrt_signal_config_t cfg, robrt_signal_mode_t mode) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicSignalConfig);
    cfg->mode = mode;
    return ROBRT_OK;
}

robrt_err_t librobrt_signal_config_set_direct_port(librobrt_signal_config_t cfg, uint16_t port) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicSignalConfig);
    cfg->direct_port = port;
    return ROBRT_OK;
}

robrt_err_t librobrt_signal_config_get_url(librobrt_signal_config_t cfg,
                                           char*    buf,
                                           uint32_t buf_len) {
    ROBRT_CHECK_HANDLE(cfg, robrt::kMagicSignalConfig);
    if (!buf || buf_len == 0) return ROBRT_ERR_PARAM;

    const auto& s  = cfg->url;
    const auto  sz = static_cast<uint32_t>(s.size());
    const auto  n  = (sz + 1 > buf_len) ? buf_len - 1 : sz;
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
    return (sz + 1 > buf_len) ? ROBRT_ERR_TRUNCATED : ROBRT_OK;
}

}  // extern "C"
