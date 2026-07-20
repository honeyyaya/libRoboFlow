// Rockchip MPP H.264 encoder for WebRTC (RK3588 ??BSP ?? librockchip_mpp).

#define MODULE_TAG "rflow_mpp_h264_enc"

#include "platform/rockchip/h264_encoder.h"

#include "base/env_reader.h"
#include "platform/rockchip/native_dec_frame_buffer.h"
#include "platform/rockchip/rga_dmabuf_sync.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "public/log_tagged.h"
#include "rtc/push_pipeline_drop_stats.h"
#include "rtc/push_pipeline_drop_stats.h"
#include <unistd.h>
#include "api/array_view.h"
#include "api/video/video_codec_constants.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "rtc_base/experiments/encoder_info_settings.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_timing.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_codec.h"
#include "common_video/h264/h264_common.h"
#include "common_video/h264/h264_bitstream_parser.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"

#include "mpp_buffer.h"
#include "mpp_err.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "mpp_task.h"
#include "rk_mpi.h"
#include "rk_mpi_cmd.h"
#include "rk_type.h"
#include "rk_venc_cfg.h"
#include "rk_venc_cmd.h"
#include "rk_venc_rc.h"

namespace rflow::rtc::hw::rockchip_mpp {

namespace {

bool MediaTimingTraceEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("RFLOW_MEDIA_TIMING_TRACE");
        return v && v[0] == '1';
    }();
    return enabled;
}

unsigned MediaTimingTraceEveryN() {
    static const unsigned every_n = []() {
        const char* v = std::getenv("RFLOW_MEDIA_TIMING_TRACE_EVERY_N");
        if (v) {
            const int n = std::atoi(v);
            if (n >= 1 && n <= 600) {
                return static_cast<unsigned>(n);
            }
        }
        return 30u;
    }();
    return every_n;
}

// ????????????????????????????? 2026-04-01 14:30:05.123
std::string CurrentLocalDateTimeYmdHmsMs() {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::system_clock;

    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const long long ms =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000LL;
    std::tm tm_storage {};
#if defined(_WIN32)
    if (localtime_s(&tm_storage, &t) != 0) {
        return "1970-01-01 00:00:00.000";
    }
#else
    if (!localtime_r(&t, &tm_storage)) {
        return "1970-01-01 00:00:00.000";
    }
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_storage) == 0) {
        return "1970-01-01 00:00:00.000";
    }
    char out[48];
    std::snprintf(out, sizeof(out), "%s.%03lld", buf, ms >= 0 ? ms : ms + 1000LL);
    return std::string(out);
}

#define MPP_ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))

// ???????mpi_enc_test ??H.264 ???mdinfo_size ?????????HEVC ??????????
// (ALIGN(hor_stride,64)>>6) * (ALIGN(ver_stride,16)>>4) * 16
// Old RK3588 estimate (64x64 MB)*32 can mismatch MPP expectation and break encode_put_frame.
static size_t EncMdInfoBytesH264MpiEncTest(int hor_stride, int ver_stride) {
    return static_cast<size_t>(MPP_ALIGN(hor_stride, 64) >> 6) *
           static_cast<size_t>(MPP_ALIGN(ver_stride, 16) >> 4) * 16;
}

static bool AnnexBHasIdrNalu(const uint8_t* p, size_t len) {
    size_t i = 0;
    while (i + 3 < len) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            if (i + 3 < len) {
                uint8_t nal = static_cast<uint8_t>(p[i + 3] & 0x1f);
                if (nal == 5) {
                    return true;
                }
            }
            i += 3;
            continue;
        }
        if (i + 4 < len && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) {
            if (i + 4 < len) {
                uint8_t nal = static_cast<uint8_t>(p[i + 4] & 0x1f);
                if (nal == 5) {
                    return true;
                }
            }
            i += 4;
            continue;
        }
        ++i;
    }
    return false;
}

// MPP stream_type=1 uses 4-byte length-prefixed NAL payload. WebRTC RTP packetizer expects Annex-B.
// Write converted Annex-B into *out and reuse caller-provided vector to avoid per-frame heap alloc.
static bool FillAvcLengthPrefixedToAnnexB(const uint8_t* p, size_t len, std::vector<uint8_t>* out) {
    if (!out || len < 4) {
        return false;
    }
    out->clear();
    out->reserve(len + (len / 64) * 4 + 16);
    size_t o = 0;
    while (o + 4 <= len) {
        uint32_t nsize = (static_cast<uint32_t>(p[o]) << 24) |
                         (static_cast<uint32_t>(p[o + 1]) << 16) |
                         (static_cast<uint32_t>(p[o + 2]) << 8) | static_cast<uint32_t>(p[o + 3]);
        o += 4;
        if (nsize == 0 || o + nsize > len) {
            out->clear();
            return false;
        }
        out->push_back(0);
        out->push_back(0);
        out->push_back(0);
        out->push_back(1);
        out->insert(out->end(), p + o, p + o + nsize);
        o += nsize;
    }
    if (o != len) {
        out->clear();
        return false;
    }
    return !out->empty();
}

static bool FillAvcLengthPrefixed16ToAnnexB(const uint8_t* p, size_t len, std::vector<uint8_t>* out) {
    if (!out || len < 2) {
        return false;
    }
    out->clear();
    out->reserve(len + (len / 32) * 4 + 16);
    size_t o = 0;
    while (o + 2 <= len) {
        uint16_t nsize = (static_cast<uint16_t>(p[o]) << 8) | static_cast<uint16_t>(p[o + 1]);
        o += 2;
        if (nsize == 0 || o + nsize > len) {
            out->clear();
            return false;
        }
        out->push_back(0);
        out->push_back(0);
        out->push_back(0);
        out->push_back(1);
        out->insert(out->end(), p + o, p + o + nsize);
        o += nsize;
    }
    if (o != len) {
        out->clear();
        return false;
    }
    return !out->empty();
}

// ??NAL ????????????????????????????
static bool FillRawSingleNalToAnnexB(const uint8_t* p, size_t len, std::vector<uint8_t>* out) {
    if (!out || len < 1 || (p[0] & 0x80) != 0) {
        return false;
    }
    out->clear();
    out->reserve(4 + len);
    out->push_back(0);
    out->push_back(0);
    out->push_back(0);
    out->push_back(1);
    out->insert(out->end(), p, p + len);
    return true;
}

/// Copy semiplanar NV12/NV21 source (shared chroma stride) into MPP frame buffer.
static void CopySemiPlanarToMppBuffer(const uint8_t* src_y,
                                      const uint8_t* src_uv,
                                      int src_stride_y,
                                      int src_stride_uv,
                                      uint8_t* dst_base,
                                      int hor_stride,
                                      int ver_stride,
                                      int width,
                                      int height) {
    uint8_t* dst_y = dst_base;
    uint8_t* dst_uv = dst_base + static_cast<size_t>(hor_stride) * static_cast<size_t>(ver_stride);
    for (int y = 0; y < height; ++y) {
        memcpy(dst_y + static_cast<size_t>(y) * static_cast<size_t>(hor_stride),
               src_y + static_cast<size_t>(y) * static_cast<size_t>(src_stride_y), static_cast<size_t>(width));
    }
    const int chroma_rows = height / 2;
    for (int y = 0; y < chroma_rows; ++y) {
        memcpy(dst_uv + static_cast<size_t>(y) * static_cast<size_t>(hor_stride),
               src_uv + static_cast<size_t>(y) * static_cast<size_t>(src_stride_uv), static_cast<size_t>(width));
    }
}

static void CopyNv12ToMppBuffer(const webrtc::NV12BufferInterface* nv12,
                                uint8_t* dst_base,
                                int hor_stride,
                                int ver_stride,
                                int width,
                                int height) {
    CopySemiPlanarToMppBuffer(nv12->DataY(), nv12->DataUV(), nv12->StrideY(), nv12->StrideUV(), dst_base, hor_stride,
                              ver_stride, width, height);
}

static int H264ProfileIdForMpp(const webrtc::VideoCodec* c) {
    (void)c;
    // Keep output aligned with negotiated main profile in current signaling config.
    return 77;  // MPP H.264 main profile id
}

static int ReadEnvIntInRange(const char* name, int fallback, int min_v, int max_v) {
    return rflow::common::util::ReadEnvIntInRange(name, fallback, min_v, max_v);
}

static int MppBufferFdOrNeg1(MppBuffer buf) {
    return buf ? mpp_buffer_get_fd(buf) : -1;
}

// Some BSP builds return a reused output packet with pos at end (length==0, size>0).
// Reset read cursor before interpreting payload / EOI.
static size_t MppPacketReadableLength(MppPacket pkt, void** out_pos) {
    if (!pkt) {
        if (out_pos) {
            *out_pos = nullptr;
        }
        return 0;
    }
    size_t len = mpp_packet_get_length(pkt);
    void* pos = mpp_packet_get_pos(pkt);
    if (len == 0) {
        void* data = mpp_packet_get_data(pkt);
        const size_t size = mpp_packet_get_size(pkt);
        if (data && size > 0) {
            mpp_packet_set_pos(pkt, data);
            mpp_packet_set_length(pkt, size);
            len = mpp_packet_get_length(pkt);
            pos = mpp_packet_get_pos(pkt);
        }
    }
    if (out_pos) {
        *out_pos = pos;
    }
    return len;
}

static void ResetMppPacketWriteCursor(MppPacket pkt) {
    if (!pkt) {
        return;
    }
    void* data = mpp_packet_get_data(pkt);
    if (data) {
        mpp_packet_set_pos(pkt, data);
    }
    mpp_packet_set_length(pkt, 0);
}

// MJPEG 解码 output buffer 零拷贝绑定：direct inc_ref 或 misc/external import。
struct ImportDecBufferResult {
    MppBuffer buffer{nullptr};
    MPP_RET import_ret{MPP_NOK};
    MppBufferInfo info{};
};

/// RAII：Encode 任意出口自动 mpp_buffer_put 零拷贝输入缓冲（direct inc_ref / import），避免 fd 泄漏。
struct HeldMppInputGuard {
    MppBuffer* held{nullptr};
    void release() {
        if (held && *held) {
            mpp_buffer_put(*held);
            *held = nullptr;
        }
    }
    ~HeldMppInputGuard() { release(); }
};

static ImportDecBufferResult TryImportDecBuffer(MppBufferGroup import_grp, MppBuffer dec_buf) {
    ImportDecBufferResult result;
    if (!dec_buf) {
        result.import_ret = MPP_ERR_NULL_PTR;
        return result;
    }
    if (mpp_buffer_info_get(dec_buf, &result.info) != MPP_OK) {
        result.info.type = MPP_BUFFER_TYPE_DRM;
        result.info.fd = mpp_buffer_get_fd(dec_buf);
        result.info.size = mpp_buffer_get_size(dec_buf);
        result.info.ptr = mpp_buffer_get_ptr(dec_buf);
        result.info.hnd = nullptr;
        result.info.index = -1;
    } else {
        result.info.type = static_cast<MppBufferType>(static_cast<RK_U32>(result.info.type) &
                                                      MPP_BUFFER_TYPE_MASK);
    }
    if (result.info.fd < 0 && !result.info.ptr) {
        result.import_ret = MPP_ERR_NULL_PTR;
        return result;
    }
    DmabufSyncStartRead(result.info.fd);
    result.import_ret =
        mpp_buffer_import_with_tag(import_grp, &result.info, &result.buffer, MODULE_TAG, __func__);
    if (result.import_ret != MPP_OK) {
        result.buffer = nullptr;
    }
    return result;
}

static void LogImportDecBufferFailDiag(bool enabled,
                                       int attempt,
                                       const ImportDecBufferResult& result,
                                       MppBuffer dec_buf) {
    if (!enabled) {
        return;
    }
    RFLOW_LOG_TAG_W("RkMppH264Diag",
                    "import_fail attempt=%d import_ret=%d info_type=%u dec_fd=%d dec_size=%zu info_size=%zu",
                    attempt, static_cast<int>(result.import_ret), static_cast<unsigned>(result.info.type),
                    MppBufferFdOrNeg1(dec_buf),
                    dec_buf ? static_cast<size_t>(mpp_buffer_get_size(dec_buf)) : 0u,
                    static_cast<size_t>(result.info.size));
}

static MppBuffer ImportDecBufferWithRetry(MppBufferGroup import_grp,
                                          MppBuffer dec_buf,
                                          int retry_sleep_us,
                                          bool diag_enabled) {
    ImportDecBufferResult first = TryImportDecBuffer(import_grp, dec_buf);
    if (first.buffer) {
        return first.buffer;
    }
    LogImportDecBufferFailDiag(diag_enabled, 1, first, dec_buf);
    if (retry_sleep_us <= 0) {
        return nullptr;
    }
    usleep(static_cast<unsigned>(retry_sleep_us));
    ImportDecBufferResult second = TryImportDecBuffer(import_grp, dec_buf);
    if (!second.buffer) {
        LogImportDecBufferFailDiag(diag_enabled, 2, second, dec_buf);
    }
    return second.buffer;
}

