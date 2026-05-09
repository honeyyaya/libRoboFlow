#include "rflow/librflow_common.h"

#include "common/media/frame_types.h"

#include <cstddef>
#include <cstring>

namespace {

bool IsValidFrame(librflow_video_frame_t frame) {
    return frame && frame->magic == rflow::kMagicVideoFrame;
}

bool EnsurePayloadMaterialized(const librflow_video_frame_s* frame) {
    if (!frame) return false;

    std::lock_guard<std::mutex> lock(frame->payload_mutex);
    if (!frame->payload.empty()) return true;
    if (frame->plane_count == 0) return false;

    switch (frame->codec) {
        case RFLOW_CODEC_I420: {
            if (frame->plane_count < 3) return false;
            size_t total_size = 0;
            for (uint32_t plane_index = 0; plane_index < 3; ++plane_index) {
                total_size += static_cast<size_t>(frame->plane_widths[plane_index]) *
                              static_cast<size_t>(frame->plane_heights[plane_index]);
            }
            frame->payload.resize(total_size);
            uint8_t* write_ptr = frame->payload.data();
            for (uint32_t plane_index = 0; plane_index < 3; ++plane_index) {
                const uint32_t row_bytes = frame->plane_widths[plane_index];
                const uint32_t row_count = frame->plane_heights[plane_index];
                for (uint32_t row = 0; row < row_count; ++row) {
                    std::memcpy(write_ptr,
                                frame->plane_data[plane_index] +
                                    static_cast<size_t>(row) * frame->plane_strides[plane_index],
                                row_bytes);
                    write_ptr += row_bytes;
                }
            }
            return true;
        }
        case RFLOW_CODEC_NV12: {
            if (frame->plane_count < 2) return false;
            const size_t y_plane_size =
                static_cast<size_t>(frame->plane_widths[0]) * frame->plane_heights[0];
            const size_t uv_plane_size =
                static_cast<size_t>(frame->plane_widths[1]) * frame->plane_heights[1];
            frame->payload.resize(y_plane_size + uv_plane_size);
            uint8_t* write_ptr = frame->payload.data();
            for (uint32_t row = 0; row < frame->plane_heights[0]; ++row) {
                std::memcpy(write_ptr,
                            frame->plane_data[0] + static_cast<size_t>(row) * frame->plane_strides[0],
                            frame->plane_widths[0]);
                write_ptr += frame->plane_widths[0];
            }
            for (uint32_t row = 0; row < frame->plane_heights[1]; ++row) {
                std::memcpy(write_ptr,
                            frame->plane_data[1] + static_cast<size_t>(row) * frame->plane_strides[1],
                            frame->plane_widths[1]);
                write_ptr += frame->plane_widths[1];
            }
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

extern "C" {

rflow_video_frame_backend_t librflow_video_frame_get_backend(librflow_video_frame_t frame) {
    return IsValidFrame(frame) ? frame->backend : RFLOW_VIDEO_FRAME_BACKEND_UNKNOWN;
}

rflow_native_handle_type_t librflow_video_frame_get_native_handle_type(librflow_video_frame_t frame) {
    return IsValidFrame(frame) ? frame->native_handle_type : RFLOW_NATIVE_HANDLE_NONE;
}

uint32_t librflow_video_frame_get_oes_texture_id(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame) || frame->native_handle_type != RFLOW_NATIVE_HANDLE_ANDROID_OES_TEXTURE) {
        return 0;
    }
    return frame->oes_texture_id;
}

void* librflow_video_frame_get_android_hardware_buffer(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame) || frame->native_handle_type != RFLOW_NATIVE_HANDLE_ANDROID_HARDWARE_BUFFER) {
        return nullptr;
    }
    return frame->android_hardware_buffer;
}

int32_t librflow_video_frame_get_sync_fence_fd(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame)) return -1;
    return frame->sync_fence_fd;
}

rflow_err_t librflow_video_frame_get_gl_prepare_callback(librflow_video_frame_t frame,
                                                         librflow_gl_prepare_fn* out_fn,
                                                         void** out_userdata) {
    if (!IsValidFrame(frame) || !out_fn || !out_userdata) return RFLOW_ERR_PARAM;
    *out_fn = nullptr;
    *out_userdata = nullptr;

    if (!frame->gl_prepare_fn) return RFLOW_ERR_NOT_SUPPORT;

    *out_fn = frame->gl_prepare_fn;
    *out_userdata = frame->gl_prepare_userdata;
    return RFLOW_OK;
}

