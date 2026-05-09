#include "subscriber_offer_pump.h"

#include <utility>

#include "core/thread/thread_pool.h"

namespace rflow::service::impl {

void SubscriberOfferPump::Start(DispatchFn on_dispatch) {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    on_dispatch_ = std::move(on_dispatch);
    stopping_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    // 不预投 task；首次 Enqueue 会在 ScheduleDispatchLocked 中触发。
}

void SubscriberOfferPump::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    stopping_.store(true, std::memory_order_release);

    // 等待 in-flight DrainOnce 自然结束。已入队但尚未 dispatch 的 peer_id 会被
    // DrainOnce 内的 stopping_ 检查跳过；DrainOnce 末尾把 dispatching_ 置 false
    // 并 notify_all，本处随之返回。
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !dispatching_.load(std::memory_order_acquire); });
        queue_.clear();
    }
}

void SubscriberOfferPump::Enqueue(std::string peer_id) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!running_.load(std::memory_order_acquire) ||
        stopping_.load(std::memory_order_acquire)) {
        return;
    }
    queue_.push_back(std::move(peer_id));
    if (!dispatching_.load(std::memory_order_acquire)) {
        ScheduleDispatchLocked();
    }
}

void SubscriberOfferPump::ScheduleDispatchLocked() {
    // mu_ 已在调用方持有；本处仅置 dispatching_ 并丢出 task。
    dispatching_.store(true, std::memory_order_release);
    rflow::thread::post([this] { DrainOnce(); });
}

void SubscriberOfferPump::DrainOnce() {
    while (true) {
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> lk(mu_);
            // 收尾顺序：stopping_ 为 true 直接退出；否则若 queue 空也退出，把
            // dispatching_ 置 false 让 Stop 通过 cv 醒过来。
            if (stopping_.load(std::memory_order_acquire) || queue_.empty()) {
                dispatching_.store(false, std::memory_order_release);
                cv_.notify_all();
                return;
            }
            batch.swap(queue_);
        }

        // dispatch 在锁外跑，避免业务回调持有 mu_ 引发死锁；本时刻其它线程的
        // Enqueue 会把新 peer_id 推到 queue_，下一轮 while 循环会 drain 到。
        if (!on_dispatch_) {
            // 未设置 dispatch 回调：丢弃这一批，继续下一轮（或下次进入 if 退出）。
            continue;
        }
        for (const auto& peer_id : batch) {
            // 进入下一项前再检查 stopping_，让 Stop 在长 batch 中段就生效。
            if (stopping_.load(std::memory_order_acquire)) {
                break;
            }
            on_dispatch_(peer_id);
        }
    }
}

}  // namespace rflow::service::impl
