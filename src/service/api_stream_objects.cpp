#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include "base/abi_string_copy.h"
#include "public/object_access_api.h"

extern "C" {

librflow_svc_stream_cb_t librflow_svc_stream_cb_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_svc_stream_cb_s>(
        rflow::service::kMagicStreamCb);
}

void librflow_svc_stream_cb_destroy(librflow_svc_stream_cb_t cb) {
    rflow::common::abi::DestroyMagicObject(cb, rflow::service::kMagicStreamCb);
}

rflow_err_t librflow_svc_stream_cb_set_on_state(librflow_svc_stream_cb_t cb,
                                                 librflow_svc_on_stream_state_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::service::kMagicStreamCb, on_state, fn);
}

rflow_err_t librflow_svc_stream_cb_set_on_encoded_video(librflow_svc_stream_cb_t cb,
                                                         librflow_svc_on_encoded_video_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::service::kMagicStreamCb, on_encoded_video, fn);
}

rflow_err_t librflow_svc_stream_cb_set_on_stream_stats(librflow_svc_stream_cb_t cb,
                                                        librflow_svc_on_stream_stats_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::service::kMagicStreamCb, on_stream_stats, fn);
}

rflow_err_t librflow_svc_stream_cb_set_userdata(librflow_svc_stream_cb_t cb, void* ud) {
    RFLOW_SET_FIELD(cb, rflow::service::kMagicStreamCb, userdata, ud);
}

librflow_svc_stream_param_t librflow_svc_stream_param_create(void) {
    auto* p = rflow::common::abi::CreateMagicObject<librflow_svc_stream_param_s>(
        rflow::service::kMagicStreamParam);
    if (!p) return nullptr;
    p->rc_mode = RFLOW_RC_VBR;
    return p;
}

void librflow_svc_stream_param_destroy(librflow_svc_stream_param_t p) {
    rflow::common::abi::DestroyMagicObject(p, rflow::service::kMagicStreamParam);
}

rflow_err_t librflow_svc_stream_param_set_in_codec(librflow_svc_stream_param_t p, rflow_codec_t c) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, in_codec, has_in_codec, c);
}

rflow_err_t librflow_svc_stream_param_set_out_codec(librflow_svc_stream_param_t p, rflow_codec_t c) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, out_codec, has_out_codec, c);
}

rflow_err_t librflow_svc_stream_param_set_src_size(librflow_svc_stream_param_t p, uint32_t w, uint32_t h) {
    RFLOW_SET_2_VALUES_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, src_w, w, src_h, h, has_src_size);
}

rflow_err_t librflow_svc_stream_param_set_out_size(librflow_svc_stream_param_t p, uint32_t w, uint32_t h) {
    RFLOW_SET_2_VALUES_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_w, w, out_h, h, has_out_size);
}

rflow_err_t librflow_svc_stream_param_set_fps(librflow_svc_stream_param_t p, uint32_t f) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, fps, has_fps, f);
}

rflow_err_t librflow_svc_stream_param_set_gop(librflow_svc_stream_param_t p, uint32_t g) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, gop, has_gop, g);
}

rflow_err_t librflow_svc_stream_param_set_rc_mode(librflow_svc_stream_param_t p, rflow_rc_mode_t r) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, rc_mode, has_rc_mode, r);
}

rflow_err_t librflow_svc_stream_param_set_qp(librflow_svc_stream_param_t p, uint32_t qp) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, qp, has_qp, qp);
}

rflow_err_t librflow_svc_stream_param_set_bitrate(librflow_svc_stream_param_t p, uint32_t br, uint32_t max_br) {
    RFLOW_SET_2_VALUES_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, bitrate_kbps, br, max_bitrate_kbps, max_br, has_bitrate);
}

rflow_err_t librflow_svc_stream_param_set_dynamic_bitrate(librflow_svc_stream_param_t p, bool e, uint32_t lo, uint32_t hi) {
    RFLOW_SET_3_VALUES_WITH_FLAG(
        p,
        rflow::service::kMagicStreamParam,
        dynamic_bitrate,
        e,
        lowest_kbps,
        lo,
        highest_kbps,
        hi,
        has_dynamic_bitrate);
}

rflow_err_t librflow_svc_stream_param_set_enable_transcode(librflow_svc_stream_param_t p, bool e) {
    RFLOW_SET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, enable_transcode, has_enable_transcode, e);
}