rflow_err_t librflow_video_frame_acquire_for_sampling(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame)) return RFLOW_ERR_PARAM;
    if (!frame->sampling_acquire_fn) return RFLOW_ERR_NOT_SUPPORT;
    return frame->sampling_acquire_fn(frame->sampling_userdata);
}

void librflow_video_frame_release_after_sampling(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame) || !frame->sampling_release_fn) return;
    frame->sampling_release_fn(frame->sampling_userdata);
}

rflow_codec_t librflow_video_frame_get_codec(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame)) return RFLOW_CODEC_UNKNOWN;
    return frame->codec;
}

rflow_frame_type_t librflow_video_frame_get_type(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame)) return RFLOW_FRAME_UNKNOWN;
    return frame->type;
}

uint32_t librflow_video_frame_get_plane_count(librflow_video_frame_t frame) {
    return IsValidFrame(frame) ? frame->plane_count : 0;
}

const uint8_t* librflow_video_frame_get_plane_data(librflow_video_frame_t frame, uint32_t plane_index) {
    if (!IsValidFrame(frame) || plane_index >= frame->plane_count) return nullptr;
    return frame->plane_data[plane_index];
}

uint32_t librflow_video_frame_get_plane_stride(librflow_video_frame_t frame, uint32_t plane_index) {
    if (!IsValidFrame(frame) || plane_index >= frame->plane_count) return 0;
    return frame->plane_strides[plane_index];
}

uint32_t librflow_video_frame_get_plane_width(librflow_video_frame_t frame, uint32_t plane_index) {
    if (!IsValidFrame(frame) || plane_index >= frame->plane_count) return 0;
    return frame->plane_widths[plane_index];
}

uint32_t librflow_video_frame_get_plane_height(librflow_video_frame_t frame, uint32_t plane_index) {
    if (!IsValidFrame(frame) || plane_index >= frame->plane_count) return 0;
    return frame->plane_heights[plane_index];
}

const uint8_t* librflow_video_frame_get_data(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame) || !EnsurePayloadMaterialized(frame)) return nullptr;
    return frame->payload.data();
}

uint32_t librflow_video_frame_get_data_size(librflow_video_frame_t frame) {
    if (!IsValidFrame(frame) || !EnsurePayloadMaterialized(frame)) return 0;
    return static_cast<uint32_t>(frame->payload.size());
}

uint32_t librflow_video_frame_get_width(librflow_video_frame_t frame) { return IsValidFrame(frame) ? frame->width : 0; }
uint32_t librflow_video_frame_get_height(librflow_video_frame_t frame) { return IsValidFrame(frame) ? frame->height : 0; }
uint64_t librflow_video_frame_get_pts_ms(librflow_video_frame_t frame) { return IsValidFrame(frame) ? frame->pts_ms : 0; }
uint64_t librflow_video_frame_get_utc_ms(librflow_video_frame_t frame) { return IsValidFrame(frame) ? frame->utc_ms : 0; }
uint32_t librflow_video_frame_get_seq(librflow_video_frame_t frame) { return IsValidFrame(frame) ? frame->seq : 0; }
int32_t librflow_video_frame_get_index(librflow_video_frame_t frame) {
    return IsValidFrame(frame) ? frame->stream_index : -1;
}

librflow_video_frame_t librflow_video_frame_retain(librflow_video_frame_t frame) {
    if (!frame || frame->magic != rflow::kMagicVideoFrame) return nullptr;
    const_cast<librflow_video_frame_s*>(frame)->refcount.fetch_add(1, std::memory_order_relaxed);
    return frame;
}

void librflow_video_frame_release(librflow_video_frame_t frame) {
    if (!frame || frame->magic != rflow::kMagicVideoFrame) return;
    auto* mutable_frame = const_cast<librflow_video_frame_s*>(frame);
    if (mutable_frame->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        mutable_frame->magic = 0;
        delete mutable_frame;
    }
}

}  // extern "C"
