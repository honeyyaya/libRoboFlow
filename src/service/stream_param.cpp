#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_stream_param_t librobrt_svc_stream_param_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_stream_param_s();
    if (!p) return nullptr;
    p->magic   = robrt::service::kMagicStreamParam;
    p->rc_mode = ROBRT_RC_VBR;
    return p;
}

void librobrt_svc_stream_param_destroy(librobrt_svc_stream_param_t p) {
    if (!p || p->magic != robrt::service::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

#define SVC_SP_CHECK(p) ROBRT_CHECK_HANDLE(p, robrt::service::kMagicStreamParam)

/* ---------------- setters ---------------- */

robrt_err_t librobrt_svc_stream_param_set_in_codec(librobrt_svc_stream_param_t p, robrt_codec_t c) {
    SVC_SP_CHECK(p);
    p->in_codec     = c;
    p->has_in_codec = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_out_codec(librobrt_svc_stream_param_t p, robrt_codec_t c) {
    SVC_SP_CHECK(p);
    p->out_codec     = c;
    p->has_out_codec = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_src_size(librobrt_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p);
    p->src_w        = w;
    p->src_h        = h;
    p->has_src_size = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_out_size(librobrt_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p);
    p->out_w        = w;
    p->out_h        = h;
    p->has_out_size = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_fps(librobrt_svc_stream_param_t p, uint32_t f) {
    SVC_SP_CHECK(p);
    p->fps     = f;
    p->has_fps = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_gop(librobrt_svc_stream_param_t p, uint32_t g) {
    SVC_SP_CHECK(p);
    p->gop     = g;
    p->has_gop = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_rc_mode(librobrt_svc_stream_param_t p, robrt_rc_mode_t r) {
    SVC_SP_CHECK(p);
    p->rc_mode     = r;
    p->has_rc_mode = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_qp(librobrt_svc_stream_param_t p, uint32_t qp) {
    SVC_SP_CHECK(p);
    p->qp     = qp;
    p->has_qp = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_bitrate(librobrt_svc_stream_param_t p, uint32_t br, uint32_t max_br) {
    SVC_SP_CHECK(p);
    p->bitrate_kbps     = br;
    p->max_bitrate_kbps = max_br;
    p->has_bitrate      = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_dynamic_bitrate(librobrt_svc_stream_param_t p, bool e, uint32_t lo, uint32_t hi) {
    SVC_SP_CHECK(p);
    p->dynamic_bitrate     = e;
    p->lowest_kbps         = lo;
    p->highest_kbps        = hi;
    p->has_dynamic_bitrate = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_enable_transcode(librobrt_svc_stream_param_t p, bool e) {
    SVC_SP_CHECK(p);
    p->enable_transcode     = e;
    p->has_enable_transcode = true;
    return ROBRT_OK;
}

/* ---------------- getters ---------------- */

robrt_err_t librobrt_svc_stream_param_get_in_codec(librobrt_svc_stream_param_t p, robrt_codec_t* out_codec) {
    SVC_SP_CHECK(p);
    if (!out_codec)       return ROBRT_ERR_PARAM;
    if (!p->has_in_codec) return ROBRT_ERR_NOT_FOUND;
    *out_codec = p->in_codec;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_out_codec(librobrt_svc_stream_param_t p, robrt_codec_t* out_codec) {
    SVC_SP_CHECK(p);
    if (!out_codec)        return ROBRT_ERR_PARAM;
    if (!p->has_out_codec) return ROBRT_ERR_NOT_FOUND;
    *out_codec = p->out_codec;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_src_size(librobrt_svc_stream_param_t p,
                                                    uint32_t* out_width, uint32_t* out_height) {
    SVC_SP_CHECK(p);
    if (!out_width || !out_height) return ROBRT_ERR_PARAM;
    if (!p->has_src_size)          return ROBRT_ERR_NOT_FOUND;
    *out_width  = p->src_w;
    *out_height = p->src_h;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_out_size(librobrt_svc_stream_param_t p,
                                                    uint32_t* out_width, uint32_t* out_height) {
    SVC_SP_CHECK(p);
    if (!out_width || !out_height) return ROBRT_ERR_PARAM;
    if (!p->has_out_size)          return ROBRT_ERR_NOT_FOUND;
    *out_width  = p->out_w;
    *out_height = p->out_h;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_fps(librobrt_svc_stream_param_t p, uint32_t* out_fps) {
    SVC_SP_CHECK(p);
    if (!out_fps)   return ROBRT_ERR_PARAM;
    if (!p->has_fps) return ROBRT_ERR_NOT_FOUND;
    *out_fps = p->fps;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_gop(librobrt_svc_stream_param_t p, uint32_t* out_gop) {
    SVC_SP_CHECK(p);
    if (!out_gop)   return ROBRT_ERR_PARAM;
    if (!p->has_gop) return ROBRT_ERR_NOT_FOUND;
    *out_gop = p->gop;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_rc_mode(librobrt_svc_stream_param_t p, robrt_rc_mode_t* out_rc) {
    SVC_SP_CHECK(p);
    if (!out_rc)        return ROBRT_ERR_PARAM;
    if (!p->has_rc_mode) return ROBRT_ERR_NOT_FOUND;
    *out_rc = p->rc_mode;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_qp(librobrt_svc_stream_param_t p, uint32_t* out_qp) {
    SVC_SP_CHECK(p);
    if (!out_qp)   return ROBRT_ERR_PARAM;
    if (!p->has_qp) return ROBRT_ERR_NOT_FOUND;
    *out_qp = p->qp;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_bitrate(librobrt_svc_stream_param_t p,
                                                   uint32_t* out_bitrate_kbps,
                                                   uint32_t* out_max_bitrate_kbps) {
    SVC_SP_CHECK(p);
    if (!out_bitrate_kbps || !out_max_bitrate_kbps) return ROBRT_ERR_PARAM;
    if (!p->has_bitrate)                             return ROBRT_ERR_NOT_FOUND;
    *out_bitrate_kbps     = p->bitrate_kbps;
    *out_max_bitrate_kbps = p->max_bitrate_kbps;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_dynamic_bitrate(librobrt_svc_stream_param_t p,
                                                           bool* out_enable,
                                                           uint32_t* out_lowest_kbps,
                                                           uint32_t* out_highest_kbps) {
    SVC_SP_CHECK(p);
    if (!out_enable || !out_lowest_kbps || !out_highest_kbps) return ROBRT_ERR_PARAM;
    if (!p->has_dynamic_bitrate)                               return ROBRT_ERR_NOT_FOUND;
    *out_enable       = p->dynamic_bitrate;
    *out_lowest_kbps  = p->lowest_kbps;
    *out_highest_kbps = p->highest_kbps;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_get_enable_transcode(librobrt_svc_stream_param_t p, bool* out_enable) {
    SVC_SP_CHECK(p);
    if (!out_enable)              return ROBRT_ERR_PARAM;
    if (!p->has_enable_transcode) return ROBRT_ERR_NOT_FOUND;
    *out_enable = p->enable_transcode;
    return ROBRT_OK;
}

#undef SVC_SP_CHECK

}  // extern "C"
