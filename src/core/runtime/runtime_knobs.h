/**
 * @file   core/runtime/runtime_knobs.h
 * @brief  集中管理 SDK 运行期可调参数（环境变量）的注册表 / 文档 / 启动 dump。
 *
 * 设计目标：
 *   1. 集中：所有 RFLOW_* / WEBRTC_* 风格的运行期 knob 都登记在 KnobSpec 表中，
 *      消除"哪些可调、合法范围、默认值"散落各文件、无单一文档的问题。
 *   2. 旁路兼容：本注册表不替换业务代码里现有的 ReadEnvIntInRange / std::getenv 调用；
 *      它们依然可用且语义不变。注册表只是一个"权威清单 + 启动观测"。
 *   3. 命名一致：业务代码逐步迁移到 ReadInt/ReadBool/ReadSize 接口；新接口先查
 *      RFLOW_* 名，缺失再回落到 WEBRTC_* 别名（一次性 deprecation 日志）。
 *   4. 启动可观测：lib init 末尾打一次 RuntimeKnobs::DumpToLog()，包含每个 knob
 *      的当前值（与默认值不同的会高亮 NON-DEFAULT 标记），事故复现一目了然。
 *   5. 文档自动生成：DumpAsMarkdown() 可写出 docs/RUNTIME_KNOBS.md。
 *
 * Thread safety：
 *   - 注册表内部使用 mutex；ReadInt/Bool/Size 与 DumpToLog 可并发调用。
 *   - KnobSpec 表是 program-wide 的 const 数据，无运行时变更。
 */

#ifndef __RFLOW_CORE_RUNTIME_RUNTIME_KNOBS_H__
#define __RFLOW_CORE_RUNTIME_RUNTIME_KNOBS_H__

#include <cstddef>
#include <cstdint>
#include <string>

namespace rflow::core::runtime {

// 分组用于在 dump 时聚类显示，不影响行为。
enum class KnobGroup : uint8_t {
    kSignal,             // 信令调试 / 追踪
    kService,            // service 端业务默认值 / degradation
    kClient,             // client 端业务默认值（暂未使用）
    kRtcFactory,         // PeerConnectionFactory / FieldTrials / FlexFEC 等
    kMediaTrace,         // 媒体延迟 / E2E 追踪开关
    kMediaCapture,       // V4L2 / 线程调度
    kMediaMjpeg,         // MJPEG 解码路径调优
    kMediaPipeline,      // NV12 池 / 队列 / RGA 等通用管线
    kMediaDump,          // SDP / Offer / Answer dump
    kCodecBackend,       // 选 builtin / mpp / mediacodec
    kMppEncoder,         // Rockchip MPP H.264 编码器调优
    kMppDecoder,         // Rockchip MPP H.264 / MJPEG 解码器调优
};

enum class KnobKind : uint8_t {
    kBool,
    kInt,
    kSize,
    kString,
};

// KnobSpec：编译期常量描述，集中登记在 runtime_knobs.cpp 的 kKnobTable 里。
struct KnobSpec {
    const char* name;             // 主名（推荐 RFLOW_* 前缀）
    const char* alias;             // 老名（可为 nullptr；多用于 WEBRTC_* → RFLOW_* 兼容）
    KnobGroup   group;
    KnobKind    kind;
    long        default_int;       // kBool/kInt/kSize 用；kString 写 0
    long        min_int;
    long        max_int;
    const char* default_str;       // 仅 kString 用；可为空字符串
    const char* doc;               // 一行说明，用于 dump / 文档
};

// 主入口：业务代码读 knob 用这些函数。
//
// 调用语义：
//   - 优先读 spec.name；若无则读 spec.alias（并标记触发 deprecation）；
//   - 不在 KnobSpec 表中的 name 也允许调用（按"未注册 knob"统计），但会发一次 warning；
//   - 解析失败 / 越界 → 返回 default 或 clamp 到 [min,max]，与 ReadEnvIntInRange 一致。
//
// Note：KnobSpec 通过 name 全局唯一定位；alias 只是 fallback，不允许两个 spec 共享 name。
int    ReadInt   (const char* name);
bool   ReadBool  (const char* name);
size_t ReadSize  (const char* name);

// String knob 直接返回拷贝（C++ string）；未设置时返回 spec.default_str 或空串。
std::string ReadString(const char* name);

// 启动时 dump 到 SDK logger。线程安全；可重复调用，仅首次会触发 alias 兼容性 warning。
void DumpToLog();

// 把当前快照导出为 Markdown 文本（用于自动生成 docs/RUNTIME_KNOBS.md）。
std::string DumpAsMarkdown();

// 测试用：Knob 表迭代（const 引用）。test 不应直接修改。
struct KnobView {
    const KnobSpec* specs;
    std::size_t     count;
};
KnobView GetKnobTable();

}  // namespace rflow::core::runtime

#endif  // __RFLOW_CORE_RUNTIME_RUNTIME_KNOBS_H__
