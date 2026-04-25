#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_stream_param_t librflow_stream_param_create(void) {
    auto* p = new (std::nothrow) librflow_stream_param_s();
    if (!p) return nullptr;
    p->magic               = rflow::client::kMagicStreamParam;
    p->preferred_codec     = RFLOW_CODEC_UNKNOWN;
    p->video_output_mode   = RFLOW_VIDEO_OUTPUT_MODE_CPU_PLANAR;
    p->max_width           = 0;
    p->max_height          = 0;
    p->fps                 = 0;
    p->open_timeout_ms     = 0;
    p->has_preferred_codec = false;
    p->has_video_output_mode = false;
    p->has_max_size        = false;
    p->has_fps             = false;
    p->has_open_timeout_ms = false;
    return p;
}

void librflow_stream_param_destroy(librflow_stream_param_t p) {
    if (!p || p->magic != rflow::client::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

rflow_err_t librflow_stream_param_set_preferred_codec(librflow_stream_param_t p, rflow_codec_t codec) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    p->preferred_codec     = codec;
    p->has_preferred_codec = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_set_preferred_max_size(librflow_stream_param_t p,
                                                          uint32_t w, uint32_t h) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    p->max_width    = w;
    p->max_height   = h;
    p->has_max_size = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_set_preferred_fps(librflow_stream_param_t p, uint32_t fps) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    p->fps     = fps;
    p->has_fps = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_set_video_output_mode(librflow_stream_param_t p,
                                                        rflow_video_output_mode_t mode) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    p->video_output_mode = mode;
    p->has_video_output_mode = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_set_open_timeout_ms(librflow_stream_param_t p, uint32_t timeout_ms) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    p->open_timeout_ms     = timeout_ms;
    p->has_open_timeout_ms = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_get_preferred_codec(librflow_stream_param_t p,
                                                      rflow_codec_t*          out_codec) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!out_codec) return RFLOW_ERR_PARAM;
    if (!p->has_preferred_codec) return RFLOW_ERR_NOT_FOUND;
    *out_codec = p->preferred_codec;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_get_preferred_max_size(librflow_stream_param_t p,
                                                         uint32_t*               out_max_width,
                                                         uint32_t*               out_max_height) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!out_max_width || !out_max_height) return RFLOW_ERR_PARAM;
    if (!p->has_max_size) return RFLOW_ERR_NOT_FOUND;
    *out_max_width  = p->max_width;
    *out_max_height = p->max_height;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_get_preferred_fps(librflow_stream_param_t p,
                                                    uint32_t*               out_fps) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!out_fps) return RFLOW_ERR_PARAM;
    if (!p->has_fps) return RFLOW_ERR_NOT_FOUND;
    *out_fps = p->fps;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_get_open_timeout_ms(librflow_stream_param_t p,
                                                      uint32_t*               out_timeout_ms) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!out_timeout_ms) return RFLOW_ERR_PARAM;
    if (!p->has_open_timeout_ms) return RFLOW_ERR_NOT_FOUND;
    *out_timeout_ms = p->open_timeout_ms;
    return RFLOW_OK;
}

}  // extern "C"
