#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_stream_param_t librobrt_svc_stream_param_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_stream_param_s();
    if (!p) return nullptr;
    p->magic    = robrt::service::kMagicStreamParam;
    p->rc_mode  = ROBRT_RC_VBR;
    return p;
}

void librobrt_svc_stream_param_destroy(librobrt_svc_stream_param_t p) {
    if (!p || p->magic != robrt::service::kMagicStreamParam) return;
    p->magic = 0;
    delete p;
}

#define SVC_SP_CHECK(p) ROBRT_CHECK_HANDLE(p, robrt::service::kMagicStreamParam)

robrt_err_t librobrt_svc_stream_param_set_in_codec (librobrt_svc_stream_param_t p, robrt_codec_t c) {
    SVC_SP_CHECK(p); p->in_codec = c; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_out_codec(librobrt_svc_stream_param_t p, robrt_codec_t c) {
    SVC_SP_CHECK(p); p->out_codec = c; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_src_size (librobrt_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p); p->src_w = w; p->src_h = h; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_out_size (librobrt_svc_stream_param_t p, uint32_t w, uint32_t h) {
    SVC_SP_CHECK(p); p->out_w = w; p->out_h = h; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_fps      (librobrt_svc_stream_param_t p, uint32_t f) {
    SVC_SP_CHECK(p); p->fps = f; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_gop      (librobrt_svc_stream_param_t p, uint32_t g) {
    SVC_SP_CHECK(p); p->gop = g; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_rc_mode  (librobrt_svc_stream_param_t p, robrt_rc_mode_t r) {
    SVC_SP_CHECK(p); p->rc_mode = r; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_qp       (librobrt_svc_stream_param_t p, uint32_t qp) {
    SVC_SP_CHECK(p); p->qp = qp; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_bitrate  (librobrt_svc_stream_param_t p, uint32_t br, uint32_t max_br) {
    SVC_SP_CHECK(p); p->bitrate_kbps = br; p->max_bitrate_kbps = max_br; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_dynamic_bitrate(librobrt_svc_stream_param_t p, bool e, uint32_t lo, uint32_t hi) {
    SVC_SP_CHECK(p); p->dynamic_bitrate = e; p->lowest_kbps = lo; p->highest_kbps = hi; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_enable_transcode(librobrt_svc_stream_param_t p, bool e) {
    SVC_SP_CHECK(p); p->enable_transcode = e; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_enable_audio(librobrt_svc_stream_param_t p, bool e) {
    SVC_SP_CHECK(p); p->enable_audio = e; return ROBRT_OK;
}
robrt_err_t librobrt_svc_stream_param_set_audio(librobrt_svc_stream_param_t p,
                                                 robrt_audio_codec_t c, uint32_t sr, uint32_t ch, uint32_t sb) {
    SVC_SP_CHECK(p);
    p->audio_codec       = c;
    p->audio_sample_rate = sr;
    p->audio_channel     = ch;
    p->audio_sample_bit  = sb;
    return ROBRT_OK;
}

#undef SVC_SP_CHECK

}  // extern "C"
