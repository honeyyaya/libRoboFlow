#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_stream_param_t librobrt_stream_param_create(void) {
    auto* p = new (std::nothrow) librobrt_stream_param_s();
    if (!p) return nullptr;
    p->magic           = robrt::client::kMagicStreamParam;
    p->preferred_codec = ROBRT_CODEC_UNKNOWN;
    p->max_width       = 0;
    p->max_height      = 0;
    p->fps             = 0;
    p->enable_audio    = false;
    return p;
}

void librobrt_stream_param_destroy(librobrt_stream_param_t p) {
    if (!p || p->magic != robrt::client::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

robrt_err_t librobrt_stream_param_set_preferred_codec(librobrt_stream_param_t p, robrt_codec_t codec) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->preferred_codec = codec;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_preferred_max_size(librobrt_stream_param_t p,
                                                          uint32_t w, uint32_t h) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->max_width  = w;
    p->max_height = h;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_preferred_fps(librobrt_stream_param_t p, uint32_t fps) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->fps = fps;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_param_set_enable_audio(librobrt_stream_param_t p, bool enable) {
    ROBRT_CHECK_HANDLE(p, robrt::client::kMagicStreamParam);
    p->enable_audio = enable;
    return ROBRT_OK;
}

}  // extern "C"