rflow_err_t librflow_svc_stream_param_set_video_device_path(librflow_svc_stream_param_t p, const char* v) {
    RFLOW_CHECK_HANDLE(p, rflow::service::kMagicStreamParam);
    p->video_device_path = v ? v : "";
    p->has_video_device_path = true;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_param_set_video_device_index(librflow_svc_stream_param_t p, uint32_t index) {
    RFLOW_SET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, video_device_index, has_video_device_index, index);
}

rflow_err_t librflow_svc_stream_param_get_in_codec(librflow_svc_stream_param_t p, rflow_codec_t* out_codec) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_codec, has_in_codec, p->in_codec);
}

rflow_err_t librflow_svc_stream_param_get_out_codec(librflow_svc_stream_param_t p, rflow_codec_t* out_codec) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_codec, has_out_codec, p->out_codec);
}

rflow_err_t librflow_svc_stream_param_get_src_size(librflow_svc_stream_param_t p, uint32_t* out_width, uint32_t* out_height) {
    RFLOW_GET_2_VALUES_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_width, out_height, has_src_size, p->src_w, p->src_h);
}

rflow_err_t librflow_svc_stream_param_get_out_size(librflow_svc_stream_param_t p, uint32_t* out_width, uint32_t* out_height) {
    RFLOW_GET_2_VALUES_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_width, out_height, has_out_size, p->out_w, p->out_h);
}

rflow_err_t librflow_svc_stream_param_get_fps(librflow_svc_stream_param_t p, uint32_t* out_fps) {
    RFLOW_GET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, out_fps, has_fps, p->fps);
}

rflow_err_t librflow_svc_stream_param_get_gop(librflow_svc_stream_param_t p, uint32_t* out_gop) {
    RFLOW_GET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, out_gop, has_gop, p->gop);
}

rflow_err_t librflow_svc_stream_param_get_rc_mode(librflow_svc_stream_param_t p, rflow_rc_mode_t* out_rc) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_rc, has_rc_mode, p->rc_mode);
}

rflow_err_t librflow_svc_stream_param_get_qp(librflow_svc_stream_param_t p, uint32_t* out_qp) {
    RFLOW_GET_VALUE_WITH_FLAG(p, rflow::service::kMagicStreamParam, out_qp, has_qp, p->qp);
}

rflow_err_t librflow_svc_stream_param_get_bitrate(librflow_svc_stream_param_t p,
                                                   uint32_t* out_bitrate_kbps,
                                                   uint32_t* out_max_bitrate_kbps) {
    RFLOW_GET_2_VALUES_WITH_FLAG(p,
                                 rflow::service::kMagicStreamParam,
                                 out_bitrate_kbps,
                                 out_max_bitrate_kbps,
                                 has_bitrate,
                                 p->bitrate_kbps,
                                 p->max_bitrate_kbps);
}

rflow_err_t librflow_svc_stream_param_get_dynamic_bitrate(librflow_svc_stream_param_t p,
                                                           bool* out_enable,
                                                           uint32_t* out_lowest_kbps,
                                                           uint32_t* out_highest_kbps) {
    RFLOW_GET_3_VALUES_WITH_FLAG(p,
                                 rflow::service::kMagicStreamParam,
                                 out_enable,
                                 out_lowest_kbps,
                                 out_highest_kbps,
                                 has_dynamic_bitrate,
                                 p->dynamic_bitrate,
                                 p->lowest_kbps,
                                 p->highest_kbps);
}

rflow_err_t librflow_svc_stream_param_get_enable_transcode(librflow_svc_stream_param_t p, bool* out_enable) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_enable, has_enable_transcode, p->enable_transcode);
}

rflow_err_t librflow_svc_stream_param_get_video_device_path(librflow_svc_stream_param_t p,
                                                             char* buf,
                                                             uint32_t buf_len,
                                                             uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(p, rflow::service::kMagicStreamParam);
    if (!p->has_video_device_path) return RFLOW_ERR_NOT_FOUND;
    return rflow::common::base::CopyOutString(p->video_device_path, buf, buf_len, out_needed);
}

rflow_err_t librflow_svc_stream_param_get_video_device_index(librflow_svc_stream_param_t p,
                                                              uint32_t* out_device_index) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::service::kMagicStreamParam, out_device_index, has_video_device_index, p->video_device_index);
}

librflow_svc_push_frame_t librflow_svc_push_frame_create(void) {
    auto* p = rflow::common::abi::CreateMagicObject<librflow_svc_push_frame_s>(
        rflow::service::kMagicPushFrame);
    if (!p) return nullptr;
    p->flush = true;
    return p;
}