static int DrainPendingEncoderOutput(MppApi* mpi, MppCtx ctx, int64_t restore_output_timeout_ms, int max_packets) {
    if (!mpi || !ctx || max_packets <= 0) {
        return 0;
    }
    MppPollType non_block = MPP_POLL_NON_BLOCK;
    (void)mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &non_block);
    int drained = 0;
    for (int i = 0; i < max_packets; ++i) {
        MppPacket pkt = nullptr;
        const MPP_RET gr = mpi->encode_get_packet(ctx, &pkt);
        if (pkt) {
            mpp_packet_deinit(&pkt);
            ++drained;
            continue;
        }
        if (gr == MPP_ERR_TIMEOUT || gr == MPP_NOK) {
            break;
        }
        break;
    }
    RK_S64 restore_ms = restore_output_timeout_ms;
    (void)mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &restore_ms);
    return drained;
}

static void LogPutFrameFailureDiag(bool enabled,
                                   const char* phase,
                                   int ret,
                                   bool used_native_zero_copy,
                                   bool native_zero_copy_enabled,
                                   MppBuffer input_buf,
                                   MppBuffer frm_buf,
                                   int width,
                                   int height,
                                   int enc_hor_stride,
                                   int enc_ver_stride,
                                   const MppNativeDecFrameBuffer* native,
                                   int failure_streak,
                                   unsigned recover_attempts) {
    if (!enabled) {
        return;
    }
    const bool ext_input = input_buf && frm_buf && input_buf != frm_buf;
    RFLOW_LOG_TAG_E(
        "RkMppH264Diag",
        "put_frame_fail phase=%s ret=%d zero_copy=%d zero_copy_enabled=%d ext_input=%d "
        "input_fd=%d frm_fd=%d enc=%dx%d stride=%dx%d native=%dx%d fmt=%u streak=%d recover_attempts=%u",
        phase, ret, used_native_zero_copy ? 1 : 0, native_zero_copy_enabled ? 1 : 0, ext_input ? 1 : 0,
        MppBufferFdOrNeg1(input_buf), MppBufferFdOrNeg1(frm_buf), width, height, enc_hor_stride, enc_ver_stride,
        native ? native->width() : 0, native ? native->height() : 0,
        native ? static_cast<unsigned>(native->mpp_fmt()) : 0u, failure_streak, recover_attempts);
}

static void LogEmptyEoiPacketDiag(bool enabled,
                                  MppPacket out_pkt,
                                  uint32_t frame_rtp_ts,
                                  uint16_t tracking_id,
                                  bool split_by_byte_enabled,
                                  int empty_eoi_retry,
                                  int empty_eoi_retry_max,
                                  int get_packet_safety,
                                  size_t assembly_bytes) {
    if (!enabled || !out_pkt) {
        return;
    }
    RFLOW_LOG_TAG_W(
        "RkMppH264Diag",
        "empty_eoi pkt_len=%zu pkt_size=%zu is_part=%u is_soi=%u is_eoi=%u pts=%lld dts=%lld flag=0x%x "
        "has_meta=%d rtp_ts=%u tracking_id=%u split=%d assembly_bytes=%zu retry=%d/%d safety=%d",
        mpp_packet_get_length(out_pkt), mpp_packet_get_size(out_pkt),
        static_cast<unsigned>(mpp_packet_is_partition(out_pkt)),
        static_cast<unsigned>(mpp_packet_is_soi(out_pkt)), static_cast<unsigned>(mpp_packet_is_eoi(out_pkt)),
        static_cast<long long>(mpp_packet_get_pts(out_pkt)), static_cast<long long>(mpp_packet_get_dts(out_pkt)),
        static_cast<unsigned>(mpp_packet_get_flag(out_pkt)), mpp_packet_has_meta(out_pkt) ? 1 : 0, frame_rtp_ts,
        static_cast<unsigned>(tracking_id), split_by_byte_enabled ? 1 : 0, assembly_bytes, empty_eoi_retry,
        empty_eoi_retry_max, get_packet_safety);
}

void MaybeShrinkScratchBuffer(std::vector<uint8_t>* buf,
                              size_t max_capacity,
                              size_t max_live_size,
                              uint16_t periodic_tick) {
    if (!buf || buf->capacity() <= max_capacity) {
        return;
    }
    if ((periodic_tick % 300u) != 0u) {
        return;
    }
    if (buf->size() > max_live_size) {
        return;
    }
    std::vector<uint8_t> compact;
    if (!buf->empty()) {
        compact.assign(buf->begin(), buf->end());
    }
    buf->swap(compact);
}

/// MPP JPEG 解码 NV12 默认 16 像素 hor 对齐；编码 prep 需与之匹配才能零拷贝。
/// 默认 16（与 decoder / runtime_knobs 一致）；特殊 BSP 可设 RFLOW_MPP_ENC_HOR_STRIDE_ALIGN=64。
static int MppEncHorStrideAlignPixels() {
    const char* e = std::getenv("RFLOW_MPP_ENC_HOR_STRIDE_ALIGN");
    if (!e || !e[0]) {
        return 16;
    }
    const int v = std::atoi(e);
    if (v == 16 || v == 32 || v == 64) {
        return v;
    }
    return 16;
}

/// 与 RkMppMjpegDecoder::ComputeJpegOutputBufSize 一致：MJPEG 解码 output DRM buffer 按此容量分配。
static size_t MjpegDecOutputBufSizeBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const RK_U32 hor = MPP_ALIGN(static_cast<RK_U32>(width), 16);
    const RK_U32 ver = MPP_ALIGN(static_cast<RK_U32>(height), 16);
    const RK_U32 plane0 = hor * ver;
    const RK_U32 nv12 = plane0 + plane0 / 2;
    return static_cast<size_t>(std::max(nv12, plane0 * 2u));
}

void FillMppEncRcFields(MppEncCfg cfg, int target_bps, int min_bps, int max_bps, uint32_t fps) {
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", target_bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", min_bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", max_bps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", static_cast<RK_S32>(fps));
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", static_cast<RK_S32>(fps));
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
}

}  // namespace

RkMppH264Encoder::RkMppH264Encoder(const webrtc::Environment& env,
                                     webrtc::H264EncoderSettings settings,
                                     bool rockchip_mpp_rc_cbr)
    : env_(env), h264_settings_(settings), rockchip_mpp_rc_cbr_(rockchip_mpp_rc_cbr) {}

RkMppH264Encoder::~RkMppH264Encoder() {
    Release();
}

void RkMppH264Encoder::SetFecControllerOverride(webrtc::FecControllerOverride* o) {
    (void)o;
}

void RkMppH264Encoder::DestroyMpp() {
    if (initialized_ && mpi_ && mpp_ctx_) {
        (void)DrainPendingEncoderOutput(reinterpret_cast<MppApi*>(mpi_),
                                       reinterpret_cast<MppCtx>(mpp_ctx_), output_timeout_ms_,
                                       put_frame_drain_max_);
    }
    initialized_ = false;
    last_forced_idr_ctrl_us_ = -1;
    last_idr_emit_us_ = -1;
    consecutive_output_failures_ = 0;
    if (frm_buf_) {
        mpp_buffer_put(reinterpret_cast<MppBuffer>(frm_buf_));
        frm_buf_ = nullptr;
    }
    if (pkt_buf_) {
        mpp_buffer_put(reinterpret_cast<MppBuffer>(pkt_buf_));
        pkt_buf_ = nullptr;
    }
    if (md_buf_) {
        mpp_buffer_put(reinterpret_cast<MppBuffer>(md_buf_));
        md_buf_ = nullptr;
    }
    if (import_buf_grp_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(import_buf_grp_));
        import_buf_grp_ = nullptr;
    }
    if (buf_grp_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(buf_grp_));
        buf_grp_ = nullptr;
    }
    if (mpp_cfg_) {
        mpp_enc_cfg_deinit(reinterpret_cast<MppEncCfg>(mpp_cfg_));
        mpp_cfg_ = nullptr;
    }
    if (mpp_ctx_) {
        mpp_destroy(reinterpret_cast<MppCtx>(mpp_ctx_));
        mpp_ctx_ = nullptr;
        mpi_ = nullptr;
    }
    annex_scratch_.clear();
    std::vector<uint8_t>().swap(annex_scratch_);
    split_assembly_buf_.clear();
    std::vector<uint8_t>().swap(split_assembly_buf_);
    cached_extra_info_annexb_.clear();
    std::vector<uint8_t>().swap(cached_extra_info_annexb_);
}

bool RkMppH264Encoder::ApplyRcToCfg() {
    if (!mpp_cfg_ || !mpi_) {
        return false;
    }
    MppEncCfg cfg = reinterpret_cast<MppEncCfg>(mpp_cfg_);
    MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
    MppApi* mpi = reinterpret_cast<MppApi*>(mpi_);
    FillMppEncRcFields(cfg, target_bps_, min_bps_, max_bps_, fps_);
    return mpi->control(ctx, MPP_ENC_SET_CFG, cfg) == MPP_OK;
}

int RkMppH264Encoder::MppH264LevelForSize(int width, int height, uint32_t fps) {
    // H.264 Level: 720p@>30 and 1080p@>30 require higher level.
    if (width * height >= 1920 * 1080) {
        return fps > 30u ? 42 : 41;
    }
    if (width * height >= 1280 * 720) {
        return fps > 30u ? 42 : 40;
    }
    return 31;
}

int RkMppH264Encoder::InitEncode(const webrtc::VideoCodec* inst,
                                 const webrtc::VideoEncoder::Settings& settings) {
    std::lock_guard<std::mutex> lock(mpp_mu_);
    (void)settings;
    if (!inst || inst->codecType != webrtc::kVideoCodecH264) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    if (inst->numberOfSimulcastStreams > 1) {
        return WEBRTC_VIDEO_CODEC_ERR_SIMULCAST_PARAMETERS_NOT_SUPPORTED;
    }
    if (inst->width < 2 || inst->height < 2 || inst->maxFramerate == 0) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    ConfigureFromVideoCodecLocked(inst);
    cached_codec_inst_ = *inst;
    mpp_recover_attempts_ = 0;
    last_mpp_recover_us_ = 0;
    return InitMppHardwareLocked(inst);
}

