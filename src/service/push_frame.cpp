#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_push_frame_t librobrt_svc_push_frame_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_push_frame_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicPushFrame;
    p->flush = true;
    return p;
}

void librobrt_svc_push_frame_destroy(librobrt_svc_push_frame_t f) {
    if (!f || f->magic != robrt::service::kMagicPushFrame) return;
    f->magic = 0;
    delete f;
}

#define SVC_PF_CHECK(f) ROBRT_CHECK_HANDLE(f, robrt::service::kMagicPushFrame)

/* ---------------- setters ---------------- */

robrt_err_t librobrt_svc_push_frame_set_codec(librobrt_svc_push_frame_t f, robrt_codec_t c) {
    SVC_PF_CHECK(f);
    f->codec     = c;
    f->has_codec = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_type(librobrt_svc_push_frame_t f, robrt_frame_type_t t) {
    SVC_PF_CHECK(f);
    f->type     = t;
    f->has_type = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_data(librobrt_svc_push_frame_t f,
                                              const void* data, uint32_t size) {
    SVC_PF_CHECK(f);
    if ((data == nullptr) != (size == 0)) return ROBRT_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    f->data.assign(p, p + size);
    f->has_data = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_size(librobrt_svc_push_frame_t f, uint32_t w, uint32_t h) {
    SVC_PF_CHECK(f);
    f->width    = w;
    f->height   = h;
    f->has_size = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_pts_ms(librobrt_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f);
    f->pts_ms     = v;
    f->has_pts_ms = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_utc_ms(librobrt_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f);
    f->utc_ms     = v;
    f->has_utc_ms = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_seq(librobrt_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f);
    f->seq     = v;
    f->has_seq = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_flush(librobrt_svc_push_frame_t f, bool v) {
    SVC_PF_CHECK(f);
    f->flush     = v;
    f->has_flush = true;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_offset(librobrt_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f);
    f->offset     = v;
    f->has_offset = true;
    return ROBRT_OK;
}

/* ---------------- getters ---------------- */

robrt_err_t librobrt_svc_push_frame_get_codec(librobrt_svc_push_frame_t f, robrt_codec_t* out_codec) {
    SVC_PF_CHECK(f);
    if (!out_codec)   return ROBRT_ERR_PARAM;
    if (!f->has_codec) return ROBRT_ERR_NOT_FOUND;
    *out_codec = f->codec;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_type(librobrt_svc_push_frame_t f, robrt_frame_type_t* out_type) {
    SVC_PF_CHECK(f);
    if (!out_type)   return ROBRT_ERR_PARAM;
    if (!f->has_type) return ROBRT_ERR_NOT_FOUND;
    *out_type = f->type;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_data(librobrt_svc_push_frame_t f,
                                              const void** out_data, uint32_t* out_size) {
    SVC_PF_CHECK(f);
    if (!out_data || !out_size) return ROBRT_ERR_PARAM;
    if (!f->has_data)           return ROBRT_ERR_NOT_FOUND;
    *out_data = f->data.data();
    *out_size = static_cast<uint32_t>(f->data.size());
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_size(librobrt_svc_push_frame_t f,
                                              uint32_t* out_width, uint32_t* out_height) {
    SVC_PF_CHECK(f);
    if (!out_width || !out_height) return ROBRT_ERR_PARAM;
    if (!f->has_size)               return ROBRT_ERR_NOT_FOUND;
    *out_width  = f->width;
    *out_height = f->height;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_pts_ms(librobrt_svc_push_frame_t f, uint64_t* out_pts_ms) {
    SVC_PF_CHECK(f);
    if (!out_pts_ms)   return ROBRT_ERR_PARAM;
    if (!f->has_pts_ms) return ROBRT_ERR_NOT_FOUND;
    *out_pts_ms = f->pts_ms;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_utc_ms(librobrt_svc_push_frame_t f, uint64_t* out_utc_ms) {
    SVC_PF_CHECK(f);
    if (!out_utc_ms)   return ROBRT_ERR_PARAM;
    if (!f->has_utc_ms) return ROBRT_ERR_NOT_FOUND;
    *out_utc_ms = f->utc_ms;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_seq(librobrt_svc_push_frame_t f, uint32_t* out_seq) {
    SVC_PF_CHECK(f);
    if (!out_seq)   return ROBRT_ERR_PARAM;
    if (!f->has_seq) return ROBRT_ERR_NOT_FOUND;
    *out_seq = f->seq;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_flush(librobrt_svc_push_frame_t f, bool* out_flush) {
    SVC_PF_CHECK(f);
    if (!out_flush)   return ROBRT_ERR_PARAM;
    if (!f->has_flush) return ROBRT_ERR_NOT_FOUND;
    *out_flush = f->flush;
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_get_offset(librobrt_svc_push_frame_t f, uint32_t* out_offset) {
    SVC_PF_CHECK(f);
    if (!out_offset)   return ROBRT_ERR_PARAM;
    if (!f->has_offset) return ROBRT_ERR_NOT_FOUND;
    *out_offset = f->offset;
    return ROBRT_OK;
}

#undef SVC_PF_CHECK

}  // extern "C"
