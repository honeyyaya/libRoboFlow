#ifndef __RFLOW_CORE_RTC_ICE_PENDING_ICE_BUFFER_H__
#define __RFLOW_CORE_RTC_ICE_PENDING_ICE_BUFFER_H__

// 远端 ICE candidate 在 SetRemoteDescription 之前到达时，需要先缓存，
// 等 SetRemoteDescription 成功后再统一 AddIceCandidate（webrtc 要求顺序）。
//
// 这里把"线程安全的入队 + applied flag + drain"封装成一个小工具，避免在
// 业务类（client::RtcStreamSession 等）中重复实现。
//
// 仅依赖 C++ 标准库，故做成 header-only，可在任意 .cpp 中引用。

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rflow::core::rtc {

struct PendingIce {
    std::string mid;
    int         mline_index = 0;
    std::string candidate;
};

class PendingIceBuffer {
 public:
    PendingIceBuffer() = default;

    PendingIceBuffer(const PendingIceBuffer&) = delete;
    PendingIceBuffer& operator=(const PendingIceBuffer&) = delete;

    // 远端 SDP 已经成功应用？
    bool IsApplied() const noexcept {
        return applied_.load(std::memory_order_acquire);
    }

    // 重置为"远端尚未应用"，并清空已缓存（用于 Close / 新 offer / SetRemote 失败）。
    void ResetUnapplied() {
        applied_.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu_);
        pending_.clear();
    }

    // 仅清空缓存，不变更 applied。
    void Clear() {
        std::lock_guard<std::mutex> lk(mu_);
        pending_.clear();
    }

    // 入队一个 candidate。调用方应在 IsApplied() == false 时调用；
    // 返回入队后 buffer 大小（便于打印日志）。
    std::size_t Push(std::string mid, int mline_index, std::string candidate) {
        std::lock_guard<std::mutex> lk(mu_);
        pending_.push_back(PendingIce{std::move(mid), mline_index, std::move(candidate)});
        return pending_.size();
    }

    // 标记远端已应用，并把已缓存的全部取出（一次原子语义）。
    std::vector<PendingIce> MarkAppliedAndDrain() {
        applied_.store(true, std::memory_order_release);
        std::vector<PendingIce> out;
        std::lock_guard<std::mutex> lk(mu_);
        out.swap(pending_);
        return out;
    }

    // 当前缓存条数（粗略，仅用于日志）。
    std::size_t SizeApprox() const {
        std::lock_guard<std::mutex> lk(mu_);
        return pending_.size();
    }

 private:
    mutable std::mutex      mu_;
    std::vector<PendingIce> pending_;
    std::atomic<bool>       applied_{false};
};

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_ICE_PENDING_ICE_BUFFER_H__