void RkMppH264Encoder::ConfigureFromVideoCodecLocked(const webrtc::VideoCodec* inst) {
    width_ = static_cast<int>(inst->width);
    height_ = static_cast<int>(inst->height);
    hor_stride_ = MPP_ALIGN(width_, MppEncHorStrideAlignPixels());
    ver_stride_ = MPP_ALIGN(height_, 16);
    fps_ = inst->maxFramerate > 0 ? inst->maxFramerate : 30u;

    target_bps_ = static_cast<int>(inst->startBitrate) * 1000;
    min_bps_ = static_cast<int>(inst->minBitrate) * 1000;
    max_bps_ = static_cast<int>(inst->maxBitrate) * 1000;
    if (target_bps_ <= 0) {
        target_bps_ = 2'000'000;
    }
    if (min_bps_ <= 0) {
        min_bps_ = target_bps_ / 2;
    }
    if (max_bps_ <= 0) {
        max_bps_ = target_bps_ * 2;
    }
    if (min_bps_ > max_bps_) {
        std::swap(min_bps_, max_bps_);
    }

    int ki = inst->H264().keyFrameInterval;
    if (ki <= 0) {
        ki = static_cast<int>(fps_) * 2;
    }
    gop_ = ki;
    RefreshImportPoolLimitCountLocked();
    if (const char* lt = std::getenv("RFLOW_LATENCY_TRACE"); lt && lt[0] == '1') {
        RFLOW_LOG_TAG_I("Latency", "MPP H264 GOP frames=%d fps=%u", gop_, static_cast<unsigned>(fps_));
    }
    mpp_rc_mode_ = rockchip_mpp_rc_cbr_ ? MPP_ENC_RC_MODE_CBR : MPP_ENC_RC_MODE_VBR;
    intra_refresh_mode_ = ReadEnvIntInRange("RFLOW_MPP_ENC_INTRA_REFRESH_MODE", 0, 0, 3);
    const int mb_rows = std::max(1, (height_ + 15) / 16);
    const int fps_i = std::max(1, static_cast<int>(fps_));
    const int auto_refresh_arg = std::max(1, (mb_rows + fps_i - 1) / fps_i);
    intra_refresh_arg_ = ReadEnvIntInRange("RFLOW_MPP_ENC_INTRA_REFRESH_ARG", auto_refresh_arg, 1, 512);
    split_bytes_ = ReadEnvIntInRange("RFLOW_MPP_ENC_SPLIT_BYTES", 0, 0, 4096);
    split_by_byte_enabled_ = split_bytes_ > 0;
    idr_min_interval_ms_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IDR_MIN_INTERVAL_MS", 800, 0, 5000);
    idr_loss_quick_trigger_ms_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IDR_LOSS_QUICK_MS", 180, 0, 2000);
    idr_force_max_wait_ms_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IDR_FORCE_MAX_WAIT_MS", 3000, 200, 15000);
    last_forced_idr_ctrl_us_ = -1;
    last_idr_emit_us_ = -1;
    recover_soft_fail_threshold_ = 6;
    recover_hard_fail_threshold_ = 30;
    recover_disable_split_on_failure_ = true;
    debug_enabled_ = ReadEnvIntInRange("RFLOW_MPP_ENC_DEBUG", 0, 0, 1) == 1;
    latency_trace_enabled_ = ReadEnvIntInRange("RFLOW_LATENCY_TRACE", 0, 0, 1) == 1;
    e2e_trace_enabled_ = ReadEnvIntInRange("RFLOW_E2E_LATENCY_TRACE", 0, 0, 1) == 1;
    mjpeg_to_h264_trace_enabled_ = ReadEnvIntInRange("RFLOW_MJPEG_TO_H264_TRACE", 0, 0, 1) == 1;
    use_sync_encode_ = ReadEnvIntInRange("RFLOW_MPP_ENC_USE_SYNC", 0, 0, 1) == 1;
    use_task_encode_ = ReadEnvIntInRange("RFLOW_MPP_ENC_USE_TASK", 0, 0, 1) == 1;
    if (use_task_encode_) {
        RTC_LOG(LS_WARNING) << "[RkMppH264] task encode path is temporarily disabled; falling back to sync/non-task";
        use_task_encode_ = false;
        use_sync_encode_ = true;
    }
    task_read_packet_ = ReadEnvIntInRange("RFLOW_MPP_ENC_TASK_READ_PACKET", 1, 0, 1) == 1;
    native_zero_copy_enabled_ = ReadEnvIntInRange("RFLOW_MPP_ENC_NATIVE_ZERO_COPY", 1, 0, 1) == 1;
    native_zero_copy_strict_ = ReadEnvIntInRange("RFLOW_MPP_ENC_NATIVE_ZERO_COPY_STRICT", 0, 0, 1) == 1;
    native_zero_copy_fail_disable_threshold_ =
        ReadEnvIntInRange("RFLOW_MPP_ENC_NATIVE_ZERO_COPY_FAILS", 3, 1, 50);
    trace_every_n_ =
        static_cast<unsigned>(ReadEnvIntInRange("RFLOW_MPP_ENC_TRACE_EVERY_N", 120, 1, 600));
    simulate_put_frame_fail_remaining_ =
        ReadEnvIntInRange("RFLOW_MPP_ENC_SIMULATE_PUT_FRAME_FAIL_COUNT", 0, 0, 100000);
    put_frame_diag_enabled_ = ReadEnvIntInRange("RFLOW_MPP_ENC_PUT_FRAME_DIAG", 1, 0, 1) == 1;
    empty_eoi_retry_max_ = ReadEnvIntInRange("RFLOW_MPP_ENC_EMPTY_EOI_RETRIES", 6, 0, 32);
    empty_pkt_retry_max_ = ReadEnvIntInRange("RFLOW_MPP_ENC_EMPTY_PKT_RETRIES", 6, 0, 32);
    input_timeout_ms_ = ReadEnvIntInRange("RFLOW_MPP_ENC_INPUT_TIMEOUT_MS", 50, -1, 8000);
    output_timeout_ms_ = ReadEnvIntInRange("RFLOW_MPP_ENC_OUTPUT_TIMEOUT_MS", 4000, -1, 8000);
    put_frame_drain_max_ = ReadEnvIntInRange("RFLOW_MPP_ENC_PUT_FRAME_DRAIN_MAX", 16, 1, 128);
    import_retry_sleep_us_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_RETRY_US", 500, 0, 5000);
    import_grp_reset_threshold_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_RESET_FAILS", 3, 1, 100);
    import_pause_threshold_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_PAUSE_FAILS", 30, 3, 1000);
    import_pause_probe_interval_ = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_PAUSE_PROBE_FRAMES", 120, 30, 6000);
    import_reset_loop_pause_threshold_ =
        ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_RESET_LOOP_PAUSE", 5, 2, 100);
    {
        const char* bind = std::getenv("RFLOW_MPP_ENC_ZERO_COPY_BIND");
        zero_copy_bind_mode_ = 0;
        if (bind && bind[0]) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(bind[0])));
            if (c == 'e') {
                zero_copy_bind_mode_ = 2;
            } else if (c == 'n') {
                zero_copy_bind_mode_ = 1;
            }
        }
    }
}

int RkMppH264Encoder::InitMppHardwareLocked(const webrtc::VideoCodec* inst) {
    DestroyMpp();
    native_zero_copy_failures_ = 0;
    native_zero_copy_frames_ = 0;
    native_copy_fallback_frames_ = 0;
    import_consecutive_failures_ = 0;
    import_pause_zero_copy_ = false;
    import_pause_probe_frames_ = 0;
    import_grp_reset_count_ = 0;
    import_pool_limit_applied_ = false;
    consecutive_output_failures_ = 0;

    MppCtx ctx = nullptr;
    MppApi* mpi = nullptr;
    if (mpp_create(&ctx, &mpi) != MPP_OK || !ctx || !mpi) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] mpp_create failed";
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    mpp_ctx_ = ctx;
    mpi_ = mpi;

    if (mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] mpp_init(ENC, AVC) failed";
        DestroyMpp();
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    // Same as mpi_enc_test: non-split mode should finish with one get_packet per frame.
    // Timeout must be set after mpp_init on some platforms.
    RK_S64 input_timeout_ms = input_timeout_ms_;
    RK_S64 output_timeout_ms = output_timeout_ms_;
    const MPP_RET set_in_to_ret = mpi->control(ctx, MPP_SET_INPUT_TIMEOUT, &input_timeout_ms);
    if (set_in_to_ret != MPP_OK) {
        RTC_LOG(LS_WARNING) << "[RkMppH264] MPP_SET_INPUT_TIMEOUT failed ret=" << set_in_to_ret
                            << " val_ms=" << input_timeout_ms;
    } else if (debug_enabled_) {
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "MPP_SET_INPUT_TIMEOUT ok val_ms=%lld",
                        static_cast<long long>(input_timeout_ms));
    }
    const MPP_RET set_out_to_ret = mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &output_timeout_ms);
    if (set_out_to_ret != MPP_OK) {
        RTC_LOG(LS_WARNING) << "[RkMppH264] MPP_SET_OUTPUT_TIMEOUT failed ret=" << set_out_to_ret
                            << " val_ms=" << output_timeout_ms;
    } else if (debug_enabled_) {
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "MPP_SET_OUTPUT_TIMEOUT ok val_ms=%lld",
                        static_cast<long long>(output_timeout_ms));
    }

    MppEncCfg cfg = nullptr;
    if (mpp_enc_cfg_init(&cfg) != MPP_OK || !cfg) {
        DestroyMpp();
        return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    mpp_cfg_ = cfg;

    if (mpi->control(ctx, MPP_ENC_GET_CFG, cfg) != MPP_OK) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] MPP_ENC_GET_CFG failed";
        DestroyMpp();
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "prep:width", width_);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height_);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride_);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride_);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(cfg, "prep:range", MPP_FRAME_RANGE_JPEG);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", mpp_rc_mode_);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop_);
    mpp_enc_cfg_set_u32(cfg, "rc:max_reenc_times", 0);
    mpp_enc_cfg_set_u32(cfg, "rc:super_mode", 0);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_thd", 20);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_gap", 1);

    // Fill cfg only here; avoid premature MPP_ENC_SET_CFG before h264 fields are ready.
    FillMppEncRcFields(cfg, target_bps_, min_bps_, max_bps_, fps_);
    if (mpp_rc_mode_ == MPP_ENC_RC_MODE_VBR || mpp_rc_mode_ == MPP_ENC_RC_MODE_AVBR) {
        mpp_enc_cfg_set_s32(cfg, "rc:qp_init", -1);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 51);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 10);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", 51);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", 10);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 2);
    }

    const int prof = H264ProfileIdForMpp(inst);
    mpp_enc_cfg_set_s32(cfg, "h264:profile", prof);
    mpp_enc_cfg_set_s32(cfg, "h264:level", 40);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
    mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", prof >= 100 ? 1 : 0);
    if (intra_refresh_mode_ != 0) {
        if (mpp_enc_cfg_set_s32(cfg, "h264:intra_refresh_mode", intra_refresh_mode_) != MPP_OK ||
            mpp_enc_cfg_set_s32(cfg, "h264:intra_refresh_arg", intra_refresh_arg_) != MPP_OK) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] failed to apply h264 intra refresh mode="
                                << intra_refresh_mode_ << " arg=" << intra_refresh_arg_;
            intra_refresh_mode_ = 0;
            intra_refresh_arg_ = 0;
        }
    }
    // WebRTC RtpPacketizerH264 expects Annex-B start codes.
    if (mpp_enc_cfg_set_s32(cfg, "h264:stream_type", 0) != MPP_OK) {
        RTC_LOG(LS_WARNING) << "[RkMppH264] h264:stream_type=0 (Annex B) not applied, RTP may fail";
    }

    if (mpi->control(ctx, MPP_ENC_SET_CFG, cfg) != MPP_OK) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] MPP_ENC_SET_CFG failed";
        DestroyMpp();
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (split_by_byte_enabled_) {
        MppEncSliceSplit split_cfg {};
        split_cfg.change = MPP_ENC_SPLIT_CFG_CHANGE_MODE | MPP_ENC_SPLIT_CFG_CHANGE_ARG |
                           MPP_ENC_SPLIT_CFG_CHANGE_OUTPUT;
        split_cfg.split_mode = MPP_ENC_SPLIT_BY_BYTE;
        split_cfg.split_arg = static_cast<RK_U32>(split_bytes_);
        split_cfg.split_out = MPP_ENC_SPLIT_OUT_LOWDELAY;
        if (mpi->control(ctx, MPP_ENC_SET_SPLIT, &split_cfg) != MPP_OK) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] MPP_ENC_SET_SPLIT failed, disable split-by-byte";
            split_by_byte_enabled_ = false;
            split_bytes_ = 0;
        }
    }
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    if (mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode) != MPP_OK) {
        RTC_LOG(LS_WARNING) << "[RkMppH264] MPP_ENC_SET_HEADER_MODE failed";
    }
    {
        MppPacket extra = nullptr;
        if (mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &extra) == MPP_OK && extra) {
            const uint8_t* ep = static_cast<const uint8_t*>(mpp_packet_get_pos(extra));
            const size_t elen = static_cast<size_t>(mpp_packet_get_length(extra));
            if (ep && elen > 0) {
                if (!webrtc::H264::FindNaluIndices(webrtc::ArrayView<const uint8_t>(ep, elen)).empty()) {
                    cached_extra_info_annexb_.assign(ep, ep + elen);
                } else if (!FillAvcLengthPrefixedToAnnexB(ep, elen, &cached_extra_info_annexb_)) {
                    if (!FillAvcLengthPrefixed16ToAnnexB(ep, elen, &cached_extra_info_annexb_)) {
                        cached_extra_info_annexb_.clear();
                    }
                }
            }
            if (debug_enabled_) {
                RFLOW_LOG_TAG_I("RkMppH264Dbg", "extra_info len=%zu cached_annexb=%zu",
                                  static_cast<size_t>(mpp_packet_get_length(extra)), cached_extra_info_annexb_.size());
            }
            mpp_packet_deinit(&extra);
        } else if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "extra_info unavailable");
        }
    }

    // Align with mpp_h264_smoke: allocate by configured stride directly.
    const size_t nv12_size = static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_) * 3 / 2;
    const size_t pkt_size = nv12_size;
    MppBuffer fb = nullptr;
    MppBuffer pb = nullptr;
    MppBuffer mb = nullptr;
    MppBufferGroup grp = nullptr;
    static const MppBufferType kBufTypes[] = {
        MPP_BUFFER_TYPE_DRM,
        static_cast<MppBufferType>(MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE),
        MPP_BUFFER_TYPE_DMA_HEAP,
        MPP_BUFFER_TYPE_ION,
        MPP_BUFFER_TYPE_NORMAL,
    };
    static const MppBufferType kBufTypesNormalOnly[] = {
        MPP_BUFFER_TYPE_NORMAL,
    };
    const bool force_normal_buf = false;
    const MppBufferType* try_buf_types = force_normal_buf ? kBufTypesNormalOnly : kBufTypes;
    const size_t try_buf_type_count = force_normal_buf ? 1 : (sizeof(kBufTypes) / sizeof(kBufTypes[0]));
    MppBufferType chosen_buf_type = MPP_BUFFER_TYPE_NORMAL;
    bool buffers_ok = false;
    for (size_t i = 0; i < try_buf_type_count; ++i) {
        MppBufferType buf_type = try_buf_types[i];
        grp = nullptr;
        fb = pb = nullptr;
        MPP_RET grp_ret = MPP_NOK;
        if (buf_type == MPP_BUFFER_TYPE_DRM) {
            grp_ret = mpp_buffer_group_get_internal(&grp, MPP_BUFFER_TYPE_DRM);
        } else {
            grp_ret = mpp_buffer_group_get(&grp, buf_type, MPP_BUFFER_INTERNAL, MODULE_TAG, __func__);
        }
        if (grp_ret != MPP_OK || !grp) {
            continue;
        }
        if (mpp_buffer_get(grp, &fb, nv12_size) == MPP_OK &&
            mpp_buffer_get(grp, &pb, pkt_size) == MPP_OK) {
            buffers_ok = true;
            chosen_buf_type = buf_type;
            break;
        }
        if (fb) {
            mpp_buffer_put(fb);
            fb = nullptr;
        }
        if (pb) {
            mpp_buffer_put(pb);
            pb = nullptr;
        }
        mpp_buffer_group_put(grp);
        grp = nullptr;
    }
    if (!buffers_ok || !grp) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] mpp buffer alloc failed (tried drm/drm+cache/dma_heap/ion/normal)";
        DestroyMpp();
        return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    buf_grp_ = grp;
    frm_buf_ = fb;
    pkt_buf_ = pb;

    MppBufferGroup import_grp = nullptr;
    if (zero_copy_bind_mode_ == 2) {
        if (mpp_buffer_group_get_external(&import_grp, MPP_BUFFER_TYPE_DRM) == MPP_OK && import_grp) {
            import_buf_grp_ = import_grp;
            (void)ApplyImportBufferPoolLimitLocked(import_buf_grp_);
        } else {
            RTC_LOG(LS_WARNING) << "[RkMppH264] external import buffer group failed, native zero-copy unavailable";
            RFLOW_LOG_TAG_W("RkMppH264Warn", "external import buffer group failed");
        }
    } else {
        const char* bind_mode = zero_copy_bind_mode_ == 0 ? "direct" : "null";
        RFLOW_LOG_TAG_I("RkMppH264Dbg",
                        "zero-copy bind=%s (skip EXTERNAL import group; dec inc_ref or misc import)",
                        bind_mode);
    }

    if (debug_enabled_) {
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "buffer type=%d frm_fd=%d pkt_fd=%d import_grp=%p nv12_size=%zu pkt_size=%zu",
                        static_cast<int>(chosen_buf_type),
                        mpp_buffer_get_fd(reinterpret_cast<MppBuffer>(frm_buf_)),
                        mpp_buffer_get_fd(reinterpret_cast<MppBuffer>(pkt_buf_)), import_buf_grp_, nv12_size,
                        pkt_size);
    }

    const size_t md_sz = EncMdInfoBytesH264MpiEncTest(hor_stride_, ver_stride_);
    if (md_sz > 0 && mpp_buffer_get(grp, &mb, md_sz) == MPP_OK) {
        md_buf_ = mb;
    }

    initialized_ = true;
    // Monotonic 16-bit VideoFrameTrackingId carried via RTP header extension.
    // Start from 500 to avoid confusion with VideoFrame::kNotSetId==0.
    next_video_frame_tracking_id_ = 500;
    RTC_LOG(LS_INFO) << "[RkMppH264] InitEncode ok " << width_ << "x" << height_ << "@" << fps_
                     << "fps stride=" << hor_stride_ << "x" << ver_stride_
                     << " bps=" << target_bps_ << " intra_refresh=" << intra_refresh_mode_ << ":"
                     << intra_refresh_arg_ << " split_bytes=" << split_bytes_
                     << " idr_min_interval_ms=" << idr_min_interval_ms_
                     << " idr_loss_quick_ms=" << idr_loss_quick_trigger_ms_
                     << " idr_force_max_wait_ms=" << idr_force_max_wait_ms_;
    return WEBRTC_VIDEO_CODEC_OK;
}

