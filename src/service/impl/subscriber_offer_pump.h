#ifndef __RFLOW_SERVICE_IMPL_SUBSCRIBER_OFFER_PUMP_H__
#define __RFLOW_SERVICE_IMPL_SUBSCRIBER_OFFER_PUMP_H__

// Service Publisher 内部使用的 subscriber → CreateOffer 排队 pump：
// 信令 IO 线程在 OnSubscriberJoin 中只把 peer_id 入队，由进程级共享线程池
// （rflow::thread::post）在 *单飞* 模式下串行 dispatch（典型场景下调用
// streamer_->CreateOfferForPeer）。
//
// 与原有"常驻 worker 线程"实现等价的对外契约（已被 test_subscriber_offer_pump 覆盖）：
//   - Start(on_dispatch): 至多调用一次。on_dispatch 在 thread_pool worker 上下文中
//                        被调用，必须自洽，不能假设特定线程亲和。
//   - Enqueue(peer_id) : 任何线程可调用；多次入队按入队顺序串行 dispatch
//                        （即使共享线程池有多个 worker，本 pump 也保证同一时刻只有
//                        一个 in-flight dispatch）。
//   - Stop()           : 阻塞等待"当前 in-flight dispatch + 队列残留"全部结束；
//                        Stop 返回后保证不再调用 on_dispatch_。可重复调用、幂等。
//
// 实现要点：依赖 `rflow::thread::post` 时单飞机制（dispatching_ 标志位 + queue
// 配合 condvar 等 Stop 同步），避免占用专属 worker 线程。

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rflow::service::impl {

class SubscriberOfferPump {
public:
    using DispatchFn = std::function<void(const std::string& peer_id)>;

    SubscriberOfferPump() = default;
    ~SubscriberOfferPump() { Stop(); }

    SubscriberOfferPump(const SubscriberOfferPump&)            = delete;
    SubscriberOfferPump& operator=(const SubscriberOfferPump&) = delete;

    void Start(DispatchFn on_dispatch);

    void Stop();

    void Enqueue(std::string peer_id);

    bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    /// 入队后若发现没有 in-flight task 时调用。会通过 thread_pool 投递一个 dispatch
    /// task；若 post 不可用（如 thread_pool 未 init），降级为同步 drain。
    void ScheduleDispatchLocked();

    /// 单飞 dispatch：drain 当前队列，每个 peer_id 调用一次 on_dispatch_，结束后
    /// 把 dispatching_ 置 false 并通过 cv 通知 Stop。
    void DrainOnce();

    DispatchFn               on_dispatch_;
    std::atomic<bool>        running_{false};

    /// running_ 控制 Start/Stop 状态；stopping_ 控制 in-flight DrainOnce 提前退出。
    /// 两个原子变量分离是为了让 Stop() 阻塞等到 dispatching_ 归零之前不必干扰
    /// running_ 语义（IsRunning 仍可在 Stop drain 期间正确为 false）。
    std::atomic<bool>        stopping_{false};

    /// dispatching_ = true 表示 thread_pool 中有一个 DrainOnce 在跑。
    /// 使用 atomic 是为了 Enqueue 在锁外做 fast path 探测；锁内才是权威值。
    std::atomic<bool>        dispatching_{false};

    /// Stop 等待 dispatching_ 归零的同步点。
    mutable std::mutex       mu_;
    std::condition_variable  cv_;
    std::vector<std::string> queue_;
};

}  // namespace rflow::service::impl

#endif  // __RFLOW_SERVICE_IMPL_SUBSCRIBER_OFFER_PUMP_H__
