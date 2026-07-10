#include "runtime/runtime_knobs.h"

#include "base/env_reader.h"
#include "base/logging.h"

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
     "Verbose signaling client logs (e.g. register_json)."},
    {"RFLOW_SIGNALING_TIMING_TRACE", nullptr, KnobGroup::kSignal, KnobKind::kBool, 0, 0, 1, "",
     "Timing trace for signaling phases (offer, answer, ICE)."},

    // ---------- RTC factory ----------------------------------------------
    {"RFLOW_ZERO_PLAYOUT_MIN_PACING_MS", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 2, 0, 20, "",
     "Jitter buffer minimum playout pacing (ms); 0 = aggressive low latency."},
    {"RFLOW_MAX_DECODE_QUEUE_SIZE", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 3, 2, 16, "",
     "Decoder queue depth limit; smaller = lower latency, higher drop risk."},
    {"RFLOW_ENABLE_DECODE_QUEUE_GUARD", nullptr, KnobGroup::kRtcFactory, KnobKind::kBool, 0, 0, 1, "",
     "When enabled, tighten decode queue caps under low pacing to drop backlog."},
    {"RFLOW_DECODE_QUEUE_GUARD_CAP", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 6, 4, 12, "",
     "Max decode-queue size when RFLOW_ENABLE_DECODE_QUEUE_GUARD applies."},
    {"RFLOW_ENABLE_FLEXFEC", nullptr, KnobGroup::kRtcFactory, KnobKind::kBool, 1, 0, 1, "",
     "RTP FlexFEC (FlexFEC-03): effective when librflow_global_config.flexfec stays "
     "RFLOW_GLOBAL_FLEXFEC_DEFAULT; set_flexfec(ON/OFF) overrides this knob."},
    {"RFLOW_ICE_IGNORE_INTERFACES", nullptr, KnobGroup::kRtcFactory, KnobKind::kString, 0, 0, 0, "",
     "Comma-separated NIC names ignored by WebRTC ICE (e.g. eth0). Overridden by "
     "librflow_global_config_set_ice_ignore_interfaces when explicitly set."},
    {"RFLOW_DECODER_BACKEND", nullptr, KnobGroup::kRtcFactory, KnobKind::kString, 0, 0, 0, "",
     "Pull/client default H264 decoder: mpp|rockchip (default on RK) or builtin|ffmpeg."},
    {"RFLOW_BUILTIN_DECODE_RECOVERY", "RFLOW_FFMPEG_H264_DECODE_RECOVERY", KnobGroup::kRtcFactory,
     KnobKind::kBool, 1, 0, 1, "",
     "Wrap builtin software decoders with stall/error recovery."},
    {"RFLOW_BUILTIN_DECODE_STALL_FRAMES", "RFLOW_FFMPEG_H264_DECODE_STALL_FRAMES", KnobGroup::kRtcFactory,
     KnobKind::kInt, 45, 5, 600, "",
     "Reset software decoder after N Decode calls with no output."},
    {"RFLOW_H264_DECODE_RESET_FRAME_COUNT", "RFLOW_FFMPEG_H264_DECODE_RESET_FRAME_COUNT",
     KnobGroup::kRtcFactory, KnobKind::kInt, 2800, 500, 100000, "",
     "H264-only: proactive reset on keyframe before FFmpeg frame_num wrap (~3000)."},
    {"RFLOW_FIELD_TRIALS_APPEND", nullptr, KnobGroup::kRtcFactory, KnobKind::kString, 0, 0, 0, "",
     "Extra WebRTC FieldTrials string (experimental/runtime flags)."},
    {"RFLOW_WEBRTC_LOG_SEVERITY", nullptr, KnobGroup::kRtcFactory, KnobKind::kString, 0, 0, 0, "",
     "WebRTC internal RTC_LOG min level: verbose|info|warning|error|none "
     "(unset: none in Release, warning in Debug)."},
    {"RFLOW_SYNC_GETSTATS_TIMEOUT_MS", nullptr, KnobGroup::kRtcFactory, KnobKind::kInt, 1500, 200, 60000,
     "",
     "Blocking wait timeout for PeerConnection::GetStats (ms) in QoS collection."},

    // ---------- Media trace ----------------------------------------------
    {"RFLOW_LATENCY_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "Per-stage latency tracing on publisher (capture, encode, send, …)."},
    {"RFLOW_E2E_LATENCY_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "End-to-end latency trace stitched across publisher and subscriber (trace_id)."},
    {"RFLOW_MEDIA_TIMING_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "Internal timing traces inside Rockchip MPP encoder/decoder."},
    {"RFLOW_MEDIA_TIMING_TRACE_EVERY_N", nullptr, KnobGroup::kMediaTrace, KnobKind::kInt, 60, 1, 600, "",
     "Log one MEDIA_TIMING_TRACE line every N frames."},
    {"RFLOW_MJPEG_TO_H264_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "Trace dedicated to MJPEG → H264 path."},
    {"RFLOW_MJPEG_DEC_TRACE", nullptr, KnobGroup::kMediaTrace, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG decoder internal trace."},

    // ---------- Media capture / scheduling -------------------------------
    {"RFLOW_MEDIA_THREAD_SCHED", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "Media worker scheduling: rr | fifo | nice."},
    {"RFLOW_MEDIA_THREAD_RR_PRIO", nullptr, KnobGroup::kMediaCapture, KnobKind::kInt, 20, 1, 90, "",
     "SCHED_RR priority when using rr scheduler."},
    {"RFLOW_MEDIA_THREAD_NICE", nullptr, KnobGroup::kMediaCapture, KnobKind::kInt, -8, -20, 19, "",
     "Process nice value when SCHED=nice."},
    {"RFLOW_MJPEG_DECODE_CPU", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "CPU affinity mask for MJPEG decode thread (taskset-style hex or CPU id)."},
    {"RFLOW_V4L2_CAPTURE_CPU", nullptr, KnobGroup::kMediaCapture, KnobKind::kString, 0, 0, 0, "",
     "CPU affinity for V4L2 capture thread."},

    // ---------- Media MJPEG ---------------------------------------------
    {"RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kInt, 25, 0, 5000, "",
     "Max wait (ms) in MJPEG decode queue; expires → drop frame. Unset env: auto 25ms @30fps, ~3 frames @60fps."},
    {"RFLOW_CAPTURE_FPS_PIPELINE", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kString, 0, 0, 0, "auto",
     "Capture preset: auto|low_latency|high_fidelity (30 vs 60 fps queue depth)."},
    {"RFLOW_MJPEG_ZERO_COPY_TO_ENC", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "Allow MJPEG→MPP H264 zero-copy path when policy permits."},
    {"RFLOW_MJPEG_DECODE_INLINE", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "Run MJPEG decode on capture thread (skip decode-queue hop)."},
    {"RFLOW_MJPEG_DEC_LOW_LATENCY", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kBool, 0, 0, 1, "",
     "MJPEG decoder low-latency mode (sync, less buffering)."},
    {"RFLOW_MJPEG_RGA_MAX_ASPECT", nullptr, KnobGroup::kMediaMjpeg, KnobKind::kInt, 4, 1, 32, "",
     "Max width/height ratio handled by RGA (avoid extreme stretch)."},

    // ---------- Media pipeline ------------------------------------------
    {"RFLOW_PUSH_OUTBOUND_STATS_INTERVAL_SEC", nullptr, KnobGroup::kMediaPipeline, KnobKind::kInt, 0, 0, 60, "",
     "Publisher periodic outbound RTP stats log interval (s); 0 = off."},
    {"RFLOW_SKIP_LOOPBACK_RECV", nullptr, KnobGroup::kMediaPipeline, KnobKind::kBool, 0, 0, 1, "",
     "Benchmark only: skip loopback receive path on publisher to save CPU."},

    // ---------- Media dump ----------------------------------------------
    {"RFLOW_DUMP_OFFER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "Publisher: dump local Offer SDP to log."},
    {"RFLOW_DUMP_LOCAL_ANSWER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "Subscriber: dump local Answer SDP to log."},
    {"RFLOW_DUMP_REMOTE_OFFER", nullptr, KnobGroup::kMediaDump, KnobKind::kBool, 0, 0, 1, "",
     "Subscriber: dump remote Offer SDP to log."},

    // ---------- MPP encoder tuning (rockchip) ---------------------------
    {"RFLOW_MPP_ENC_HOR_STRIDE_ALIGN", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 16, 1, 256, "",
     "MPP encoder horizontal stride alignment (bytes/pixels per driver)."},
    {"RFLOW_MPP_ENC_INTRA_REFRESH_MODE", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 3, "",
     "MPP intra refresh mode (0 = disabled)."},
    {"RFLOW_MPP_ENC_INTRA_REFRESH_ARG", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 1, 512, "",
     "MPP intra refresh strength / period parameter."},
    {"RFLOW_MPP_ENC_SPLIT_BYTES", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 0, 0, 4096, "",
     "Slice split size for MPP encoder (0 = disable splitting)."},
    {"RFLOW_MPP_ENC_IDR_MIN_INTERVAL_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 800, 0, 5000, "",
     "Minimum milliseconds between encoder IDR/keyframe requests."},
    {"RFLOW_MPP_ENC_IDR_LOSS_QUICK_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 180, 0, 2000, "",
     "Window (ms) to request fast IDR after packet loss is detected."},
    {"RFLOW_MPP_ENC_IDR_FORCE_MAX_WAIT_MS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 3000, 200, 15000, "",
     "Max wait (ms) when forcing encoder IDR/output drain."},
    {"RFLOW_MPP_ENC_DEBUG", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "Verbose MPP encoder debug logs."},
    {"RFLOW_MPP_ENC_USE_SYNC", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "Use synchronous encode API instead of polled/task path."},
    {"RFLOW_MPP_ENC_USE_TASK", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "Run MPP encode as async task-queue mode."},
    {"RFLOW_MPP_ENC_TASK_READ_PACKET", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "In task mode, actively dequeue encoded packets (vs event-only)."},
    {"RFLOW_MPP_ENC_NATIVE_ZERO_COPY", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 1, 0, 1, "",
     "Prefer hardware zero-copy dmabuf encode path."},
    {"RFLOW_MPP_ENC_NATIVE_ZERO_COPY_STRICT", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "Zero-copy strict: never fall back after failure."},
    {"RFLOW_MPP_ENC_NATIVE_ZERO_COPY_FAILS", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 3, 1, 50, "",
     "Consecutive zero-copy failures before falling back."},
    {"RFLOW_MPP_ENC_TRACE_EVERY_N", nullptr, KnobGroup::kMppEncoder, KnobKind::kInt, 120, 1, 600, "",
     "Emit MPP encoder trace line every N frames."},
    {"RFLOW_MPP_ENC_ENABLE_IDR_CTRL", nullptr, KnobGroup::kMppEncoder, KnobKind::kBool, 0, 0, 1, "",
     "Experimental MPP CFG control for encoder IDR."},
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