void RkMppH264Encoder::RefreshImportPoolLimitCountLocked() {
    const int explicit_count = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_POOL_COUNT", 0, 0, 64);
    if (explicit_count > 0) {
        import_pool_limit_count_ = explicit_count;
        return;
    }

    auto env_or_preset = [](const char* name, int preset, int min_v, int max_v) -> int {
        const char* e = std::getenv(name);
        if (e && e[0]) {
            return rflow::common::util::ReadEnvIntInRange(name, preset, min_v, max_v);
        }
        return preset;
    };

    const bool high_fps = fps_ >= 45;
    int v4l2 = high_fps ? 5 : 2;
    int mjpeg_q = high_fps ? 4 : 2;
    v4l2 = env_or_preset("RFLOW_V4L2_BUFFER_COUNT", v4l2, 2, 32);
    mjpeg_q = env_or_preset("RFLOW_MJPEG_QUEUE_MAX", mjpeg_q, 1, 32);
    const int dec_slack = ReadEnvIntInRange("RFLOW_MJPEG_DEC_OUT_POOL_SLACK", 2, 0, 8);
    const int enc_slack = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_POOL_SLACK", 2, 0, 16);
    import_pool_limit_count_ = v4l2 + mjpeg_q + dec_slack + enc_slack;
    if (import_pool_limit_count_ < 4) {
        import_pool_limit_count_ = 4;
    }
}

bool RkMppH264Encoder::ApplyImportBufferPoolLimitLocked(void* import_grp_handle) {
    if (!import_grp_handle || width_ <= 0 || height_ <= 0) {
        return false;
    }
    const size_t dec_buf_sz = MjpegDecOutputBufSizeBytes(width_, height_);
    const size_t enc_nv12_sz =
        (hor_stride_ > 0 && ver_stride_ > 0)
            ? static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_) * 3 / 2
            : 0u;
    RefreshImportPoolLimitCountLocked();

    const bool use_limit = ReadEnvIntInRange("RFLOW_MPP_ENC_IMPORT_POOL_LIMIT", 0, 0, 1) == 1;
    if (!use_limit) {
        import_pool_limit_applied_ = false;
        RFLOW_LOG_TAG_I("RkMppH264Dbg",
                        "EXTERNAL import group normal mode (no limit_config; budget_count=%d dec_buf=%zu enc_nv12=%zu)",
                        import_pool_limit_count_, dec_buf_sz, enc_nv12_sz);
        return true;
    }

    if (import_pool_limit_count_ <= 0) {
        return false;
    }
    const size_t sz = dec_buf_sz > 0 ? dec_buf_sz : enc_nv12_sz;
    if (sz == 0) {
        return false;
    }
    MppBufferGroup grp = reinterpret_cast<MppBufferGroup>(import_grp_handle);
    const MPP_RET ret =
        mpp_buffer_group_limit_config(grp, sz, static_cast<RK_S32>(import_pool_limit_count_));
    if (ret == MPP_OK) {
        import_pool_limit_applied_ = true;
        RFLOW_LOG_TAG_I("RkMppH264Dbg",
                        "import pool limit count=%d size=%zu (dec_buf=%zu enc_nv12=%zu)",
                        import_pool_limit_count_, sz, dec_buf_sz, enc_nv12_sz);
        return true;
    }
    import_pool_limit_applied_ = false;
    RFLOW_LOG_TAG_W("RkMppH264Warn",
                    "import pool limit_config failed count=%d size=%zu (dec_buf=%zu enc_nv12=%zu) ret=%d",
                    import_pool_limit_count_, sz, dec_buf_sz, enc_nv12_sz, static_cast<int>(ret));
    return false;
}

bool RkMppH264Encoder::ResetImportBufferGroupLocked(const char* reason) {
    if (initialized_ && mpi_ && mpp_ctx_) {
        (void)DrainPendingEncoderOutput(reinterpret_cast<MppApi*>(mpi_),
                                       reinterpret_cast<MppCtx>(mpp_ctx_), output_timeout_ms_,
                                       put_frame_drain_max_);
    }
    if (import_buf_grp_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(import_buf_grp_));
        import_buf_grp_ = nullptr;
    }
    MppBufferGroup import_grp = nullptr;
    const MPP_RET gr = mpp_buffer_group_get_external(&import_grp, MPP_BUFFER_TYPE_DRM);
    if (gr != MPP_OK || !import_grp) {
        RFLOW_LOG_TAG_W("RkMppH264Warn", "import group reset failed reason=%s ret=%d",
                        reason ? reason : "?", static_cast<int>(gr));
        return false;
    }
    import_buf_grp_ = import_grp;
    import_pool_limit_applied_ = false;
    (void)ApplyImportBufferPoolLimitLocked(import_buf_grp_);
    import_consecutive_failures_ = 0;
    import_pause_zero_copy_ = false;
    import_pause_probe_frames_ = 0;
    ++import_grp_reset_count_;
    RFLOW_LOG_TAG_W("RkMppH264Warn", "import group reset ok reason=%s reset_count=%u",
                    reason ? reason : "?", import_grp_reset_count_);
    return true;
}

void* RkMppH264Encoder::BindDecBufferForEncodeLocked(void* dec_buf_handle) {
    auto* dec_buf = reinterpret_cast<MppBuffer>(dec_buf_handle);
    if (!native_zero_copy_enabled_ || !dec_buf) {
        return nullptr;
    }

    if (zero_copy_bind_mode_ == 0) {
        mpp_buffer_inc_ref(dec_buf);
        import_consecutive_failures_ = 0;
        import_pause_zero_copy_ = false;
        import_pause_probe_frames_ = 0;
        return dec_buf;
    }

    if (zero_copy_bind_mode_ == 1) {
        if (import_pause_zero_copy_) {
            ++import_pause_probe_frames_;
            if (import_pause_probe_frames_ < import_pause_probe_interval_) {
                return nullptr;
            }
            import_pause_probe_frames_ = 0;
            import_consecutive_failures_ = 0;
        }
        const bool log_import_fail =
            put_frame_diag_enabled_ &&
            (import_consecutive_failures_ == 0 ||
             import_consecutive_failures_ + 1 >= import_grp_reset_threshold_ ||
             ((import_consecutive_failures_ + 1) % trace_every_n_) == 0u);
        MppBuffer imported = ImportDecBufferWithRetry(nullptr, dec_buf, import_retry_sleep_us_, log_import_fail);
        if (imported) {
            import_consecutive_failures_ = 0;
            import_pause_zero_copy_ = false;
            import_pause_probe_frames_ = 0;
            return imported;
        }
        ++import_consecutive_failures_;
        if (import_consecutive_failures_ >= import_pause_threshold_ && !import_pause_zero_copy_) {
            import_pause_zero_copy_ = true;
            import_pause_probe_frames_ = 0;
            RFLOW_LOG_TAG_W("RkMppH264Warn",
                            "pause zero-copy (null import) after %d consecutive failures",
                            import_consecutive_failures_);
        }
        return nullptr;
    }

    return ExternalGroupImportDecBufferForEncodeLocked(dec_buf_handle);
}

void* RkMppH264Encoder::ExternalGroupImportDecBufferForEncodeLocked(void* dec_buf_handle) {
    auto* dec_buf = reinterpret_cast<MppBuffer>(dec_buf_handle);
    if (!native_zero_copy_enabled_ || !dec_buf) {
        return nullptr;
    }

    if (import_pause_zero_copy_) {
        ++import_pause_probe_frames_;
        if (import_pause_probe_frames_ < import_pause_probe_interval_) {
            return nullptr;
        }
        import_pause_probe_frames_ = 0;
        if (!ResetImportBufferGroupLocked("pause_probe")) {
            return nullptr;
        }
    }

    MppBufferGroup import_grp = reinterpret_cast<MppBufferGroup>(import_buf_grp_);
    if (!import_grp) {
        return nullptr;
    }

    const bool log_import_fail =
        put_frame_diag_enabled_ &&
        (import_consecutive_failures_ == 0 ||
         import_consecutive_failures_ + 1 >= import_grp_reset_threshold_ ||
         ((import_consecutive_failures_ + 1) % trace_every_n_) == 0u);

    auto try_import = [&](MppBufferGroup grp, bool diag) -> MppBuffer {
        if (!grp) {
            return nullptr;
        }
        return ImportDecBufferWithRetry(grp, dec_buf, import_retry_sleep_us_, diag);
    };

    MppBuffer imported = try_import(import_grp, log_import_fail);
    if (imported) {
        import_consecutive_failures_ = 0;
        import_pause_zero_copy_ = false;
        import_pause_probe_frames_ = 0;
        return imported;
    }

    ++import_consecutive_failures_;
    const int dec_fd = MppBufferFdOrNeg1(dec_buf);
    if (import_consecutive_failures_ >= import_grp_reset_threshold_) {
        RFLOW_LOG_TAG_W("RkMppH264Warn",
                        "import fail streak=%d dec_fd=%d; resetting EXTERNAL import group",
                        import_consecutive_failures_, dec_fd);
        if (ResetImportBufferGroupLocked("import_fail_streak")) {
            import_grp = reinterpret_cast<MppBufferGroup>(import_buf_grp_);
            imported = try_import(import_grp, false);
            if (imported) {
                return imported;
            }
            if (import_grp_reset_count_ >= static_cast<unsigned>(import_reset_loop_pause_threshold_)) {
                import_pause_zero_copy_ = true;
                import_pause_probe_frames_ = 0;
                import_consecutive_failures_ = 0;
                RFLOW_LOG_TAG_W("RkMppH264Warn",
                                "import reset loop reset_count=%u; pause zero-copy (CPU copy until probe)",
                                import_grp_reset_count_);
                return nullptr;
            }
        }
    }

    if (import_consecutive_failures_ >= import_pause_threshold_ && !import_pause_zero_copy_) {
        import_pause_zero_copy_ = true;
        import_pause_probe_frames_ = 0;
        RFLOW_LOG_TAG_W("RkMppH264Warn",
                        "pause zero-copy import after %d consecutive failures (CPU copy until probe)",
                        import_consecutive_failures_);
    }
    return nullptr;
}

