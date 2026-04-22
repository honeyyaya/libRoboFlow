#include "rflow/librflow_common.h"

#include "../internal/signal_config_impl.h"

#include <cstring>
#include <new>

extern "C" {

librflow_signal_config_t librflow_signal_config_create(void) {
    auto* c = new (std::nothrow) librflow_signal_config_s();
    if (!c) return nullptr;
    c->magic       = rflow::kMagicSignalConfig;
    c->mode        = RFLOW_SIGNAL_MODE_SERVER;
    c->direct_port = 0;
    return c;
}

void librflow_signal_config_destroy(librflow_signal_config_t cfg) {
    if (!cfg || cfg->magic != rflow::kMagicSignalConfig) return;
    cfg->magic = 0;
    delete cfg;
}

rflow_err_t librflow_signal_config_set_url(librflow_signal_config_t cfg, const char* url) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicSignalConfig);
    cfg->url = url ? url : "";
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_set_mode(librflow_signal_config_t cfg, rflow_signal_mode_t mode) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicSignalConfig);
    cfg->mode = mode;
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_set_direct_port(librflow_signal_config_t cfg, uint16_t port) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicSignalConfig);
    cfg->direct_port = port;
    return RFLOW_OK;
}

rflow_err_t librflow_signal_config_get_url(librflow_signal_config_t cfg,
                                           char*     buf,
                                           uint32_t  buf_len,
                                           uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(cfg, rflow::kMagicSignalConfig);

    const auto& s  = cfg->url;
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

}  // extern "C"
