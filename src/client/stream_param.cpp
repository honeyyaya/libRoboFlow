#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_stream_param_t librobrt_stream_param_create(void) {
    auto* p = new (std::nothrow) librobrt_stream_param_s();
    if (!p) return nullptr;
    p->magic               = robrt::client::kMagicStreamParam;
    p->preferred_codec     = ROBRT_CODEC_UNKNOWN;
    p->max_width           = 0;
    p->max_height          = 0;
    p->fps                 = 0;
    p->open_timeout_ms     = 0;
    p->has_preferred_codec = false;
    p->has_max_size        = false;
    p->has_fps             = false;
    p->has_open_timeout_ms = false;
    return p;
}

void librobrt_stream_param_destroy(librobrt_stream_param_t p) {
    if (!p || p->magic != robrt::client::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

robrt_err_t librobrt_stream_param_set_preferred_codec(librobrt_stream_param_t p, robrt_codec_t codec) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->preferred_codec     = codec;
    p->has_preferred_codec = true;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_preferred_max_size(librobrt_stream_param_t p,
                                                          uint32_t w, uint32_t h) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->max_width    = w;
    p->max_height   = h;
    p->has_max_size = true;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_preferred_fps(librobrt_stream_param_t p, uint32_t fps) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->fps     = fps;
    p->has_fps = true;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_open_timeout_ms(librobrt_stream_param_t p, uint32_t timeout_ms) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->open_timeout_ms     = timeout_ms;
    p->has_open_timeout_ms = true;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_get_preferred_codec(librobrt_stream_param_t p,
                                                      robrt_codec_t*          out_codec) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    if (!out_codec) return ROBRT_ERR_PARAM;
    if (!p->has_preferred_codec) return ROBRT_ERR_NOT_FOUND;
    *out_codec = p->preferred_codec;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_get_preferred_max_size(librobrt_stream_param_t p,
                                                         uint32_t*               out_max_width,
                                                         uint32_t*               out_max_height) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    if (!out_max_width || !out_max_height) return ROBRT_ERR_PARAM;
    if (!p->has_max_size) return ROBRT_ERR_NOT_FOUND;
    *out_max_width  = p->max_width;
    *out_max_height = p->max_height;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_get_preferred_fps(librobrt_stream_param_t p,
                                                    uint32_t*               out_fps) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    if (!out_fps) return ROBRT_ERR_PARAM;
    if (!p->has_fps) return ROBRT_ERR_NOT_FOUND;
    *out_fps = p->fps;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_get_open_timeout_ms(librobrt_stream_param_t p,
                                                      uint32_t*               out_timeout_ms) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    if (!out_timeout_ms) return ROBRT_ERR_PARAM;
    if (!p->has_open_timeout_ms) return ROBRT_ERR_NOT_FOUND;
    *out_timeout_ms = p->open_timeout_ms;
    return ROBRT_OK;
}

}  // extern "C"
