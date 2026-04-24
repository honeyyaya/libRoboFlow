#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <cstring>
#include <new>

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

librflow_svc_stream_param_t librflow_svc_stream_param_create(void) {
    auto* p = new (std::nothrow) librflow_svc_stream_param_s();
    if (!p) return nullptr;
    p->magic   = rflow::service::kMagicStreamParam;
    p->rc_mode = RFLOW_RC_VBR;
    return p;
}

void librflow_svc_stream_param_destroy(librflow_svc_stream_param_t p) {
    if (!p || p->magic != rflow::service::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

#define SVC_SP_CHECK(p) RFLOW_CHECK_HANDLE(p, rflow::service::kMagicStreamParam)

/* ---------------- setters ---------------- */

rflow_err_t librflow_svc_stream_param_set_in_codec(librflow_svc_stream_param_t p, rflow_codec_t c) {
    SVC_SP_CHECK(p);
    p->in_codec     = c;
    p->has_in_codec = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_out_codec(librflow_svc_stream_param_t p, rflow_codec_t c) {
    SVC_SP_CHECK(p);
    p->out_codec     = c;
    p->has_out_codec = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_src_size(librflow_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p);
    p->src_w        = w;
    p->src_h        = h;
    p->has_src_size = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_out_size(librflow_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p);
    p->out_w        = w;
    p->out_h        = h;
    p->has_out_size = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_fps(librflow_svc_stream_param_t p, uint32_t f) {
    SVC_SP_CHECK(p);
    p->fps     = f;
    p->has_fps = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_gop(librflow_svc_stream_param_t p, uint32_t g) {
    SVC_SP_CHECK(p);
    p->gop     = g;
    p->has_gop = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_rc_mode(librflow_svc_stream_param_t p, rflow_rc_mode_t r) {
    SVC_SP_CHECK(p);
    p->rc_mode     = r;
    p->has_rc_mode = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_qp(librflow_svc_stream_param_t p, uint32_t qp) {
    SVC_SP_CHECK(p);
    p->qp     = qp;
    p->has_qp = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_bitrate(librflow_svc_stream_param_t p, uint32_t br, uint32_t max_br) {
    SVC_SP_CHECK(p);
    p->bitrate_kbps     = br;
    p->max_bitrate_kbps = max_br;
    p->has_bitrate      = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_dynamic_bitrate(librflow_svc_stream_param_t p, bool e, uint32_t lo, uint32_t hi) {
    SVC_SP_CHECK(p);
    p->dynamic_bitrate     = e;
    p->lowest_kbps         = lo;
    p->highest_kbps        = hi;
    p->has_dynamic_bitrate = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_enable_transcode(librflow_svc_stream_param_t p, bool e) {
    SVC_SP_CHECK(p);
    p->enable_transcode     = e;
    p->has_enable_transcode = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_video_device_path(librflow_svc_stream_param_t p, const char* v) {
    SVC_SP_CHECK(p);
    p->video_device_path     = v ? v : "";
    p->has_video_device_path = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_set_video_device_index(librflow_svc_stream_param_t p, uint32_t index) {
    SVC_SP_CHECK(p);
    p->video_device_index     = index;
    p->has_video_device_index = true;
    return RFLOW_OK;
}

/* ---------------- getters ---------------- */

rflow_err_t librflow_svc_stream_param_get_in_codec(librflow_svc_stream_param_t p, rflow_codec_t* out_codec) {
    SVC_SP_CHECK(p);
    if (!out_codec)       return RFLOW_ERR_PARAM;
    if (!p->has_in_codec) return RFLOW_ERR_NOT_FOUND;
    *out_codec = p->in_codec;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_out_codec(librflow_svc_stream_param_t p, rflow_codec_t* out_codec) {
    SVC_SP_CHECK(p);
    if (!out_codec)        return RFLOW_ERR_PARAM;
    if (!p->has_out_codec) return RFLOW_ERR_NOT_FOUND;
    *out_codec = p->out_codec;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_src_size(librflow_svc_stream_param_t p,
                                                    uint32_t* out_width, uint32_t* out_height) {
    SVC_SP_CHECK(p);
    if (!out_width || !out_height) return RFLOW_ERR_PARAM;
    if (!p->has_src_size)          return RFLOW_ERR_NOT_FOUND;
    *out_width  = p->src_w;
    *out_height = p->src_h;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_out_size(librflow_svc_stream_param_t p,
                                                    uint32_t* out_width, uint32_t* out_height) {
    SVC_SP_CHECK(p);
    if (!out_width || !out_height) return RFLOW_ERR_PARAM;
    if (!p->has_out_size)          return RFLOW_ERR_NOT_FOUND;
    *out_width  = p->out_w;
    *out_height = p->out_h;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_fps(librflow_svc_stream_param_t p, uint32_t* out_fps) {
    SVC_SP_CHECK(p);
    if (!out_fps)   return RFLOW_ERR_PARAM;
    if (!p->has_fps) return RFLOW_ERR_NOT_FOUND;
    *out_fps = p->fps;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_gop(librflow_svc_stream_param_t p, uint32_t* out_gop) {
    SVC_SP_CHECK(p);
    if (!out_gop)   return RFLOW_ERR_PARAM;
    if (!p->has_gop) return RFLOW_ERR_NOT_FOUND;
    *out_gop = p->gop;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_rc_mode(librflow_svc_stream_param_t p, rflow_rc_mode_t* out_rc) {
    SVC_SP_CHECK(p);
    if (!out_rc)        return RFLOW_ERR_PARAM;
    if (!p->has_rc_mode) return RFLOW_ERR_NOT_FOUND;
    *out_rc = p->rc_mode;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_qp(librflow_svc_stream_param_t p, uint32_t* out_qp) {
    SVC_SP_CHECK(p);
    if (!out_qp)   return RFLOW_ERR_PARAM;
    if (!p->has_qp) return RFLOW_ERR_NOT_FOUND;
    *out_qp = p->qp;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_bitrate(librflow_svc_stream_param_t p,
                                                   uint32_t* out_bitrate_kbps,
                                                   uint32_t* out_max_bitrate_kbps) {
    SVC_SP_CHECK(p);
    if (!out_bitrate_kbps || !out_max_bitrate_kbps) return RFLOW_ERR_PARAM;
    if (!p->has_bitrate)                             return RFLOW_ERR_NOT_FOUND;
    *out_bitrate_kbps     = p->bitrate_kbps;
    *out_max_bitrate_kbps = p->max_bitrate_kbps;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_dynamic_bitrate(librflow_svc_stream_param_t p,
                                                           bool* out_enable,
                                                           uint32_t* out_lowest_kbps,
                                                           uint32_t* out_highest_kbps) {
    SVC_SP_CHECK(p);
    if (!out_enable || !out_lowest_kbps || !out_highest_kbps) return RFLOW_ERR_PARAM;
    if (!p->has_dynamic_bitrate)                               return RFLOW_ERR_NOT_FOUND;
    *out_enable       = p->dynamic_bitrate;
    *out_lowest_kbps  = p->lowest_kbps;
    *out_highest_kbps = p->highest_kbps;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_enable_transcode(librflow_svc_stream_param_t p, bool* out_enable) {
    SVC_SP_CHECK(p);
    if (!out_enable)              return RFLOW_ERR_PARAM;
    if (!p->has_enable_transcode) return RFLOW_ERR_NOT_FOUND;
    *out_enable = p->enable_transcode;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_stream_param_get_video_device_path(librflow_svc_stream_param_t p,
                                                            char* buf, uint32_t buf_len,
                                                            uint32_t* out_needed) {
    SVC_SP_CHECK(p);
    if (!p->has_video_device_path) return RFLOW_ERR_NOT_FOUND;
    return copy_out(p->video_device_path, buf, buf_len, out_needed);
}
rflow_err_t librflow_svc_stream_param_get_video_device_index(librflow_svc_stream_param_t p,
                                                             uint32_t* out_device_index) {
    SVC_SP_CHECK(p);
    if (!out_device_index)          return RFLOW_ERR_PARAM;
    if (!p->has_video_device_index) return RFLOW_ERR_NOT_FOUND;
    *out_device_index = p->video_device_index;
    return RFLOW_OK;
}

#undef SVC_SP_CHECK

}  // extern "C"
