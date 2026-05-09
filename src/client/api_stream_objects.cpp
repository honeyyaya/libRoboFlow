#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "common/public/object_access_api.h"

extern "C" {

librflow_stream_cb_t librflow_stream_cb_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_stream_cb_s>(
        rflow::client::kMagicStreamCb);
}

void librflow_stream_cb_destroy(librflow_stream_cb_t cb) {
    rflow::common::abi::DestroyMagicObject(cb, rflow::client::kMagicStreamCb);
}

rflow_err_t librflow_stream_cb_set_on_state(librflow_stream_cb_t cb, librflow_on_stream_state_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::client::kMagicStreamCb, on_state, fn);
}

rflow_err_t librflow_stream_cb_set_on_video(librflow_stream_cb_t cb, librflow_on_video_frame_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::client::kMagicStreamCb, on_video, fn);
}

rflow_err_t librflow_stream_cb_set_on_stream_stats(librflow_stream_cb_t cb, librflow_on_stream_stats_fn fn) {
    RFLOW_SET_FIELD(cb, rflow::client::kMagicStreamCb, on_stream_stats, fn);
}

rflow_err_t librflow_stream_cb_set_userdata(librflow_stream_cb_t cb, void* ud) {
    RFLOW_SET_FIELD(cb, rflow::client::kMagicStreamCb, userdata, ud);
}

librflow_stream_param_t librflow_stream_param_create(void) {
    auto* p = rflow::common::abi::CreateMagicObject<librflow_stream_param_s>(
        rflow::client::kMagicStreamParam);
    if (!p) return nullptr;
    p->preferred_codec = RFLOW_CODEC_UNKNOWN;
    p->video_output_mode = RFLOW_VIDEO_OUTPUT_MODE_CPU_PLANAR;
    p->max_width = 0;
    p->max_height = 0;
    p->fps = 0;
    p->open_timeout_ms = 0;
    p->has_preferred_codec = false;
    p->has_video_output_mode = false;
    p->has_max_size = false;
    p->has_fps = false;
    p->has_open_timeout_ms = false;
    return p;
}

void librflow_stream_param_destroy(librflow_stream_param_t p) {
    rflow::common::abi::DestroyMagicObject(p, rflow::client::kMagicStreamParam);
}

rflow_err_t librflow_stream_param_set_preferred_codec(librflow_stream_param_t p, rflow_codec_t codec) {
    RFLOW_SET_VALUE_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, preferred_codec, has_preferred_codec, codec);
}

rflow_err_t librflow_stream_param_set_preferred_max_size(librflow_stream_param_t p, uint32_t w, uint32_t h) {
    RFLOW_SET_2_VALUES_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, max_width, w, max_height, h, has_max_size);
}

rflow_err_t librflow_stream_param_set_preferred_fps(librflow_stream_param_t p, uint32_t fps) {
    RFLOW_SET_VALUE_WITH_FLAG(p, rflow::client::kMagicStreamParam, fps, has_fps, fps);
}

rflow_err_t librflow_stream_param_set_video_output_mode(librflow_stream_param_t p, rflow_video_output_mode_t mode) {
    RFLOW_SET_VALUE_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, video_output_mode, has_video_output_mode, mode);
}

rflow_err_t librflow_stream_param_set_open_timeout_ms(librflow_stream_param_t p, uint32_t timeout_ms) {
    RFLOW_SET_VALUE_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, open_timeout_ms, has_open_timeout_ms, timeout_ms);
}

rflow_err_t librflow_stream_param_get_preferred_codec(librflow_stream_param_t p, rflow_codec_t* out_codec) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, out_codec, has_preferred_codec, p->preferred_codec);
}

rflow_err_t librflow_stream_param_get_preferred_max_size(librflow_stream_param_t p,
                                                         uint32_t* out_max_width,
                                                         uint32_t* out_max_height) {
    RFLOW_GET_2_VALUES_WITH_FLAG(p,
                                 rflow::client::kMagicStreamParam,
                                 out_max_width,
                                 out_max_height,
                                 has_max_size,
                                 p->max_width,
                                 p->max_height);
}

rflow_err_t librflow_stream_param_get_preferred_fps(librflow_stream_param_t p, uint32_t* out_fps) {
    RFLOW_GET_VALUE_WITH_FLAG(p, rflow::client::kMagicStreamParam, out_fps, has_fps, p->fps);
}

rflow_err_t librflow_stream_param_get_open_timeout_ms(librflow_stream_param_t p, uint32_t* out_timeout_ms) {
    RFLOW_GET_VALUE_WITH_FLAG(
        p, rflow::client::kMagicStreamParam, out_timeout_ms, has_open_timeout_ms, p->open_timeout_ms);
}

}  // extern "C"
