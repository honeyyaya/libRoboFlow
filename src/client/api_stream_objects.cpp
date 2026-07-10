#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "public/object_access_api.h"

#include "base/abi_string_copy.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

constexpr uint32_t kIceServerUrlMaxLen      = 511u;
constexpr uint32_t kIceTurnCredentialMaxLen = 127u;

std::string TrimCopyToken(const char* s) {
    if (!s) {
        return {};
    }
    std::string t(s);
    const auto begin = std::find_if_not(t.begin(), t.end(),
                                        [](unsigned char c) { return std::isspace(c) != 0; });
    const auto rend =
        std::find_if_not(t.rbegin(), t.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= rend) {
        return {};
    }
    return std::string(begin, rend);
}

rflow_err_t CopyRequiredIceUrl(const char* url, std::string* out) {
    if (!out) {
        return RFLOW_ERR_PARAM;
    }
    std::string t = TrimCopyToken(url);
    if (t.empty() || t.size() > kIceServerUrlMaxLen) {
        return RFLOW_ERR_PARAM;
    }
    *out = std::move(t);
    return RFLOW_OK;
}

rflow_err_t CopyOptionalIceCredential(const char* value, std::string* out) {
    if (!out) {
        return RFLOW_ERR_PARAM;
    }
    if (!value) {
        out->clear();
        return RFLOW_OK;
    }
    std::string t = TrimCopyToken(value);
    if (t.size() > kIceTurnCredentialMaxLen) {
        return RFLOW_ERR_PARAM;
    }
    *out = std::move(t);
    return RFLOW_OK;
}

}  // namespace

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

rflow_err_t librflow_stream_param_set_stun_server(librflow_stream_param_t p, const char* stun_url) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (const rflow_err_t rc = CopyRequiredIceUrl(stun_url, &p->stun_server); rc != RFLOW_OK) {
        return rc;
    }
    p->has_stun_server = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_set_turn_server(librflow_stream_param_t p,
                                                  const char* turn_url,
                                                  const char* username,
                                                  const char* password) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (const rflow_err_t rc = CopyRequiredIceUrl(turn_url, &p->turn_server); rc != RFLOW_OK) {
        return rc;
    }
    if (const rflow_err_t rc = CopyOptionalIceCredential(username, &p->turn_username); rc != RFLOW_OK) {
        return rc;
    }
    if (const rflow_err_t rc = CopyOptionalIceCredential(password, &p->turn_password); rc != RFLOW_OK) {
        return rc;
    }
    p->has_turn_server = true;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_param_get_stun_server(librflow_stream_param_t p,
                                                  char* buf,
                                                  uint32_t buf_len,
                                                  uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!p->has_stun_server) {
        return RFLOW_ERR_NOT_FOUND;
    }
    return rflow::common::base::CopyOutString(p->stun_server, buf, buf_len, out_needed);
}

rflow_err_t librflow_stream_param_get_turn_server(librflow_stream_param_t p,
                                                  char* url_buf,
                                                  uint32_t url_buf_len,
                                                  uint32_t* out_url_needed,
                                                  char* username_buf,
                                                  uint32_t username_buf_len,
                                                  uint32_t* out_username_needed,
                                                  char* password_buf,
                                                  uint32_t password_buf_len,
                                                  uint32_t* out_password_needed) {
    RFLOW_CHECK_HANDLE(p, rflow::client::kMagicStreamParam);
    if (!p->has_turn_server) {
        return RFLOW_ERR_NOT_FOUND;
    }
    const rflow_err_t url_rc =
        rflow::common::base::CopyOutString(p->turn_server, url_buf, url_buf_len, out_url_needed);
    if (url_rc != RFLOW_OK) {
        return url_rc;
    }
    (void)rflow::common::base::CopyOutString(p->turn_username, username_buf, username_buf_len,
                                             out_username_needed);
    (void)rflow::common::base::CopyOutString(p->turn_password, password_buf, password_buf_len,
                                             out_password_needed);
    return RFLOW_OK;
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