bool RkMppH264Encoder::RecoverMppSessionLocked() {
    if (!cached_codec_inst_.has_value()) {
        return false;
    }
    const int64_t now_us = webrtc::TimeMicros();
    const int cooldown_ms = ReadEnvIntInRange("RFLOW_MPP_ENC_RECOVER_COOLDOWN_MS", 500, 0, 60000);
    if (last_mpp_recover_us_ > 0 && cooldown_ms > 0 &&
        (now_us - last_mpp_recover_us_) < static_cast<int64_t>(cooldown_ms) * 1000) {
        return false;
    }
    const unsigned max_attempts =
        static_cast<unsigned>(ReadEnvIntInRange("RFLOW_MPP_ENC_RECOVER_MAX", 10, 1, 1000));
    if (mpp_recover_attempts_ >= max_attempts) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] MPP recover skipped: max attempts reached ("
                          << mpp_recover_attempts_ << "/" << max_attempts << ")";
        return false;
    }
    ++mpp_recover_attempts_;
    last_mpp_recover_us_ = now_us;
    RTC_LOG(LS_WARNING) << "[RkMppH264] attempting MPP session recover attempt="
                        << mpp_recover_attempts_ << "/" << max_attempts;
    RFLOW_LOG_TAG_W("RkMppH264Warn", "MPP session recover attempt %u/%u",
                    mpp_recover_attempts_, max_attempts);
    if (InitMppHardwareLocked(&*cached_codec_inst_) != WEBRTC_VIDEO_CODEC_OK) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] MPP session recover failed";
        RFLOW_LOG_TAG_E("RkMppH264Err", "MPP session recover failed");
        return false;
    }
    RTC_LOG(LS_WARNING) << "[RkMppH264] MPP session recover ok " << width_ << "x" << height_;
    RFLOW_LOG_TAG_I("RkMppH264Dbg", "MPP session recover ok %dx%d", width_, height_);
    return true;
}

int32_t RkMppH264Encoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) {
    std::lock_guard<std::mutex> lock(mpp_mu_);
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RkMppH264Encoder::Release() {
    std::lock_guard<std::mutex> lock(mpp_mu_);
    cached_codec_inst_.reset();
    mpp_recover_attempts_ = 0;
    last_mpp_recover_us_ = 0;
    DestroyMpp();
    return WEBRTC_VIDEO_CODEC_OK;
}

void RkMppH264Encoder::SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) {
    std::lock_guard<std::mutex> lock(mpp_mu_);
    if (!initialized_) {
        return;
    }
    const uint32_t sum = parameters.bitrate.get_sum_bps();
    if (sum > 0) {
        target_bps_ = static_cast<int>(sum);
    }
    if (parameters.framerate_fps > 0.0) {
        fps_ = static_cast<uint32_t>(parameters.framerate_fps + 0.5);
        if (fps_ < 1) {
            fps_ = 1;
        }
    }
    min_bps_ = std::max(10'000, target_bps_ * 3 / 4);
    max_bps_ = std::max(target_bps_, min_bps_) * 4 / 3;
    if (debug_enabled_) {
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "SetRates sum_bps=%u target_bps=%d min_bps=%d max_bps=%d fps=%f",
                        static_cast<unsigned>(sum), target_bps_, min_bps_, max_bps_, parameters.framerate_fps);
    }
    // Temporary stabilization: avoid runtime MPP_ENC_SET_CFG churn until encoder path is stable.
    // TODO: restore guarded ApplyRcToCfg once crash root cause is fully resolved.
    // ApplyRcToCfg();
}

webrtc::VideoEncoder::EncoderInfo RkMppH264Encoder::GetEncoderInfo() const {
    webrtc::VideoEncoder::EncoderInfo info;
    // false: allow VideoStreamEncoder to CropAndScale kNative frames (RGA path) under
    // MAINTAIN_FRAMERATE so weak-network adaptation can reduce resolution.
    info.supports_native_handle =
        ReadEnvIntInRange("RFLOW_MPP_ENC_SUPPORTS_NATIVE_HANDLE", 0, 0, 1) == 1;
    info.implementation_name = "rockchip_mpp_h264";
    info.has_trusted_rate_controller = false;
    info.is_hardware_accelerated = true;
    info.supports_simulcast = false;
    info.requested_resolution_alignment = 16;
    info.scaling_settings = webrtc::VideoEncoder::ScalingSettings::kOff;
    info.resolution_bitrate_limits =
        webrtc::EncoderInfoSettings::GetDefaultSinglecastBitrateLimitsWhenQpIsUntrusted(
            webrtc::kVideoCodecH264);
    if (info.resolution_bitrate_limits.empty()) {
        info.resolution_bitrate_limits = {
            webrtc::VideoEncoder::ResolutionBitrateLimits(1280 * 720, 150000, 75000, 2500000),
            webrtc::VideoEncoder::ResolutionBitrateLimits(640 * 360, 50000, 30000, 1200000),
            webrtc::VideoEncoder::ResolutionBitrateLimits(320 * 180, 0, 0, 450000),
        };
    }
    // kNative for MPP MJPEG decode pass-through; keep NV12/I420 planar paths.
    info.preferred_pixel_formats = {webrtc::VideoFrameBuffer::Type::kNative,
                                    webrtc::VideoFrameBuffer::Type::kNV12,
                                    webrtc::VideoFrameBuffer::Type::kI420};
    return info;
}

int32_t RkMppH264Encoder::EmitAssembledFrame(const webrtc::VideoFrame& frame,
                                               const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& vfb,
                                               int64_t encode_before_us,
                                               int64_t on_frame_to_encode_enter_us,
                                               bool mpp_reports_intra) {
    if (split_assembly_buf_.empty()) {
        return WEBRTC_VIDEO_CODEC_OK;
    }
    const uint8_t* raw = split_assembly_buf_.data();
    const size_t len = split_assembly_buf_.size();
    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> buf;
    const auto nal_indices = webrtc::H264::FindNaluIndices(webrtc::ArrayView<const uint8_t>(raw, len));
    const bool detected_annexb = !nal_indices.empty();
    if (detected_annexb) {
        buf = webrtc::EncodedImageBuffer::Create(len);
        memcpy(buf->data(), raw, len);
    } else {
        bool annex_ok = FillAvcLengthPrefixedToAnnexB(raw, len, &annex_scratch_);
        if (!annex_ok) {
            annex_ok = FillAvcLengthPrefixed16ToAnnexB(raw, len, &annex_scratch_);
        }
        if (!annex_ok) {
            annex_ok = FillRawSingleNalToAnnexB(raw, len, &annex_scratch_);
        }
        if (!annex_ok) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] unparseable bitstream (assembled), request SW fallback; len=" << len;
            RFLOW_LOG_TAG_E("RkMppH264Err", "unparseable assembled bitstream len=%zu, request SW fallback", len);
            return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
        }
        buf = webrtc::EncodedImageBuffer::Create(annex_scratch_.size());
        memcpy(buf->data(), annex_scratch_.data(), annex_scratch_.size());
    }
    {
        const auto out_nals = webrtc::H264::FindNaluIndices(
            webrtc::ArrayView<const uint8_t>(buf->data(), buf->size()));
        bool has_sps = false;
        bool has_pps = false;
        bool has_idr = false;
        for (const auto& nalu : out_nals) {
            if (nalu.payload_start_offset >= buf->size()) {
                continue;
            }
            const uint8_t t = static_cast<uint8_t>(buf->data()[nalu.payload_start_offset] & 0x1f);
            has_sps = has_sps || (t == 7);
            has_pps = has_pps || (t == 8);
            has_idr = has_idr || (t == 5);
        }
        if (has_idr && (!has_sps || !has_pps) && !cached_extra_info_annexb_.empty()) {
            auto merged = webrtc::EncodedImageBuffer::Create(cached_extra_info_annexb_.size() + buf->size());
            memcpy(merged->data(), cached_extra_info_annexb_.data(), cached_extra_info_annexb_.size());
            memcpy(merged->data() + cached_extra_info_annexb_.size(), buf->data(), buf->size());
            buf = merged;
            if (debug_enabled_) {
                RFLOW_LOG_TAG_I("RkMppH264Dbg", "prepended cached sps/pps bytes=%zu for idr frame",
                                  cached_extra_info_annexb_.size());
            }
        }
    }
    (void)detected_annexb;
    (void)nal_indices;
    webrtc::H264BitstreamParser qp_parser;
    webrtc::EncodedImage encoded;
    encoded.SetEncodedData(buf);
    encoded._encodedWidth = width_;
    encoded._encodedHeight = height_;
    encoded.SetRtpTimestamp(frame.rtp_timestamp());
    encoded.SetColorSpace(frame.color_space());
    encoded.capture_time_ms_ = frame.render_time_ms();
    const int64_t encode_finish_us = webrtc::TimeMicros();
    encoded.SetEncodeTime(encode_before_us / webrtc::kNumMicrosecsPerMillisec,
                          encode_finish_us / webrtc::kNumMicrosecsPerMillisec);
    encoded.video_timing_mutable()->flags = webrtc::VideoSendTiming::kNotTriggered;

    if (latency_trace_enabled_) {
        static std::atomic<unsigned> enc_lat_n{0};
        const unsigned n = ++enc_lat_n;
        if ((n % 30u) == 0u) {
            const double ms = static_cast<double>(encode_finish_us - encode_before_us) / 1000.0;
            RFLOW_LOG_TAG_I("Latency", "MPP H264 encode put+get ms=%f sample#%u", ms, static_cast<unsigned>(n));
        }
    }

    if (mpp_reports_intra) {
        encoded._frameType = webrtc::VideoFrameType::kVideoFrameKey;
    } else if (AnnexBHasIdrNalu(buf->data(), buf->size())) {
        encoded._frameType = webrtc::VideoFrameType::kVideoFrameKey;
    } else {
        encoded._frameType = webrtc::VideoFrameType::kVideoFrameDelta;
    }
    if (encoded._frameType == webrtc::VideoFrameType::kVideoFrameKey) {
        last_idr_emit_us_ = webrtc::TimeMicros();
    }

    qp_parser.ParseBitstream(encoded);
    encoded.qp_ = qp_parser.GetLastSliceQp().value_or(-1);

    const uint32_t trace_tid = next_video_frame_tracking_id_;
    const bool trace_periodic_log = (trace_tid % trace_every_n_ == 0u);
    encoded.SetVideoFrameTrackingId(trace_tid);
    ++next_video_frame_tracking_id_;
    webrtc::CodecSpecificInfo specifics{};
    specifics.codecType = webrtc::kVideoCodecH264;
    specifics.codecSpecific.H264.packetization_mode = h264_settings_.packetization_mode;
    specifics.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    specifics.codecSpecific.H264.base_layer_sync = false;
    specifics.codecSpecific.H264.idr_frame =
        (encoded._frameType == webrtc::VideoFrameType::kVideoFrameKey);
    const int64_t before_on_encoded_cb_us = webrtc::TimeMicros();
    webrtc::EncodedImageCallback::Result res = callback_->OnEncodedImage(encoded, &specifics);
    const int64_t after_on_encoded_cb_us = webrtc::TimeMicros();
    const int64_t webrtc_onencodedimage_us = after_on_encoded_cb_us - before_on_encoded_cb_us;
    if (MediaTimingTraceEnabled()) {
        static std::atomic<unsigned> media_trace_n{0};
        const unsigned n = ++media_trace_n;
        if ((n % MediaTimingTraceEveryN()) == 0u) {
            RFLOW_LOG_TAG_I(
                "MEDIA_TIMING",
                "[tx] t_us=%lld trace_id=%u rtp_ts=%u t_frame_ts_us=%lld t_encode_done_us=%lld t_after_onencoded_us=%lld "
                "onencoded_cb_cost_us=%lld",
                static_cast<long long>(after_on_encoded_cb_us), static_cast<unsigned>(trace_tid), encoded.RtpTimestamp(),
                static_cast<long long>(frame.timestamp_us()), static_cast<long long>(encode_finish_us),
                static_cast<long long>(after_on_encoded_cb_us),
                static_cast<long long>(webrtc_onencodedimage_us));
        }
    }
    if (debug_enabled_) {
        static std::atomic<unsigned> enc_cb_n{0};
        const unsigned n = ++enc_cb_n;
        if ((n <= 5u) || ((n % 60u) == 0u)) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg",
                              "OnEncodedImage #%u size=%zu type=%s cb_error=%d", static_cast<unsigned>(n),
                              encoded.size(),
                              (encoded._frameType == webrtc::VideoFrameType::kVideoFrameKey ? "key" : "delta"),
                              static_cast<int>(res.error));
        }
    }
    if (res.error != webrtc::EncodedImageCallback::Result::OK) {
        RFLOW_LOG_TAG_E("RkMppH264Err", "OnEncodedImage callback error=%d", static_cast<int>(res.error));
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (e2e_trace_enabled_) {
        int64_t t_mjpeg_input_us = frame.timestamp_us();
        int64_t t_v4l2_us = -1;
        int64_t t_on_frame_us = -1;
        int64_t wall_utc_ms = -1;
        if (MppNativeDecFrameBuffer* e2e_native = MppNativeDecFrameBuffer::TryGet(vfb)) {
            const int64_t mjpeg_us = e2e_native->mjpeg_input_timestamp_us();
            if (mjpeg_us > 0) {
                t_mjpeg_input_us = mjpeg_us;
            }
            t_v4l2_us = e2e_native->v4l2_timestamp_us();
            t_on_frame_us = e2e_native->on_frame_enter_us();
            wall_utc_ms = e2e_native->wall_capture_utc_ms();
        }
        RFLOW_LOG_TAG_I(
            "E2E_TX",
            "rtp_ts=%u trace_id=%u t_mjpeg_input_us=%lld t_v4l2_us=%lld t_on_frame_us=%lld t_enc_done_us=%lld "
            "t_after_onencoded_us=%lld wall_utc_ms=%lld",
            encoded.RtpTimestamp(), static_cast<unsigned>(trace_tid), static_cast<long long>(t_mjpeg_input_us),
            static_cast<long long>(t_v4l2_us), static_cast<long long>(t_on_frame_us),
            static_cast<long long>(encode_finish_us), static_cast<long long>(after_on_encoded_cb_us),
            static_cast<long long>(wall_utc_ms));
    }
    if (trace_periodic_log) {
        int64_t mjpeg_input_to_encode_done_us = encode_finish_us - frame.timestamp_us();
        int64_t mjpeg_input_to_after_onencoded_us = after_on_encoded_cb_us - frame.timestamp_us();
        int64_t usb_to_frame_timestamp_us = -1;
        int64_t decode_queue_wait_us = -1;
        if (MppNativeDecFrameBuffer* native_fb = MppNativeDecFrameBuffer::TryGet(vfb)) {
            const int64_t mjpeg_input_us = native_fb->mjpeg_input_timestamp_us();
            if (mjpeg_input_us > 0) {
                mjpeg_input_to_encode_done_us = encode_finish_us - mjpeg_input_us;
                mjpeg_input_to_after_onencoded_us = after_on_encoded_cb_us - mjpeg_input_us;
            }
            const int64_t v4l2_timestamp_us = native_fb->v4l2_timestamp_us();
            if (v4l2_timestamp_us > 0) {
                usb_to_frame_timestamp_us = frame.timestamp_us() - v4l2_timestamp_us;
            }
            decode_queue_wait_us = native_fb->decode_queue_wait_us();
        }
        std::ostringstream trace_oss;
        trace_oss << "[" << CurrentLocalDateTimeYmdHmsMs() << "]: current_video_frame_tracking_id_=" << trace_tid
                  << ", mjpeg_input_to_encode_done_us=" << mjpeg_input_to_encode_done_us << " ("
                  << (static_cast<double>(mjpeg_input_to_encode_done_us) / 1000.0) << " ms)"
                  << ", mjpeg_input_to_after_onencoded_us=" << mjpeg_input_to_after_onencoded_us << " ("
                  << (static_cast<double>(mjpeg_input_to_after_onencoded_us) / 1000.0) << " ms)"
                  << ", webrtc_onencodedimage_us=" << webrtc_onencodedimage_us << " ("
                  << (static_cast<double>(webrtc_onencodedimage_us) / 1000.0) << " ms)"
                  << ", usb_to_frame_timestamp_us=" << usb_to_frame_timestamp_us;
        if (usb_to_frame_timestamp_us >= 0) {
            trace_oss << " (" << (static_cast<double>(usb_to_frame_timestamp_us) / 1000.0) << " ms)";
        }
        trace_oss << ", decode_queue_wait_us=" << decode_queue_wait_us << " ("
                  << (static_cast<double>(decode_queue_wait_us) / 1000.0) << " ms)"
                  << ", on_frame_to_encode_enter_us=" << on_frame_to_encode_enter_us;
        if (on_frame_to_encode_enter_us >= 0) {
            trace_oss << " (" << (static_cast<double>(on_frame_to_encode_enter_us) / 1000.0) << " ms)";
        }
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "%s", trace_oss.str().c_str());
    }
    if (mjpeg_to_h264_trace_enabled_) {
        static std::atomic<unsigned> g_pipe_n{0};
        const unsigned pn = ++g_pipe_n;
        if (pn % 30u == 0u) {
            const int64_t delta_us = encode_finish_us - frame.timestamp_us();
            RFLOW_LOG_TAG_I("Pipe MJPEG->H264",
                              "frame#%u v4l2_mjpeg_process_start_to_h264_ready_us=%lld (%.3f ms)", pn,
                              static_cast<long long>(delta_us), static_cast<double>(delta_us) / 1000.0);
        }
    }
    return WEBRTC_VIDEO_CODEC_OK;
}

