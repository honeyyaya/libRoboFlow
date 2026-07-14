#include "platform/rockchip/rga_nv12_scale.h"

#include "base/env_reader.h"
#include "platform/rockchip/rga_dmabuf_sync.h"
#include "public/log_tagged.h"
#include "runtime/runtime_knobs.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <vector>

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "rk_type.h"

#if defined(RFLOW_HAVE_LIBRGA)
#include <rga/im2d.hpp>
#include <rga/rga.h>
#endif

#define MPP_ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))

namespace rflow::rtc::hw::rockchip_mpp {

namespace {

bool RgaScaleTraceEnabled() {
    static const bool enabled = rflow::core::runtime::ReadBool("RFLOW_RGA_SCALE_TRACE");
    return enabled;
}

unsigned RgaScaleTraceEveryN() {
    static const unsigned every_n = []() {
        const int n = rflow::core::runtime::ReadInt("RFLOW_RGA_SCALE_TRACE_EVERY_N");
        return static_cast<unsigned>(n >= 1 ? n : 60);
    }();
    return every_n;
}

bool ShouldLogRgaScaleOk() {
    if (!RgaScaleTraceEnabled()) {
        return false;
    }
    static std::atomic<unsigned> ok_count{0};
    const unsigned n = RgaScaleTraceEveryN();
    const unsigned c = ok_count.fetch_add(1, std::memory_order_relaxed) + 1;
    return c == 1u || (n > 0 && c % n == 0);
}

int ScalePoolMaxSlots() {
    static const int slots = []() {
        const int v = rflow::common::util::ReadEnvIntInRange("RFLOW_RGA_SCALE_POOL_SLOTS", 8, 2, 32);
        return v;
    }();
    return slots;
}

}  // namespace

int Nv12HorStrideAlign(int width) {
    return static_cast<int>(MPP_ALIGN(static_cast<RK_U32>(width), 16));
}

int Nv12VerStrideAlign(int height) {
    return static_cast<int>(MPP_ALIGN(static_cast<RK_U32>(height), 16));
}

size_t Nv12BufferSizeBytes(int hor_stride, int ver_stride) {
    if (hor_stride <= 0 || ver_stride <= 0) {
        return 0;
    }
    return static_cast<size_t>(hor_stride) * static_cast<size_t>(ver_stride) * 3u / 2u;
}

#if defined(RFLOW_HAVE_LIBRGA)

bool RgaNv12CropScaleDmabuf(const RgaNv12CropScaleParams& p) {
    if (p.src_fd < 0 || p.dst_fd < 0 || p.crop_width <= 0 || p.crop_height <= 0 || p.scaled_width <= 0 ||
        p.scaled_height <= 0 || p.src_buf_size == 0 || p.dst_buf_size == 0) {
        return false;
    }
    if (p.offset_x < 0 || p.offset_y < 0 || p.offset_x + p.crop_width > p.src_width ||
        p.offset_y + p.crop_height > p.src_height) {
        return false;
    }

    const int src_rga_fmt =
        (p.src_mpp_fmt == MPP_FMT_YUV420SP) ? RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_420_SP;
    const int dst_rga_fmt = RK_FORMAT_YCbCr_420_SP;

    DmabufSyncStartRead(p.src_fd);
    DmabufSyncStartWrite(p.dst_fd);

    const im_rect srect{p.offset_x, p.offset_y, p.crop_width, p.crop_height};
    const im_rect drect{0, 0, p.scaled_width, p.scaled_height};

    auto run_improcess = [&](const rga_buffer_t& src, const rga_buffer_t& dst) -> IM_STATUS {
        return improcess(src, dst, {}, srect, drect, {}, IM_SYNC);
    };

    IM_STATUS st = IM_STATUS_FAILED;
    rga_buffer_handle_t hs = 0;
    rga_buffer_handle_t hd = 0;
    const int src_import_sz =
        static_cast<int>(std::min(p.src_buf_size, static_cast<size_t>(std::numeric_limits<int>::max())));
    const int dst_import_sz =
        static_cast<int>(std::min(p.dst_buf_size, static_cast<size_t>(std::numeric_limits<int>::max())));

    hs = importbuffer_fd(p.src_fd, src_import_sz);
    hd = importbuffer_fd(p.dst_fd, dst_import_sz);
    if (hs != 0 && hd != 0) {
        rga_buffer_t src =
            wrapbuffer_handle(hs, p.src_width, p.src_height, src_rga_fmt, p.src_hor_stride, p.src_ver_stride);
        rga_buffer_t dst = wrapbuffer_handle(hd, p.scaled_width, p.scaled_height, dst_rga_fmt, p.dst_hor_stride,
                                              p.dst_ver_stride);
        st = run_improcess(src, dst);
    }
    if (hs != 0) {
        releasebuffer_handle(hs);
        hs = 0;
    }
    if (hd != 0) {
        releasebuffer_handle(hd);
        hd = 0;
    }

    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR) {
        rga_buffer_t src = wrapbuffer_fd_t(p.src_fd, p.src_width, p.src_height, p.src_hor_stride, p.src_ver_stride,
                                           src_rga_fmt);
        rga_buffer_t dst = wrapbuffer_fd_t(p.dst_fd, p.scaled_width, p.scaled_height, p.dst_hor_stride, p.dst_ver_stride,
                                           dst_rga_fmt);
        st = run_improcess(src, dst);
    }

