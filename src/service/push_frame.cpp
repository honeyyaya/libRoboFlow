#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_svc_push_frame_t librflow_svc_push_frame_create(void) {
    auto* p = new (std::nothrow) librflow_svc_push_frame_s();
    if (!p) return nullptr;
    p->magic = rflow::service::kMagicPushFrame;
    p->flush = true;
    return p;
}

void librflow_svc_push_frame_destroy(librflow_svc_push_frame_t f) {
    if (!f || f->magic != rflow::service::kMagicPushFrame) return;
    f->magic = 0;
    delete f;
}

#define SVC_PF_CHECK(f) RFLOW_CHECK_HANDLE(f, rflow::service::kMagicPushFrame)

/* ---------------- setters ---------------- */

rflow_err_t librflow_svc_push_frame_set_codec(librflow_svc_push_frame_t f, rflow_codec_t c) {
    SVC_PF_CHECK(f);
    f->codec     = c;
    f->has_codec = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_type(librflow_svc_push_frame_t f, rflow_frame_type_t t) {
    SVC_PF_CHECK(f);
    f->type     = t;
    f->has_type = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_data(librflow_svc_push_frame_t f,
                                              const void* data, uint32_t size) {
    SVC_PF_CHECK(f);
    if ((data == nullptr) != (size == 0)) return RFLOW_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    f->data.assign(p, p + size);
    f->has_data = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_size(librflow_svc_push_frame_t f, uint32_t w, uint32_t h) {
    SVC_PF_CHECK(f);
    f->width    = w;
    f->height   = h;
    f->has_size = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_pts_ms(librflow_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f);
    f->pts_ms     = v;
    f->has_pts_ms = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_utc_ms(librflow_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f);
    f->utc_ms     = v;
    f->has_utc_ms = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_seq(librflow_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f);
    f->seq     = v;
    f->has_seq = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_flush(librflow_svc_push_frame_t f, bool v) {
    SVC_PF_CHECK(f);
    f->flush     = v;
    f->has_flush = true;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_set_offset(librflow_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f);
    f->offset     = v;
    f->has_offset = true;
    return RFLOW_OK;
}

/* ---------------- getters ---------------- */

rflow_err_t librflow_svc_push_frame_get_codec(librflow_svc_push_frame_t f, rflow_codec_t* out_codec) {
    SVC_PF_CHECK(f);
    if (!out_codec)   return RFLOW_ERR_PARAM;
    if (!f->has_codec) return RFLOW_ERR_NOT_FOUND;
    *out_codec = f->codec;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_type(librflow_svc_push_frame_t f, rflow_frame_type_t* out_type) {
    SVC_PF_CHECK(f);
    if (!out_type)   return RFLOW_ERR_PARAM;
    if (!f->has_type) return RFLOW_ERR_NOT_FOUND;
    *out_type = f->type;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_data(librflow_svc_push_frame_t f,
                                              const void** out_data, uint32_t* out_size) {
    SVC_PF_CHECK(f);
    if (!out_data || !out_size) return RFLOW_ERR_PARAM;
    if (!f->has_data)           return RFLOW_ERR_NOT_FOUND;
    *out_data = f->data.data();
    *out_size = static_cast<uint32_t>(f->data.size());
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_size(librflow_svc_push_frame_t f,
                                              uint32_t* out_width, uint32_t* out_height) {
    SVC_PF_CHECK(f);
    if (!out_width || !out_height) return RFLOW_ERR_PARAM;
    if (!f->has_size)               return RFLOW_ERR_NOT_FOUND;
    *out_width  = f->width;
    *out_height = f->height;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_pts_ms(librflow_svc_push_frame_t f, uint64_t* out_pts_ms) {
    SVC_PF_CHECK(f);
    if (!out_pts_ms)   return RFLOW_ERR_PARAM;
    if (!f->has_pts_ms) return RFLOW_ERR_NOT_FOUND;
    *out_pts_ms = f->pts_ms;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_utc_ms(librflow_svc_push_frame_t f, uint64_t* out_utc_ms) {
    SVC_PF_CHECK(f);
    if (!out_utc_ms)   return RFLOW_ERR_PARAM;
    if (!f->has_utc_ms) return RFLOW_ERR_NOT_FOUND;
    *out_utc_ms = f->utc_ms;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_seq(librflow_svc_push_frame_t f, uint32_t* out_seq) {
    SVC_PF_CHECK(f);
    if (!out_seq)   return RFLOW_ERR_PARAM;
    if (!f->has_seq) return RFLOW_ERR_NOT_FOUND;
    *out_seq = f->seq;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_flush(librflow_svc_push_frame_t f, bool* out_flush) {
    SVC_PF_CHECK(f);
    if (!out_flush)   return RFLOW_ERR_PARAM;
    if (!f->has_flush) return RFLOW_ERR_NOT_FOUND;
    *out_flush = f->flush;
    return RFLOW_OK;
}
rflow_err_t librflow_svc_push_frame_get_offset(librflow_svc_push_frame_t f, uint32_t* out_offset) {
    SVC_PF_CHECK(f);
    if (!out_offset)   return RFLOW_ERR_PARAM;
    if (!f->has_offset) return RFLOW_ERR_NOT_FOUND;
    *out_offset = f->offset;
    return RFLOW_OK;
}

#undef SVC_PF_CHECK

}  // extern "C"