bool RkMppH264Encoder::HandleOutputFailureAndMaybeRecover(const char* stage, int err_code) {
    ++consecutive_output_failures_;
    RTC_LOG(LS_WARNING) << "[RkMppH264] output failure stage=" << stage << " err=" << err_code
                        << " streak=" << consecutive_output_failures_;
    if (put_frame_diag_enabled_ &&
        (consecutive_output_failures_ == 1 ||
         consecutive_output_failures_ >= recover_hard_fail_threshold_ ||
         (consecutive_output_failures_ % 10) == 0)) {
        RFLOW_LOG_TAG_E("RkMppH264Diag", "output_failure stage=%s err=%d streak=%d recover_attempts=%u "
                                          "zero_copy_enabled=%d native_zc_failures=%d",
                        stage, err_code, consecutive_output_failures_, mpp_recover_attempts_,
                        native_zero_copy_enabled_ ? 1 : 0, native_zero_copy_failures_);
    }
    if (recover_disable_split_on_failure_ && split_by_byte_enabled_ &&
        consecutive_output_failures_ >= recover_soft_fail_threshold_) {
        split_by_byte_enabled_ = false;
        split_bytes_ = 0;
        RTC_LOG(LS_WARNING) << "[RkMppH264] auto-disable split-by-byte after failure streak="
                            << consecutive_output_failures_;
    }
    if (consecutive_output_failures_ >= recover_hard_fail_threshold_) {
        RTC_LOG(LS_ERROR) << "[RkMppH264] failure streak reached hard threshold="
                          << recover_hard_fail_threshold_ << ", attempting MPP recover";
        if (RecoverMppSessionLocked()) {
            consecutive_output_failures_ = 0;
            return true;
        }
        return false;
    }
    return true;
}