void librflow_svc_push_frame_destroy(librflow_svc_push_frame_t f) {
    rflow::common::abi::DestroyMagicObject(f, rflow::service::kMagicPushFrame);
}

rflow_err_t librflow_svc_push_frame_set_codec(librflow_svc_push_frame_t f, rflow_codec_t c) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, codec, has_codec, c);
}

rflow_err_t librflow_svc_push_frame_set_type(librflow_svc_push_frame_t f, rflow_frame_type_t t) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, type, has_type, t);
}

rflow_err_t librflow_svc_push_frame_set_data(librflow_svc_push_frame_t f, const void* data, uint32_t size) {
    RFLOW_CHECK_HANDLE(f, rflow::service::kMagicPushFrame);
    if ((data == nullptr) != (size == 0)) return RFLOW_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    f->data.assign(p, p + size);
    f->has_data = true;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_push_frame_set_size(librflow_svc_push_frame_t f, uint32_t w, uint32_t h) {
    RFLOW_SET_2_VALUES_WITH_FLAG(f, rflow::service::kMagicPushFrame, width, w, height, h, has_size);
}

rflow_err_t librflow_svc_push_frame_set_pts_ms(librflow_svc_push_frame_t f, uint64_t v) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, pts_ms, has_pts_ms, v);
}

rflow_err_t librflow_svc_push_frame_set_utc_ms(librflow_svc_push_frame_t f, uint64_t v) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, utc_ms, has_utc_ms, v);
}

rflow_err_t librflow_svc_push_frame_set_seq(librflow_svc_push_frame_t f, uint32_t v) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, seq, has_seq, v);
}

rflow_err_t librflow_svc_push_frame_set_flush(librflow_svc_push_frame_t f, bool v) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, flush, has_flush, v);
}

rflow_err_t librflow_svc_push_frame_set_offset(librflow_svc_push_frame_t f, uint32_t v) {
    RFLOW_SET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, offset, has_offset, v);
}

rflow_err_t librflow_svc_push_frame_get_codec(librflow_svc_push_frame_t f, rflow_codec_t* out_codec) {
    RFLOW_GET_VALUE_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_codec, has_codec, f->codec);
}

rflow_err_t librflow_svc_push_frame_get_type(librflow_svc_push_frame_t f, rflow_frame_type_t* out_type) {
    RFLOW_GET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, out_type, has_type, f->type);
}

rflow_err_t librflow_svc_push_frame_get_data(librflow_svc_push_frame_t f, const void** out_data, uint32_t* out_size) {
    RFLOW_CHECK_HANDLE(f, rflow::service::kMagicPushFrame);
    if (!out_data || !out_size) return RFLOW_ERR_PARAM;
    if (!f->has_data) return RFLOW_ERR_NOT_FOUND;
    *out_data = f->data.data();
    *out_size = static_cast<uint32_t>(f->data.size());
    return RFLOW_OK;
}

rflow_err_t librflow_svc_push_frame_get_size(librflow_svc_push_frame_t f, uint32_t* out_width, uint32_t* out_height) {
    RFLOW_GET_2_VALUES_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_width, out_height, has_size, f->width, f->height);
}

rflow_err_t librflow_svc_push_frame_get_pts_ms(librflow_svc_push_frame_t f, uint64_t* out_pts_ms) {
    RFLOW_GET_VALUE_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_pts_ms, has_pts_ms, f->pts_ms);
}

rflow_err_t librflow_svc_push_frame_get_utc_ms(librflow_svc_push_frame_t f, uint64_t* out_utc_ms) {
    RFLOW_GET_VALUE_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_utc_ms, has_utc_ms, f->utc_ms);
}

rflow_err_t librflow_svc_push_frame_get_seq(librflow_svc_push_frame_t f, uint32_t* out_seq) {
    RFLOW_GET_VALUE_WITH_FLAG(f, rflow::service::kMagicPushFrame, out_seq, has_seq, f->seq);
}

rflow_err_t librflow_svc_push_frame_get_flush(librflow_svc_push_frame_t f, bool* out_flush) {
    RFLOW_GET_VALUE_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_flush, has_flush, f->flush);
}

rflow_err_t librflow_svc_push_frame_get_offset(librflow_svc_push_frame_t f, uint32_t* out_offset) {
    RFLOW_GET_VALUE_WITH_FLAG(
        f, rflow::service::kMagicPushFrame, out_offset, has_offset, f->offset);
}

}  // extern "C"
