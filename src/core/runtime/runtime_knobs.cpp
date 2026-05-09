#include "core/runtime/runtime_knobs.h"

#include "common/base/env_reader.h"
#include "core/base/logging.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rflow::core::runtime {

namespace {

// =========================================================================
// KnobSpec 表 — 集中登记 SDK 全部已知 runtime knob。
//
// 表里收录范围：
//   - 所有曾在 src/ 中以 std::getenv / ReadEnvIntInRange / ReadEnvBool /
//     TraceFlagEnabled 出现的环境变量名；
//   - 即使尚未通过 ReadInt/ReadBool/ReadSize 接口访问，登记在表中也可：
//     a) 启动 dump 时把当前值列出；
//     b) docs/RUNTIME_KNOBS.md 文档自动生成；
//
// 命名约定：
//   - 推荐 RFLOW_* 前缀；老 WEBRTC_* 仍登记，并在主名 RFLOW_* 一栏列出迁移目标
//     （若已有），通过 alias 字段串联，启动 dump 时打 deprecation 标记。
//   - 暂未提供 RFLOW_* 主名的 WEBRTC_* knob，main = "WEBRTC_..."、alias = nullptr，
//     待后续主版本统一改名。
//
// 维护：新增 knob 时，必须在此表追加一行；structure_check 工具后续可扩展校验。
// =========================================================================

constexpr KnobSpec kKnobTable[] = {
    // ---------- Signal --------------------------------------------------
    {"RFLOW_VERBOSE_SIGNAL", nullptr, KnobGroup::kSignal, KnobKind::kBool, 0, 0, 1, "",
     "信令客户端冗长日志（register_json 等）"},
    {"RFLOW_SIGNALING_TIMING_TRACE", nullptr, KnobGroup::kSignal, KnobKind::kBool, 0, 0, 1, "",
     "信令时序追踪（offer→answer→ICE 各阶段耗时）"},

    // ---------- Service business -----------------------------------------
    {"RFLOW_SVC_DEFAULT_FPS", nullptr, KnobGroup::kService, KnobKind::kInt, 30, 1, 240, "",
     "service 默认推流帧率（业务未指定时）"},
    {"RFLOW_SVC_DEFAULT_BITRATE_KBPS", nullptr, KnobGroup::kService, KnobKind::kInt, 0, 0, 200000, "",
     "service 默认目标码率 kbps（0=自动）"},
    {"RFLOW_SVC_DEFAULT_MIN_BITRATE_KBPS", nullptr, KnobGroup::kService, KnobKind::kInt, 0, 0, 200000, "",
     "service 默认最小码率 kbps"},
    {"RFLOW_SVC_DEFAULT_MAX_BITRATE_KBPS", nullptr, KnobGroup::kService, KnobKind::kInt, 0, 0, 200000, "",
     "service 默认最大码率 kbps"},
    {"RFLOW_SVC_PREFER_INTERNAL_VIDEO_SOURCE", nullptr, KnobGroup::kService, KnobKind::kBool, 0, 0, 1, "",
     "service 默认偏好内部 V4L2 采集而非外部投帧"},
    {"RFLOW_SVC_DEGRADATION_PREFERENCE", nullptr, KnobGroup::kService, KnobKind::kString, 0, 0, 0,
     "maintain_framerate",
     "WebRTC degradation_preference: maintain_framerate / maintain_resolution / balanced / disabled"},

    // ---------- RTC factory ----------------------------------------------
    {"RFLOW_ZERO_PLAYOUT_MIN_PACING_MS", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 1, 0, 20, "",
     "JitterBuffer playout 最小 pacing；0=极致低时延"},
    {"RFLOW_MAX_DECODE_QUEUE_SIZE", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 6, 4, 16, "",
     "解码队列上限；越小越低时延但更易丢帧"},
    {"RFLOW_ENABLE_DECODE_QUEUE_GUARD", nullptr, KnobGroup::kRtcFactory, KnobKind::kBool, 0, 0, 1, "",
     "启用解码队列的 guard 自动丢帧"},
    {"RFLOW_DECODE_QUEUE_GUARD_CAP", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 6, 4, 12, "",
     "decode queue guard 容量"},
    {"RFLOW_ENABLE_FLEXFEC", nullptr, KnobGroup::kRtcFactory, KnobKind::kBool, 0, 0, 1, "",
     "启用 FlexFEC 前向纠错"},
    {"RFLOW_FIELD_TRIALS_APPEND", nullptr, KnobGroup::kRtcFactory, KnobKind::kString, 0, 0, 0, "",
     "WebRTC FieldTrials 字符串附加项（实验功能开关）"},

    // ---------- Media trace ----------------------------------------------
    {"WEBRTC_LATENCY_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "推流端到端延迟单段追踪（capture/encode/send 等）"},
    {"WEBRTC_E2E_LATENCY_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "推/拉两端拼接的 E2E latency trace（trace_id 关联）"},
    {"RFLOW_MEDIA_TIMING_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "MPP 编/解码器内部 timing 追踪"},
    {"RFLOW_MEDIA_TIMING_TRACE_EVERY_N", nullptr, KnobGroup::kMediaTrace, KnobKind::kInt, 60, 1, 600, "",
     "MEDIA_TIMING_TRACE 采样步长（每 N 帧一行）"},
    {"WEBRTC_MJPEG_TO_H264_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG→H264 路径专用 trace"},
    {"WEBRTC_MJPEG_DEC_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG 解码器内部追踪"},

    // ---------- Media capture / scheduling -------------------------------
    {"RFLOW_MEDIA_THREAD_SCHED", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "媒体线程调度策略：rr / fifo / nice"},
    {"RFLOW_MEDIA_THREAD_RR_PRIO", nullptr, KnobGroup::kMediaCapture, KnobKind::kInt, 20, 1, 90, "",
     "SCHED_RR 优先级"},
    {"RFLOW_MEDIA_THREAD_NICE", nullptr, KnobGroup::kMediaCapture, KnobKind::kInt, -8, -20, 19, "",
     "nice 值（仅当 SCHED=nice）"},
    {"RFLOW_MJPEG_DECODE_CPU", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "MJPEG 解码线程 CPU 亲和（taskset 风格 mask 或单 CPU id）"},
    {"RFLOW_V4L2_CAPTURE_CPU", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "V4L2 capture 线程 CPU 亲和"},

    // ---------- Media MJPEG ---------------------------------------------
    {"WEBRTC_MJPEG_DECODE_QUEUE_MAX_WAIT_MS", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kInt, 25, 0, 5000, "",
     "MJPEG 解码队列最大等待毫秒；超时丢帧"},
    {"WEBRTC_MJPEG_ZERO_COPY_TO_ENC", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG → MPP H264 编码零拷贝路径开关"},
    {"WEBRTC_MJPEG_DECODE_INLINE", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG 解码内联在 capture 线程（省一次队列+线程切换）"},
    {"WEBRTC_MJPEG_QUEUE_LATEST_ONLY", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 1, 0, 1, "",
     "MJPEG 队列只保留最新帧（默认 1）"},
    {"WEBRTC_MJPEG_QUEUE_MAX", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kInt, 2, 1, 16, "",
     "MJPEG 队列最大长度"},
    {"WEBRTC_PREFER_MJPEG_PIXFMT", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kString, 0, 0, 0, "",
     "MJPEG 解码后偏好像素格式（nv12 / i420）"},
    {"WEBRTC_MJPEG_V4L2_DMABUF", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "V4L2 MJPEG 走 dma-buf 接收"},
    {"WEBRTC_MJPEG_RGA_TO_MPP", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "RGA 转换后直接送 MPP 编码（dma-buf 链路）"},
    {"WEBRTC_MJPEG_DEC_LOW_LATENCY", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG 解码器低延迟模式（同步、关池化）"},
    {"WEBRTC_MJPEG_RGA_DISABLE_AFTER_FAIL", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 1, 0, 1, "",
     "RGA 失败后回退 CPU 路径并禁用 RGA"},
    {"WEBRTC_MJPEG_RGA_MAX_ASPECT", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kInt, 4, 1, 32, "",
     "允许 RGA 处理的最大宽高比（避免极端拉伸）"},

    // ---------- Media pipeline ------------------------------------------
    {"WEBRTC_NV12_POOL_SLOTS", nullptr, KnobGroup::kMediaPipeline, KnobKind::kInt, 4, 2, 32, "",
     "NV12 帧池 slot 数"},
    {"WEBRTC_V4L2_BUFFER_COUNT", nullptr, KnobGroup::kMediaPipeline, KnobKind::kInt, 2, 2, 32, "",
     "V4L2 mmap buffer 数"},
    {"WEBRTC_V4L2_POLL_TIMEOUT_MS", nullptr, KnobGroup::kMediaPipeline, KnobKind::kInt, 5, 0, 5000, "",
     "V4L2 poll 超时毫秒"},
    {"WEBRTC_DUAL_MPP_MJPEG_H264", nullptr, KnobGroup::kMediaPipeline, KnobKind::kBool, 1, 0, 1, "",
     "Rockchip 双 MPP（MJPEG 解 + H264 编）协同模式"},
    {"WEBRTC_PUSH_OUTBOUND_STATS_INTERVAL_SEC", nullptr, KnobGroup::kMediaPipeline, KnobKind::kInt, 0, 0, 60, "",
     "推流端 outbound RTC stats 周期日志间隔（秒；0=关闭）"},
    {"WEBRTC_SKIP_LOOPBACK_RECV", nullptr, KnobGroup::kMediaPipeline, KnobKind::kBool, 0, 0, 1, "",
     "压测：推流端跳过 loopback recv，节省回环 CPU"},

    // ---------- Media dump ----------------------------------------------
    {"WEBRTC_DUMP_OFFER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "推流端 dump 本地 Offer SDP 到 stderr"},
    {"WEBRTC_DUMP_LOCAL_ANSWER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "拉流端 dump 本地 Answer SDP 到 stderr"},
    {"WEBRTC_DUMP_REMOTE_OFFER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "拉流端 dump 远端 Offer SDP 到 stderr"},

    // ---------- Codec backend on/off ------------------------------------
    {"WEBRTC_DISABLE_MPP_H264", nullptr, KnobGroup::kCodecBackend, KnobKind::kBool, 0, 0, 1, "",
     "运行时禁用 MPP H.264 编码 backend，回退 builtin"},
    {"WEBRTC_DISABLE_MPP_H264_DECODE", nullptr, KnobGroup::kCodecBackend, KnobKind::kBool, 0, 0, 1, "",
     "运行时禁用 MPP H.264 解码 backend，回退 builtin"},

    // ---------- MPP encoder tuning (rockchip) ---------------------------
    {"WEBRTC_MPP_ENC_HOR_STRIDE_ALIGN", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 16, 1, 256, "",
     "MPP 编码水平 stride 对齐"},
    {"WEBRTC_MPP_ENC_GOP", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 600, "",
     "MPP 编码 GOP（0=auto）"},
    {"WEBRTC_MPP_ENC_INTRA_REFRESH_MODE", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 3, "",
     "MPP intra refresh 模式（0=off）"},
    {"WEBRTC_MPP_ENC_INTRA_REFRESH_ARG", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 1, 512, "",
     "MPP intra refresh 参数"},
    {"WEBRTC_MPP_ENC_SPLIT_BYTES", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 4096, "",
     "MPP 编码切片字节数（slice splitting）"},
    {"WEBRTC_MPP_ENC_IDR_MIN_INTERVAL_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 800, 0, 5000, "",
     "强制 IDR 之间最小间隔毫秒"},
    {"WEBRTC_MPP_ENC_IDR_LOSS_QUICK_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 180, 0, 2000, "",
     "丢包后快速触发 IDR 的窗口毫秒"},
    {"WEBRTC_MPP_ENC_IDR_FORCE_MAX_WAIT_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 3000, 200, 15000, "",
     "强制 IDR 命令最大等待毫秒"},
    {"WEBRTC_MPP_ENC_RECOVER_SOFT_FAILS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 6, 1, 200, "",
     "软失败次数阈值"},
    {"WEBRTC_MPP_ENC_RECOVER_HARD_FAILS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 30, 2, 500, "",
     "硬失败次数阈值"},
    {"WEBRTC_MPP_ENC_RECOVER_DISABLE_SPLIT", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "失败次数过多时禁用 split 模式"},
    {"WEBRTC_MPP_ENC_DEBUG", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "MPP 编码 debug 日志"},
    {"WEBRTC_MPP_ENC_USE_SYNC", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "MPP 编码同步模式"},
    {"WEBRTC_MPP_ENC_USE_TASK", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "MPP 编码 task 模式"},
    {"WEBRTC_MPP_ENC_TASK_READ_PACKET", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "task 模式下主动 read packet"},
    {"WEBRTC_MPP_ENC_NATIVE_ZERO_COPY", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "MPP 编码原生零拷贝路径"},
    {"WEBRTC_MPP_ENC_NATIVE_ZERO_COPY_STRICT", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "零拷贝严格模式（失败不回退）"},
    {"WEBRTC_MPP_ENC_NATIVE_ZERO_COPY_FAILS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 3, 1, 50, "",
     "零拷贝连续失败回退阈值"},
    {"WEBRTC_MPP_ENC_TRACE_EVERY_N", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 45, 1, 600, "",
     "MPP 编码 trace 每 N 帧一行"},
    {"WEBRTC_MPP_ENC_OUTPUT_TIMEOUT_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 5000, "",
     "MPP 编码 output 超时毫秒（0=驱动默认）"},
    {"WEBRTC_MPP_ENC_INPUT_TIMEOUT_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 5000, "",
     "MPP 编码 input 超时毫秒（0=驱动默认）"},
    {"WEBRTC_MPP_ENC_FORCE_NORMAL_BUF", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "强制 normal buffer（debug：禁用 ext buffer）"},
    {"WEBRTC_MPP_ENC_ENABLE_IDR_CTRL", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "启用 MPP CFG IDR 控制（实验）"},
    {"WEBRTC_MPP_ENC_PACKET_RETRY", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 6, 0, 100, "",
     "sync/task 模式下 packet read 重试次数"},
    {"WEBRTC_MPP_ENC_PACKET_RETRY_SLEEP_US", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 500, 50, 10000, "",
     "packet 重试间隔微秒"},
    {"WEBRTC_MPP_ENC_PACKET_POLL_BLOCK", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "packet poll 阻塞模式"},

    // ---------- MPP decoder tuning (rockchip) ---------------------------
    {"WEBRTC_MPP_H264_DEC_LOW_LATENCY", nullptr, KnobGroup::kMppDecoder, KnobKind::kBool, 0, 0, 1, "",
     "MPP H264 解码低延迟模式"},
    {"WEBRTC_MPP_H264_DEC_POLL_TIMEOUT_MS", nullptr, KnobGroup::kMppDecoder, KnobKind::kInt, 0, 0, 5000, "",
     "MPP H264 解码 poll 超时毫秒（0=驱动默认）"},
};

constexpr std::size_t kKnobCount = sizeof(kKnobTable) / sizeof(kKnobTable[0]);

const char* GroupName(KnobGroup g) {
    switch (g) {
        case KnobGroup::kSignal:        return "signal";
        case KnobGroup::kService:       return "service";
        case KnobGroup::kClient:        return "client";
        case KnobGroup::kRtcFactory:    return "rtc_factory";
        case KnobGroup::kMediaTrace:    return "media.trace";
        case KnobGroup::kMediaCapture:  return "media.capture";
        case KnobGroup::kMediaMjpeg:    return "media.mjpeg";
        case KnobGroup::kMediaPipeline: return "media.pipeline";
        case KnobGroup::kMediaDump:     return "media.dump";
        case KnobGroup::kCodecBackend:  return "codec.backend";
        case KnobGroup::kMppEncoder:    return "mpp.encoder";
        case KnobGroup::kMppDecoder:    return "mpp.decoder";
    }
    return "unknown";
}

const char* KindName(KnobKind k) {
    switch (k) {
        case KnobKind::kBool:   return "bool";
        case KnobKind::kInt:    return "int";
        case KnobKind::kSize:   return "size";
        case KnobKind::kString: return "string";
    }
    return "unknown";
}

const KnobSpec* FindSpec(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kKnobCount; ++i) {
        if (std::strcmp(kKnobTable[i].name, name) == 0) {
            return &kKnobTable[i];
        }
    }
    return nullptr;
}

// alias 命中（spec.alias != name 主名时）的"已警告过"集合，避免日志洪水。
std::mutex                      g_alias_mu;
std::unordered_set<std::string> g_alias_warned;

void MaybeWarnAliasUsed(const KnobSpec& spec, const char* used_name) {
    if (!spec.alias) return;
    if (std::strcmp(used_name, spec.alias) != 0) return;
    std::lock_guard<std::mutex> lk(g_alias_mu);
    if (g_alias_warned.insert(used_name).second) {
        RFLOW_CORE_LOGW("[knobs] env %s is deprecated; please use %s instead", used_name,
                        spec.name);
    }
}

// 读环境变量：先 spec.name，再 spec.alias，命中 alias 打 deprecation。
const char* ResolveEnv(const KnobSpec& spec, const char** out_used_name = nullptr) {
    if (const char* v = std::getenv(spec.name); v && v[0]) {
        if (out_used_name) *out_used_name = spec.name;
        return v;
    }
    if (spec.alias) {
        if (const char* v = std::getenv(spec.alias); v && v[0]) {
            if (out_used_name) *out_used_name = spec.alias;
            return v;
        }
    }
    if (out_used_name) *out_used_name = nullptr;
    return nullptr;
}

bool ParseBool(const char* v, bool fallback) {
    if (!v || !v[0]) return fallback;
    char c = v[0];
    if (c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T') return true;
    if (c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F') return false;
    return fallback;
}

long ParseLong(const char* v, long fallback, long min_v, long max_v) {
    if (!v || !v[0]) return fallback;
    char* end = nullptr;
    long  parsed = std::strtol(v, &end, 10);
    if (end == v || (end && *end != '\0')) return fallback;
    if (parsed < min_v) return min_v;
    if (parsed > max_v) return max_v;
    return parsed;
}

unsigned long long ParseULL(const char* v, unsigned long long fallback,
                            unsigned long long min_v, unsigned long long max_v) {
    if (!v || !v[0]) return fallback;
    char*              end = nullptr;
    unsigned long long parsed = std::strtoull(v, &end, 10);
    if (end == v || (end && *end != '\0')) return fallback;
    if (parsed < min_v) return min_v;
    if (parsed > max_v) return max_v;
    return parsed;
}

}  // namespace

int ReadInt(const char* name) {
    const KnobSpec* spec = FindSpec(name);
    if (!spec) {
        // 未注册 knob：仍允许读，但只用 ReadEnvIntInRange 默认 fallback
        return rflow::common::base::ReadEnvIntInRange(name, 0, INT32_MIN, INT32_MAX);
    }
    const char* used = nullptr;
    const char* v    = ResolveEnv(*spec, &used);
    MaybeWarnAliasUsed(*spec, used ? used : "");
    return static_cast<int>(
        ParseLong(v, spec->default_int, spec->min_int, spec->max_int));
}

bool ReadBool(const char* name) {
    const KnobSpec* spec = FindSpec(name);
    if (!spec) {
        return rflow::common::base::ReadEnvBool(name, false);
    }
    const char* used = nullptr;
    const char* v    = ResolveEnv(*spec, &used);
    MaybeWarnAliasUsed(*spec, used ? used : "");
    return ParseBool(v, spec->default_int != 0);
}

size_t ReadSize(const char* name) {
    const KnobSpec* spec = FindSpec(name);
    if (!spec) {
        return rflow::common::base::ReadEnvSizeInRange(name, 0, 0, SIZE_MAX);
    }
    const char* used = nullptr;
    const char* v    = ResolveEnv(*spec, &used);
    MaybeWarnAliasUsed(*spec, used ? used : "");
    return static_cast<size_t>(ParseULL(v,
                                        static_cast<unsigned long long>(spec->default_int),
                                        static_cast<unsigned long long>(spec->min_int),
                                        static_cast<unsigned long long>(spec->max_int)));
}

std::string ReadString(const char* name) {
    const KnobSpec* spec = FindSpec(name);
    const char*     v    = nullptr;
    const char*     used = nullptr;
    if (spec) {
        v = ResolveEnv(*spec, &used);
        MaybeWarnAliasUsed(*spec, used ? used : "");
    } else {
        v = std::getenv(name);
    }
    if (v && v[0]) return std::string(v);
    if (spec && spec->default_str) return std::string(spec->default_str);
    return {};
}

namespace {

std::string FormatCurrentValue(const KnobSpec& spec) {
    const char* used = nullptr;
    const char* env  = ResolveEnv(spec, &used);
    std::ostringstream oss;
    switch (spec.kind) {
        case KnobKind::kBool: {
            bool v = ParseBool(env, spec.default_int != 0);
            oss << (v ? "true" : "false");
            break;
        }
        case KnobKind::kInt: {
            long v = ParseLong(env, spec.default_int, spec.min_int, spec.max_int);
            oss << v;
            break;
        }
        case KnobKind::kSize: {
            unsigned long long v = ParseULL(env,
                                            static_cast<unsigned long long>(spec.default_int),
                                            static_cast<unsigned long long>(spec.min_int),
                                            static_cast<unsigned long long>(spec.max_int));
            oss << v;
            break;
        }
        case KnobKind::kString: {
            const char* def = spec.default_str ? spec.default_str : "";
            oss << '"' << (env && env[0] ? env : def) << '"';
            break;
        }
    }
    return oss.str();
}

bool IsNonDefault(const KnobSpec& spec) {
    const char* env = ResolveEnv(spec);
    if (!env || !env[0]) return false;
    switch (spec.kind) {
        case KnobKind::kBool:
            return ParseBool(env, spec.default_int != 0) != (spec.default_int != 0);
        case KnobKind::kInt:
            return ParseLong(env, spec.default_int, spec.min_int, spec.max_int) != spec.default_int;
        case KnobKind::kSize:
            return ParseULL(env,
                            static_cast<unsigned long long>(spec.default_int),
                            static_cast<unsigned long long>(spec.min_int),
                            static_cast<unsigned long long>(spec.max_int)) !=
                   static_cast<unsigned long long>(spec.default_int);
        case KnobKind::kString: {
            const char* def = spec.default_str ? spec.default_str : "";
            return std::strcmp(env, def) != 0;
        }
    }
    return false;
}

}  // namespace

void DumpToLog() {
    static std::atomic<bool> dumped{false};
    if (dumped.exchange(true)) return;  // 仅打一次

    int non_default = 0;
    for (std::size_t i = 0; i < kKnobCount; ++i) {
        if (IsNonDefault(kKnobTable[i])) ++non_default;
    }
    RFLOW_CORE_LOGI("[knobs] runtime knobs total=%zu non-default=%d",
                    kKnobCount, non_default);

    if (non_default == 0) return;
    for (std::size_t i = 0; i < kKnobCount; ++i) {
        const KnobSpec& s = kKnobTable[i];
        if (!IsNonDefault(s)) continue;
        const char* used = nullptr;
        ResolveEnv(s, &used);
        const std::string val   = FormatCurrentValue(s);
        const char*       group = GroupName(s.group);
        if (used && s.alias && std::strcmp(used, s.alias) == 0) {
            RFLOW_CORE_LOGI("[knobs]   %s.%s = %s   [via alias %s, deprecated]",
                            group, s.name, val.c_str(), used);
        } else {
            RFLOW_CORE_LOGI("[knobs]   %s.%s = %s", group, s.name, val.c_str());
        }
    }
}

std::string DumpAsMarkdown() {
    std::ostringstream oss;
    oss << "# libRoboFlow Runtime Knobs\n\n";
    oss << "本表由 `core/runtime/runtime_knobs.cpp` 中 `kKnobTable` 自动生成（"
        << kKnobCount << " 项）。\n\n";

    KnobGroup last_group = static_cast<KnobGroup>(255);
    for (std::size_t i = 0; i < kKnobCount; ++i) {
        const KnobSpec& s = kKnobTable[i];
        if (s.group != last_group) {
            oss << "\n## " << GroupName(s.group) << "\n\n";
            oss << "| name | kind | default | range | alias | doc |\n";
            oss << "|------|------|---------|-------|-------|-----|\n";
            last_group = s.group;
        }
        oss << "| `" << s.name << "` | " << KindName(s.kind) << " | ";
        if (s.kind == KnobKind::kString) {
            oss << "\"" << (s.default_str ? s.default_str : "") << "\"";
        } else {
            oss << s.default_int;
        }
        oss << " | ";
        if (s.kind == KnobKind::kInt || s.kind == KnobKind::kSize) {
            oss << "[" << s.min_int << "," << s.max_int << "]";
        } else {
            oss << "-";
        }
        oss << " | " << (s.alias ? s.alias : "-") << " | " << (s.doc ? s.doc : "") << " |\n";
    }
    return oss.str();
}

KnobView GetKnobTable() {
    return {kKnobTable, kKnobCount};
}

}  // namespace rflow::core::runtime
