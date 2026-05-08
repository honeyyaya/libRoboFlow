/**
 * @file   timing_log.h
 * @brief  统一的耗时/流水线日志开关。
 *
 * 受控的日志组：
 *   - rtc_stream_session 中的 1 Hz 流水线统计行（[Pipeline/Video] /
 *     [Pipeline/Latency] / [Pipeline/Codec] / [Pipeline/Net]）。
 *   - mediacodec_video_decoder 等硬件解码路径中按 tracking_id 采样的
 *     [Timing/Decode] 单帧耗时分析行。
 *
 * 默认全部关闭。开启方式（任选其一，须在**应用进程启动前**生效，见下）：
 *
 * 1) 环境变量（桌面 Linux / 部分自研启动流程会继承给子进程）：
 *        RFLOW_LOG_TIMING=1
 *    注意：在 adb 里对普通「从桌面启动」的 App 执行 `export`，**不会**
 *    进 zygote fork 出来的进程，因此 **getenv 往往读不到**。
 *
 * 2) Android：系统属性（推荐用 adb 试）：
 *        adb shell setprop debug.rflow.log_timing 1
 *    需在**冷启动应用前**设置；部分正式版 ROM 对非白名单 debug 属性有
 *    限制，userdebug/eng 设备或 root 更容易成功。关闭：
 *        adb shell setprop debug.rflow.log_timing 0
 *
 * 3) 宿主 App：在 System.loadLibrary native 之前调用
 *    `android.os.Os.setenv("RFLOW_LOG_TIMING", "1", true)`（API 21+），
 *    可把开关写进进程环境，供本头文件 getenv 路径读取。
 *
 * 仅进程内第一次 IsEnabled() 时解析一次；之后为无锁静态读。
 */
#ifndef __RFLOW_INTERNAL_TIMING_LOG_H__
#define __RFLOW_INTERNAL_TIMING_LOG_H__

#include <cstdint>
#include <cstdlib>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace rflow::timing_log {

namespace {

inline bool IsTruthyTimingToken(const char* v) {
    if (!v || !v[0]) return false;
    const char c = v[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T' || c == 'o' || c == 'O';
}

inline bool ReadTimingSwitchOnce() {
    if (IsTruthyTimingToken(std::getenv("RFLOW_LOG_TIMING"))) {
        return true;
    }
#if defined(__ANDROID__)
    // 与 adb：`setprop debug.rflow.log_timing 1` 对齐；不依赖 shell 的 export。
    char prop_buf[PROP_VALUE_MAX] = {};
    if (__system_property_get("debug.rflow.log_timing", prop_buf) > 0) {
        return IsTruthyTimingToken(prop_buf);
    }
#endif
    return false;
}

}  // namespace

inline bool IsEnabled() {
    static const bool enabled = ReadTimingSwitchOnce();
    return enabled;
}

// 与发送侧 VideoFrameTrackingId 采样规则保持一致：
// 仅当总开关启用且 (tracking_id % period) == 0 时为采样帧。
// period 默认 120：60fps 下约每 2 秒打一次单帧耗时分析，足以观察长期趋势又不刷屏。
inline bool ShouldSampleByTrackingId(uint32_t tracking_id, uint32_t period = 120u) {
    if (!IsEnabled() || period == 0u) return false;
    return (tracking_id % period) == 0u;
}

}  // namespace rflow::timing_log

#endif  // __RFLOW_INTERNAL_TIMING_LOG_H__