int32_t RkMppH264Encoder::Encode(const webrtc::VideoFrame& frame,
                                 const std::vector<webrtc::VideoFrameType>* frame_types) {
    std::lock_guard<std::mutex> lock(mpp_mu_);
    if (!initialized_ || !callback_ || !mpi_ || !mpp_ctx_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (debug_enabled_) {
        static std::atomic<unsigned> enc_call_n{0};
        const unsigned n = ++enc_call_n;
        if ((n <= 5u) || ((n % 60u) == 0u)) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "Encode called #%u ts_us=%lld rtp_ts=%u", n,
                            static_cast<long long>(frame.timestamp_us()), frame.rtp_timestamp());
        }
    }

    MppCtx ctx = reinterpret_cast<MppCtx>(mpp_ctx_);
    MppApi* mpi = reinterpret_cast<MppApi*>(mpi_);

    uint8_t* dst = static_cast<uint8_t*>(mpp_buffer_get_ptr(reinterpret_cast<MppBuffer>(frm_buf_)));
    if (!dst) {
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> vfb = frame.video_frame_buffer();
    const int64_t encode_enter_us = webrtc::TimeMicros();
    int64_t on_frame_to_encode_enter_us = -1;
    if (MppNativeDecFrameBuffer* nfb = MppNativeDecFrameBuffer::TryGet(vfb)) {
        const int64_t on_frame_us = nfb->on_frame_enter_us();
        if (on_frame_us > 0 && encode_enter_us >= on_frame_us) {
            on_frame_to_encode_enter_us = encode_enter_us - on_frame_us;
        }
    }
    MppBuffer input_mpp_buf = reinterpret_cast<MppBuffer>(frm_buf_);

    bool used_native_zero_copy = false;
    bool cpu_wrote_frm_buf = false;
    int frame_hor_stride = hor_stride_;
    int frame_ver_stride = ver_stride_;
    MppBuffer orphan_import_buf = nullptr;
    const auto release_orphan_import = [&]() {
        if (orphan_import_buf) {
            mpp_buffer_put(orphan_import_buf);
            orphan_import_buf = nullptr;
        }
    };
    struct OrphanImportGuard {
        std::function<void()> release;
        ~OrphanImportGuard() {
            if (release) {
                release();
            }
        }
    } orphan_import_guard{release_orphan_import};
    auto copy_i420_to_encoder_buffer = [&](const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& any_vfb) -> int32_t {
        webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = any_vfb->ToI420();
        if (!i420) {
            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
        }
        if (i420->width() != width_ || i420->height() != height_) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] frame size mismatch expect " << width_ << "x" << height_
                                << " got " << i420->width() << "x" << i420->height();
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        uint8_t* dst_uv = dst + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
        libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(),
                           i420->StrideV(), dst, hor_stride_, dst_uv, hor_stride_, width_, height_);
        cpu_wrote_frm_buf = true;
        return WEBRTC_VIDEO_CODEC_OK;
    };

    if (vfb->type() == webrtc::VideoFrameBuffer::Type::kNative) {
        MppNativeDecFrameBuffer* native = MppNativeDecFrameBuffer::TryGet(vfb);
        if (native) {
            MppBuffer ext = reinterpret_cast<MppBuffer>(native->mpp_buffer_handle());
            if (!ext) {
                if (native_zero_copy_strict_) {
                    RTC_LOG(LS_ERROR) << "[RkMppH264] strict native zero-copy: native frame has null mpp buffer";
                    return WEBRTC_VIDEO_CODEC_ERROR;
                }
                const int32_t rc = copy_i420_to_encoder_buffer(vfb);
                if (rc != WEBRTC_VIDEO_CODEC_OK) {
                    return rc;
                }
            } else {
                const auto* src_base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(ext));
                if (!src_base) {
                    if (native_zero_copy_strict_) {
                        RTC_LOG(LS_ERROR) << "[RkMppH264] strict native zero-copy: mpp_buffer_get_ptr(ext) is null";
                        return WEBRTC_VIDEO_CODEC_ERROR;
                    }
                    return WEBRTC_VIDEO_CODEC_ERROR;
                }
                const RK_U32 fmt = static_cast<RK_U32>(native->mpp_fmt());
                const int nhs = native->hor_stride();
                const int nvs = native->ver_stride();
                const uint8_t* src_y = src_base;
                const uint8_t* src_uv = src_base + static_cast<size_t>(nhs) * static_cast<size_t>(nvs);
                const bool dims_ok = (native->width() == width_ && native->height() == height_);
                const bool stride_ok = (nhs == hor_stride_ && nvs == ver_stride_);
                const bool zero_copy_stride_ok = stride_ok || (nhs >= width_ && nvs >= height_);
                const bool nv12_mpp = (fmt == MPP_FMT_YUV420SP);
                if (dims_ok && zero_copy_stride_ok && nv12_mpp && native_zero_copy_enabled_) {
                    MppBuffer imported =
                        reinterpret_cast<MppBuffer>(BindDecBufferForEncodeLocked(ext));
                    if (imported) {
                        input_mpp_buf = imported;
                        orphan_import_buf = imported;
                        used_native_zero_copy = true;
                        frame_hor_stride = nhs;
                        frame_ver_stride = nvs;
                    } else if (native_zero_copy_strict_) {
                        RTC_LOG(LS_ERROR) << "[RkMppH264] strict native zero-copy: import dec buffer to enc group failed";
                        return WEBRTC_VIDEO_CODEC_ERROR;
                    } else {
                        ++native_copy_fallback_frames_;
                        rflow::core::rtc::PushPipelineDropStats::Instance().OnZcFallback(1);
                        DmabufSyncStartRead(MppBufferFdOrNeg1(ext));
                        CopySemiPlanarToMppBuffer(src_y, src_uv, nhs, nhs, dst, hor_stride_, ver_stride_, width_,
                                                  height_);
                        cpu_wrote_frm_buf = true;
                    }
                } else if (dims_ok && (nv12_mpp || fmt == MPP_FMT_YUV420SP_VU)) {
                    DmabufSyncStartRead(MppBufferFdOrNeg1(ext));
                    if (fmt == MPP_FMT_YUV420SP) {
                        CopySemiPlanarToMppBuffer(src_y, src_uv, nhs, nhs, dst, hor_stride_, ver_stride_, width_, height_);
                    } else {
                        uint8_t* dst_uv = dst + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
                        if (libyuv::NV21ToNV12(src_y, nhs, src_uv, nhs, dst, hor_stride_, dst_uv, hor_stride_, width_,
                                               height_) != 0) {
                            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
                        }
                    }
                    cpu_wrote_frm_buf = true;
                } else {
                    RTC_LOG(LS_WARNING) << "[RkMppH264] native dec frame mismatch expect " << width_ << "x" << height_
                                        << " stride " << hor_stride_ << "x" << ver_stride_ << " fmt " << fmt << " got "
                                        << native->width() << "x" << native->height() << " stride " << nhs << "x" << nvs;
                    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
                }
            }
        } else {
            const int32_t rc = copy_i420_to_encoder_buffer(vfb);
            if (rc != WEBRTC_VIDEO_CODEC_OK) {
                return rc;
            }
        }
        if (native_zero_copy_strict_ && !used_native_zero_copy) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] strict native zero-copy required but frame fell back to copy path";
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
    } else if (vfb->type() == webrtc::VideoFrameBuffer::Type::kNV12) {
        const webrtc::NV12BufferInterface* nv12 = vfb->GetNV12();
        if (nv12->width() != width_ || nv12->height() != height_) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] NV12 frame size mismatch expect " << width_ << "x" << height_
                                << " got " << nv12->width() << "x" << nv12->height();
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        CopyNv12ToMppBuffer(nv12, dst, hor_stride_, ver_stride_, width_, height_);
        cpu_wrote_frm_buf = true;
    } else {
        webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = vfb->ToI420();
        if (!i420) {
            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
        }
        if (i420->width() != width_ || i420->height() != height_) {
            RTC_LOG(LS_WARNING) << "[RkMppH264] frame size mismatch expect " << width_ << "x" << height_
                                << " got " << i420->width() << "x" << i420->height();
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        uint8_t* dst_uv = dst + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
        libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(),
                           i420->StrideV(), dst, hor_stride_, dst_uv, hor_stride_, width_, height_);
        cpu_wrote_frm_buf = true;
    }

    if (cpu_wrote_frm_buf) {
        DmabufSyncEndWrite(MppBufferFdOrNeg1(reinterpret_cast<MppBuffer>(frm_buf_)));
    }

    bool want_key = false;
    if (frame_types) {
        for (webrtc::VideoFrameType t : *frame_types) {
            if (t == webrtc::VideoFrameType::kVideoFrameKey) {
                want_key = true;
                break;
            }
        }
    }
    const bool enable_idr_ctrl = ReadEnvIntInRange("RFLOW_MPP_ENC_ENABLE_IDR_CTRL", 0, 0, 1) == 1;
    if (want_key && enable_idr_ctrl) {
        bool allow_force_idr = true;
        const int64_t now_us = webrtc::TimeMicros();
        const int64_t min_interval_us = static_cast<int64_t>(std::max(0, idr_min_interval_ms_)) * 1000;
        const int64_t loss_quick_us = static_cast<int64_t>(std::max(0, idr_loss_quick_trigger_ms_)) * 1000;
        const int64_t force_max_wait_us = static_cast<int64_t>(std::max(1, idr_force_max_wait_ms_)) * 1000;
        if (min_interval_us > 0 && last_forced_idr_ctrl_us_ > 0 && now_us > last_forced_idr_ctrl_us_ &&
            (now_us - last_forced_idr_ctrl_us_) < min_interval_us) {
            allow_force_idr = false;
            if (loss_quick_us > 0 && last_idr_emit_us_ > 0 && now_us > last_idr_emit_us_ &&
                (now_us - last_idr_emit_us_) >= loss_quick_us) {
                allow_force_idr = true;
            }
            if (last_idr_emit_us_ > 0 && now_us > last_idr_emit_us_ &&
                (now_us - last_idr_emit_us_) >= force_max_wait_us) {
                allow_force_idr = true;
            }
        }
        if (allow_force_idr) {
            if (mpi->control(ctx, MPP_ENC_SET_IDR_FRAME, nullptr) == MPP_OK) {
                last_forced_idr_ctrl_us_ = now_us;
            } else {
                RTC_LOG(LS_WARNING) << "[RkMppH264] MPP_ENC_SET_IDR_FRAME failed";
            }
        }
    }

    MppFrame mframe = nullptr;
    if (mpp_frame_init(&mframe) != MPP_OK) {
        return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    mpp_frame_set_width(mframe, static_cast<RK_U32>(width_));
    mpp_frame_set_height(mframe, static_cast<RK_U32>(height_));
    mpp_frame_set_hor_stride(mframe, static_cast<RK_U32>(frame_hor_stride));
    mpp_frame_set_ver_stride(mframe, static_cast<RK_U32>(frame_ver_stride));
    mpp_frame_set_fmt(mframe, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(mframe, input_mpp_buf);
    // Some BSP builds are unstable with very large microsecond PTS values.
    // Use RTP timestamp scale for encoder input PTS to keep values bounded.
    mpp_frame_set_pts(mframe, static_cast<RK_S64>(frame.rtp_timestamp()));
    MppBuffer held_input_buf = nullptr;
    HeldMppInputGuard held_input_guard{&held_input_buf};
    if (used_native_zero_copy && input_mpp_buf && input_mpp_buf != reinterpret_cast<MppBuffer>(frm_buf_)) {
        DmabufSyncStartRead(mpp_buffer_get_fd(input_mpp_buf));
        held_input_buf = input_mpp_buf;
        orphan_import_buf = nullptr;
    }

    MppMeta meta = mpp_frame_get_meta(mframe);
    MppPacket prebound_pkt = nullptr;
    (void)meta;

    const int64_t encode_before_us = webrtc::TimeMicros();
    const bool use_sync_encode = use_sync_encode_;
    const bool use_task_encode = use_task_encode_;
    MppNativeDecFrameBuffer* native_fb_diag = MppNativeDecFrameBuffer::TryGet(vfb);
    const MppBuffer frm_mpp_buf = reinterpret_cast<MppBuffer>(frm_buf_);
    auto log_put_frame_fail = [&](const char* phase, int put_ret) {
        LogPutFrameFailureDiag(put_frame_diag_enabled_, phase, put_ret, used_native_zero_copy,
                               native_zero_copy_enabled_, input_mpp_buf, frm_mpp_buf, width_, height_,
                               hor_stride_, ver_stride_, native_fb_diag, consecutive_output_failures_,
                               mpp_recover_attempts_);
    };
    if (debug_enabled_) {
        const char* path = use_task_encode ? "task_encode" : (use_sync_encode ? "encode" : "encode_put_frame");
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "before %s ts_us=%lld", path, static_cast<long long>(frame.timestamp_us()));
        RFLOW_LOG_TAG_I("RkMppH264Dbg", "frame fmt=%d hor_stride=%d ver_stride=%d input_fd=%d",
                        static_cast<int>(mpp_frame_get_fmt(mframe)), mpp_frame_get_hor_stride(mframe),
                        mpp_frame_get_ver_stride(mframe), mpp_buffer_get_fd(input_mpp_buf));
    }
    MPP_RET ret = MPP_NOK;
    MppPacket first_out_pkt = nullptr;
    MppTask output_task = nullptr;
    static std::atomic<unsigned> encode_probe_n{0};
    const unsigned encode_probe_idx = ++encode_probe_n;
    auto recover_or_error = [&](const char* stage, int err_code) -> int32_t {
        split_assembly_buf_.clear();
        if (prebound_pkt) {
            mpp_packet_deinit(&prebound_pkt);
        }
        if (output_task) {
            mpi->enqueue(ctx, MPP_PORT_OUTPUT, output_task);
            output_task = nullptr;
        }
        held_input_guard.release();
        return HandleOutputFailureAndMaybeRecover(stage, err_code) ? WEBRTC_VIDEO_CODEC_OK
                                                                    : WEBRTC_VIDEO_CODEC_ERROR;
    };
    if (use_task_encode) {
        MppTask input_task = nullptr;
        ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "poll input ret=%d", static_cast<int>(ret));
        }
        if (ret != MPP_OK) {
            mpp_frame_deinit(&mframe);
            RFLOW_LOG_TAG_E("RkMppH264Err", "poll input ret=%d", static_cast<int>(ret));
            held_input_guard.release();
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &input_task);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "dequeue input ret=%d task=%d", static_cast<int>(ret),
                            input_task ? 1 : 0);
        }
        if (ret != MPP_OK || !input_task) {
            mpp_frame_deinit(&mframe);
            RFLOW_LOG_TAG_E("RkMppH264Err", "dequeue input ret=%d", static_cast<int>(ret));
            held_input_guard.release();
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        mpp_task_meta_set_frame(input_task, KEY_INPUT_FRAME, mframe);
        if (md_buf_ && !use_task_encode) {
            mpp_task_meta_set_buffer(input_task, KEY_MOTION_INFO, reinterpret_cast<MppBuffer>(md_buf_));
        }
        ret = mpi->enqueue(ctx, MPP_PORT_INPUT, input_task);
        mpp_frame_deinit(&mframe);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "enqueue input ret=%d", static_cast<int>(ret));
        }
        if (ret != MPP_OK) {
            RFLOW_LOG_TAG_E("RkMppH264Err", "enqueue input ret=%d", static_cast<int>(ret));
            held_input_guard.release();
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "poll output(task) ret=%d", static_cast<int>(ret));
        }
        if (ret != MPP_OK) {
            RFLOW_LOG_TAG_E("RkMppH264Err", "poll output(task) ret=%d", static_cast<int>(ret));
            return recover_or_error("task_poll_output", ret);
        }
        ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &output_task);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "dequeue output ret=%d task=%d", static_cast<int>(ret),
                            output_task ? 1 : 0);
        }
        if (ret != MPP_OK || !output_task) {
            RFLOW_LOG_TAG_E("RkMppH264Err", "dequeue output(task) ret=%d", static_cast<int>(ret));
            return recover_or_error("task_dequeue_output", ret);
        }
        // In task mode, read KEY_OUTPUT_PACKET by default.
        const bool task_read_packet = task_read_packet_;
        if (task_read_packet) {
            ret = mpp_task_meta_get_packet(output_task, KEY_OUTPUT_PACKET, &first_out_pkt);
            if (debug_enabled_) {
                RFLOW_LOG_TAG_I("RkMppH264Dbg", "get output packet ret=%d pkt=%d", static_cast<int>(ret),
                                first_out_pkt ? 1 : 0);
            }
            if (ret != MPP_OK || !first_out_pkt) {
                RFLOW_LOG_TAG_E("RkMppH264Err", "task output has no packet ret=%d pkt=%d", static_cast<int>(ret),
                                first_out_pkt ? 1 : 0);
                return recover_or_error("task_get_packet", ret);
            }
        } else if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "task output dequeued; packet read skipped by env");
        }
    } else if (use_sync_encode) {
        ret = mpi->encode(ctx, mframe, &first_out_pkt);
        mpp_frame_deinit(&mframe);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "after encode ret=%d first_pkt=%d", static_cast<int>(ret),
                            first_out_pkt ? 1 : 0);
        }
        if (ret != MPP_OK) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] encode ret=" << ret;
            RFLOW_LOG_TAG_E("RkMppH264Err", "encode ret=%d", static_cast<int>(ret));
            held_input_guard.release();
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        if (debug_enabled_ && encode_probe_idx <= 3) {
            MppPacket probe_extra = nullptr;
            MPP_RET probe_ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &probe_extra);
            size_t probe_len = (probe_extra ? mpp_packet_get_length(probe_extra) : 0u);
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "probe extra_info after encode #%u ret=%d len=%zu",
                            static_cast<unsigned>(encode_probe_idx), static_cast<int>(probe_ret), probe_len);
            if (probe_extra) {
                mpp_packet_deinit(&probe_extra);
            }
        }
    } else {
        // Do NOT prebind KEY_OUTPUT_PACKET unless split-by-byte is enabled.
        // Root cause of empty_eoi: prebound pkt_buf_ is returned by encode_get_packet with length=0
        // before hardware fills it; is_part==0 was misread as frame EOI (see MPP async enc API).
        if (split_by_byte_enabled_ && meta && pkt_buf_) {
            if (mpp_packet_init_with_buffer(&prebound_pkt, reinterpret_cast<MppBuffer>(pkt_buf_)) == MPP_OK &&
                prebound_pkt) {
                ResetMppPacketWriteCursor(prebound_pkt);
                mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, prebound_pkt);
            }
        }
        if (simulate_put_frame_fail_remaining_ > 0) {
            --simulate_put_frame_fail_remaining_;
            mpp_frame_deinit(&mframe);
            held_input_guard.release();
            return recover_or_error("simulate_put_frame_fail", -1);
        }
        auto sync_frm_buf_after_cpu_write = [&]() {
            DmabufSyncEndWrite(MppBufferFdOrNeg1(reinterpret_cast<MppBuffer>(frm_buf_)));
        };
        ret = mpi->encode_put_frame(ctx, mframe);
        if (ret != MPP_OK && input_mpp_buf != reinterpret_cast<MppBuffer>(frm_buf_)) {
            log_put_frame_fail("zero_copy_first", ret);
            const int drained_before_fallback =
                DrainPendingEncoderOutput(mpi, ctx, output_timeout_ms_, put_frame_drain_max_);
            if (put_frame_diag_enabled_ && drained_before_fallback > 0) {
                RFLOW_LOG_TAG_W("RkMppH264Diag", "zero_copy put_frame fail drain=%d before memcpy retry",
                                drained_before_fallback);
            }
            if (native_zero_copy_strict_) {
                RTC_LOG(LS_ERROR) << "[RkMppH264] strict native zero-copy: encode_put_frame failed ret=" << ret;
                mpp_frame_deinit(&mframe);
                held_input_guard.release();
                return WEBRTC_VIDEO_CODEC_ERROR;
            }
            MppNativeDecFrameBuffer* native_fb = MppNativeDecFrameBuffer::TryGet(vfb);
            if (native_fb) {
                MppBuffer ext = reinterpret_cast<MppBuffer>(native_fb->mpp_buffer_handle());
                const auto* src_base = ext ? static_cast<const uint8_t*>(mpp_buffer_get_ptr(ext)) : nullptr;
                if (src_base) {
                    const RK_U32 fmt = static_cast<RK_U32>(native_fb->mpp_fmt());
                    const int nhs = native_fb->hor_stride();
                    const int nvs = native_fb->ver_stride();
                    const uint8_t* src_y = src_base;
                    const uint8_t* src_uv = src_base + static_cast<size_t>(nhs) * static_cast<size_t>(nvs);
                    RTC_LOG(LS_WARNING) << "[RkMppH264] zero-copy encode_put_frame failed ret=" << ret
                                        << ", retry with memcpy to encoder buffer";
                    held_input_guard.release();
                    DmabufSyncStartRead(MppBufferFdOrNeg1(ext));
                    if (fmt == MPP_FMT_YUV420SP) {
                        CopySemiPlanarToMppBuffer(src_y, src_uv, nhs, nhs, dst, hor_stride_, ver_stride_, width_, height_);
                    } else if (fmt == MPP_FMT_YUV420SP_VU) {
                        uint8_t* dst_uv = dst + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
                        libyuv::NV21ToNV12(src_y, nhs, src_uv, nhs, dst, hor_stride_, dst_uv, hor_stride_, width_, height_);
                    }
                    sync_frm_buf_after_cpu_write();
                    used_native_zero_copy = false;
                    input_mpp_buf = reinterpret_cast<MppBuffer>(frm_buf_);
                    mpp_frame_set_buffer(mframe, input_mpp_buf);
                    ret = mpi->encode_put_frame(ctx, mframe);
                    if (ret != MPP_OK) {
                        log_put_frame_fail("after_memcpy", ret);
                    }
                } else {
                    const int32_t rc = copy_i420_to_encoder_buffer(vfb);
                    if (rc == WEBRTC_VIDEO_CODEC_OK) {
                        ++native_copy_fallback_frames_;
                        rflow::core::rtc::PushPipelineDropStats::Instance().OnZcFallback(1);
                        sync_frm_buf_after_cpu_write();
                        used_native_zero_copy = false;
                        input_mpp_buf = reinterpret_cast<MppBuffer>(frm_buf_);
                        mpp_frame_set_buffer(mframe, input_mpp_buf);
                        ret = mpi->encode_put_frame(ctx, mframe);
                        if (ret != MPP_OK) {
                            log_put_frame_fail("after_i420_memcpy", ret);
                        }
                    }
                }
                if (ret != MPP_OK) {
                    ++native_zero_copy_failures_;
                    if (native_zero_copy_enabled_ &&
                        native_zero_copy_failures_ >= native_zero_copy_fail_disable_threshold_) {
                        native_zero_copy_enabled_ = false;
                        RTC_LOG(LS_WARNING) << "[RkMppH264] disable native zero-copy after "
                                            << native_zero_copy_failures_ << " put_frame failures";
                        RFLOW_LOG_TAG_W("RkMppH264Warn", "disable native zero-copy after %u failures",
                                        static_cast<unsigned>(native_zero_copy_failures_));
                    }
                } else {
                    native_zero_copy_failures_ = 0;
                }
            }
        }
        if (ret != MPP_OK) {
            if (input_mpp_buf == reinterpret_cast<MppBuffer>(frm_buf_)) {
                log_put_frame_fail("own_buffer", ret);
            } else if (!used_native_zero_copy) {
                log_put_frame_fail("final", ret);
            }
            const int drained =
                DrainPendingEncoderOutput(mpi, ctx, output_timeout_ms_, put_frame_drain_max_);
            if (put_frame_diag_enabled_ && drained > 0) {
                RFLOW_LOG_TAG_W("RkMppH264Diag", "put_frame drain=%d pkts before retry ret=%d", drained,
                                static_cast<int>(ret));
            }
            ret = mpi->encode_put_frame(ctx, mframe);
            if (ret == MPP_OK) {
                if (put_frame_diag_enabled_) {
                    RFLOW_LOG_TAG_W("RkMppH264Warn", "put_frame ok after drain (discarded_pkts=%d)", drained);
                }
            } else if (input_mpp_buf == reinterpret_cast<MppBuffer>(frm_buf_)) {
                log_put_frame_fail("own_buffer_after_drain", ret);
            } else if (!used_native_zero_copy) {
                log_put_frame_fail("final_after_drain", ret);
            }
        }
        if (ret == MPP_OK && used_native_zero_copy) {
            ++native_zero_copy_frames_;
        }
        mpp_frame_deinit(&mframe);
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "after encode_put_frame ret=%d sync_mode=%d", static_cast<int>(ret),
                            use_sync_encode ? 1 : 0);
        }
        if (ret != MPP_OK) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] encode_put_frame ret=" << ret;
            RFLOW_LOG_TAG_E("RkMppH264Err", "encode_put_frame ret=%d", static_cast<int>(ret));
            held_input_guard.release();
            return recover_or_error("encode_put_frame", ret);
        }
        if (debug_enabled_ && encode_probe_idx <= 3) {
            MppPacket probe_extra = nullptr;
            MPP_RET probe_ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &probe_extra);
            size_t probe_len = (probe_extra ? mpp_packet_get_length(probe_extra) : 0u);
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "probe extra_info after put_frame #%u ret=%d len=%zu",
                            static_cast<unsigned>(encode_probe_idx), static_cast<int>(probe_ret), probe_len);
            if (probe_extra) {
                mpp_packet_deinit(&probe_extra);
            }
        }
    }

    // Same as mpi_enc_test: in non-split mode one packet usually ends frame (is_part==0 as EOI).
    // In split mode, assemble all parts before OnEncodedImage.
    split_assembly_buf_.clear();
    if (use_sync_encode && !first_out_pkt) {
        // Sync encode API may return no packet for this input; do not block current frame.
        // Allow next frames to drive encoder forward.
        held_input_guard.release();
        return WEBRTC_VIDEO_CODEC_OK;
    }
    const int max_pkt_iterations = split_by_byte_enabled_ ? 2048 : 64;
    const int packet_retry_limit = empty_pkt_retry_max_;
    const int packet_retry_sleep_us = 500;
    const bool packet_poll_block = true;
    bool frame_output_done = false;
    bool mpp_intra_hint = false;
    int safety = 0;
    int empty_pkt_retry = 0;
    int empty_eoi_retry = 0;
    auto finish_dropped_frame = [&]() -> int32_t {
        split_assembly_buf_.clear();
        if (prebound_pkt) {
            mpp_packet_deinit(&prebound_pkt);
            prebound_pkt = nullptr;
        }
        if (output_task) {
            mpi->enqueue(ctx, MPP_PORT_OUTPUT, output_task);
            output_task = nullptr;
        }
        held_input_guard.release();
        MaybeShrinkScratchBuffer(&split_assembly_buf_, 1024 * 1024, 0, next_video_frame_tracking_id_);
        MaybeShrinkScratchBuffer(&annex_scratch_, 1024 * 1024, 64 * 1024, next_video_frame_tracking_id_);
        return WEBRTC_VIDEO_CODEC_OK;
    };
    do {
        if (debug_enabled_) {
            static std::atomic<unsigned> get_pkt_n{0};
            const unsigned n = ++get_pkt_n;
            if ((n <= 5u) || ((n % 60u) == 0u)) {
                RFLOW_LOG_TAG_I("RkMppH264Dbg", "encode_get_packet try #%u", n);
            }
        }
        if (use_task_encode && !first_out_pkt) {
            MPP_RET poll_ret = MPP_OK;
            poll_ret = mpi->poll(ctx, MPP_PORT_OUTPUT, packet_poll_block ? MPP_POLL_BLOCK : MPP_POLL_NON_BLOCK);
            if (debug_enabled_) {
                RFLOW_LOG_TAG_I("RkMppH264Dbg", "poll output ret=%d", static_cast<int>(poll_ret));
            }
            if (poll_ret != MPP_OK && poll_ret != MPP_ERR_TIMEOUT) {
                return recover_or_error("encode_poll_output", poll_ret);
            }
        }
        MppPacket out_pkt = nullptr;
        if (first_out_pkt) {
            out_pkt = first_out_pkt;
            first_out_pkt = nullptr;
            ret = MPP_OK;
        } else {
            ret = mpi->encode_get_packet(ctx, &out_pkt);
        }
        if (out_pkt && prebound_pkt && out_pkt == prebound_pkt) {
            prebound_pkt = nullptr;
        }
        if (ret == MPP_ERR_TIMEOUT) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] encode_get_packet timeout";
            RFLOW_LOG_TAG_E("RkMppH264Err", "encode_get_packet timeout");
            return recover_or_error("encode_get_packet_timeout", ret);
        }
        if (ret != MPP_OK) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] encode_get_packet ret=" << ret;
            RFLOW_LOG_TAG_E("RkMppH264Err", "encode_get_packet ret=%d", static_cast<int>(ret));
            return recover_or_error("encode_get_packet_ret", ret);
        }
        if (!out_pkt) {
            if (empty_pkt_retry < packet_retry_limit) {
                ++empty_pkt_retry;
                if (debug_enabled_ && (empty_pkt_retry <= 3 || (empty_pkt_retry % 10) == 0)) {
                    RFLOW_LOG_TAG_I("RkMppH264Dbg", "empty packet retry %d/%d sleep_us=%d", empty_pkt_retry,
                                    packet_retry_limit, packet_retry_sleep_us);
                }
                usleep(static_cast<unsigned>(packet_retry_sleep_us));
                continue;
            }
            break;
        }
        const RK_U32 is_part = mpp_packet_is_partition(out_pkt);
        void* pos = nullptr;
        const size_t len = MppPacketReadableLength(out_pkt, &pos);
        if (len == 0) {
            mpp_packet_deinit(&out_pkt);
            if (empty_pkt_retry < packet_retry_limit) {
                ++empty_pkt_retry;
                usleep(static_cast<unsigned>(packet_retry_sleep_us));
                continue;
            }
            continue;
        }
        empty_pkt_retry = 0;
        const bool pkt_eoi =
            split_by_byte_enabled_ ? ((is_part == 0) || (mpp_packet_is_eoi(out_pkt) != 0)) : true;
        {
            RK_S32 intra = 0;
            if (mpp_packet_has_meta(out_pkt) &&
                mpp_meta_get_s32(mpp_packet_get_meta(out_pkt), KEY_OUTPUT_INTRA, &intra) == MPP_OK && intra) {
                mpp_intra_hint = true;
            }
        }
        if (pos) {
            const uint8_t* raw = static_cast<const uint8_t*>(pos);
            split_assembly_buf_.insert(split_assembly_buf_.end(), raw, raw + len);
        }
        mpp_packet_deinit(&out_pkt);
        if (pkt_eoi) {
            if (split_assembly_buf_.empty()) {
                if (empty_eoi_retry < empty_eoi_retry_max_) {
                    ++empty_eoi_retry;
                    usleep(static_cast<unsigned>(packet_retry_sleep_us));
                    continue;
                }
                LogEmptyEoiPacketDiag(put_frame_diag_enabled_, nullptr, frame.rtp_timestamp(),
                                      next_video_frame_tracking_id_, split_by_byte_enabled_, empty_eoi_retry,
                                      empty_eoi_retry_max_, safety, split_assembly_buf_.size());
                RTC_LOG(LS_WARNING) << "[RkMppH264] empty EOI after " << empty_eoi_retry
                                    << " retries, dropping input frame (MPP skip/drain)";
                {
                    static std::atomic<uint64_t> empty_eoi_drop_total{0};
                    const uint64_t total = empty_eoi_drop_total.fetch_add(1, std::memory_order_relaxed) + 1;
                    char detail[96];
                    snprintf(detail, sizeof(detail), "reason=empty_eoi retries=%d rtp_ts=%u",
                             empty_eoi_retry, frame.rtp_timestamp());
                    rflow::core::rtc::PushPipelineDropStats::Instance().LogDrop("MppH264/Encode", 1, total,
                                                                                     detail);
                }
                RFLOW_LOG_TAG_W("RkMppH264Warn", "empty_eoi_drop retries=%d rtp_ts=%u tracking_id=%u",
                                empty_eoi_retry, frame.rtp_timestamp(),
                                static_cast<unsigned>(next_video_frame_tracking_id_));
                return finish_dropped_frame();
            }
            const int32_t emit_ret =
                EmitAssembledFrame(frame, vfb, encode_before_us, on_frame_to_encode_enter_us, mpp_intra_hint);
            if (emit_ret != WEBRTC_VIDEO_CODEC_OK) {
                return emit_ret;
            }
            split_assembly_buf_.clear();
            mpp_intra_hint = false;
            frame_output_done = true;
        }
        if (++safety > max_pkt_iterations) {
            RTC_LOG(LS_ERROR) << "[RkMppH264] encode_get_packet exceeded safety iterations";
            return recover_or_error("encode_get_packet_safety", safety);
        }
    } while (!frame_output_done);
    if (!frame_output_done) {
        split_assembly_buf_.clear();
        RTC_LOG(LS_ERROR) << "[RkMppH264] encoder output finished without EOI (no complete frame)";
        return recover_or_error("no_eoi_frame", -1);
    }
    if (prebound_pkt) {
        mpp_packet_deinit(&prebound_pkt);
    }
    if (output_task) {
        if (debug_enabled_) {
            RFLOW_LOG_TAG_I("RkMppH264Dbg", "enqueue output task back");
        }
        mpi->enqueue(ctx, MPP_PORT_OUTPUT, output_task);
    }
    held_input_guard.release();
    MaybeShrinkScratchBuffer(&split_assembly_buf_, 1024 * 1024, 0, next_video_frame_tracking_id_);
    MaybeShrinkScratchBuffer(&annex_scratch_, 1024 * 1024, 64 * 1024, next_video_frame_tracking_id_);
    consecutive_output_failures_ = 0;
    return WEBRTC_VIDEO_CODEC_OK;
}

}  // namespace rflow::rtc::hw::rockchip_mpp