    DmabufSyncEndWrite(p.dst_fd);
    DmabufSyncEndRead(p.src_fd);

    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR) {
        if (RgaScaleTraceEnabled()) {
            RFLOW_LOG_TAG_W("RgaNv12Scale", "improcess failed status=%d (%s) crop=%dx%d@%d,%d -> %dx%d",
                            static_cast<int>(st), imStrError_t(st), p.crop_width, p.crop_height, p.offset_x,
                            p.offset_y, p.scaled_width, p.scaled_height);
        }
        return false;
    }
    if (ShouldLogRgaScaleOk()) {
        RFLOW_LOG_TAG_I("RgaNv12Scale", "ok crop=%dx%d@%d,%d -> %dx%d stride=%dx%d", p.crop_width, p.crop_height,
                        p.offset_x, p.offset_y, p.scaled_width, p.scaled_height, p.dst_hor_stride, p.dst_ver_stride);
    }
    return true;
}

#else  // !RFLOW_HAVE_LIBRGA

bool RgaNv12CropScaleDmabuf(const RgaNv12CropScaleParams& /*params*/) {
    return false;
}

#endif  // RFLOW_HAVE_LIBRGA

MppDrmScaleBufferPool& MppDrmScaleBufferPool::Instance() {
    static MppDrmScaleBufferPool pool;
    return pool;
}

MppDrmScaleBufferPool::MppDrmScaleBufferPool() {
    max_slots_ = ScalePoolMaxSlots();
}

MppDrmScaleBufferPool::~MppDrmScaleBufferPool() {
    std::lock_guard<std::mutex> lock(mu_);
    for (Slot& slot : slots_) {
        if (slot.buf) {
            mpp_buffer_put(slot.buf);
            slot.buf = nullptr;
        }
    }
    slots_.clear();
    if (group_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(group_));
        group_ = nullptr;
    }
}

bool MppDrmScaleBufferPool::EnsureGroupLocked() {
    if (group_) {
        return true;
    }
    MppBufferGroup grp = nullptr;
    if (mpp_buffer_group_get_internal(&grp, MPP_BUFFER_TYPE_DRM) != MPP_OK || !grp) {
        RFLOW_LOG_TAG_E("RgaNv12Scale", "scale pool mpp_buffer_group_get_internal failed");
        return false;
    }
    group_ = grp;
    return true;
}

MppBuffer MppDrmScaleBufferPool::AllocNewLocked(int hor_stride, int ver_stride, size_t capacity) {
    if (!EnsureGroupLocked()) {
        return nullptr;
    }
    MppBuffer buf = nullptr;
    if (mpp_buffer_get(reinterpret_cast<MppBufferGroup>(group_), &buf, capacity) != MPP_OK || !buf) {
        RFLOW_LOG_TAG_E("RgaNv12Scale", "scale pool mpp_buffer_get failed size=%zu", capacity);
        return nullptr;
    }
    Slot slot;
    slot.buf = buf;
    slot.capacity = capacity;
    slot.hor_stride = hor_stride;
    slot.ver_stride = ver_stride;
    slot.in_use = true;
    slots_.push_back(slot);
    return buf;
}

MppBuffer MppDrmScaleBufferPool::Acquire(int scaled_width, int scaled_height, int* out_hor_stride,
                                           int* out_ver_stride, size_t* out_buf_bytes) {
    if (scaled_width <= 0 || scaled_height <= 0 || !out_hor_stride || !out_ver_stride || !out_buf_bytes) {
        return nullptr;
    }
    const int hs = Nv12HorStrideAlign(scaled_width);
    const int vs = Nv12VerStrideAlign(scaled_height);
    const size_t need = Nv12BufferSizeBytes(hs, vs);
    if (need == 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (Slot& slot : slots_) {
        if (slot.in_use || !slot.buf || slot.capacity < need) {
            continue;
        }
        slot.in_use = true;
        *out_hor_stride = hs;
        *out_ver_stride = vs;
        *out_buf_bytes = slot.capacity;
        return slot.buf;
    }

    if (static_cast<int>(slots_.size()) >= max_slots_) {
        RFLOW_LOG_TAG_W("RgaNv12Scale", "scale pool exhausted slots=%zu max=%d need=%zu", slots_.size(), max_slots_,
                        need);
        return nullptr;
    }

    MppBuffer buf = AllocNewLocked(hs, vs, need);
    if (!buf) {
        return nullptr;
    }
    *out_hor_stride = hs;
    *out_ver_stride = vs;
    *out_buf_bytes = need;
    return buf;
}

void MppDrmScaleBufferPool::Release(MppBuffer buf) {
    if (!buf) {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    for (Slot& slot : slots_) {
        if (slot.buf == buf) {
            slot.in_use = false;
            return;
        }
    }
    mpp_buffer_put(buf);
}

}  // namespace rflow::rtc::hw::rockchip_mpp
