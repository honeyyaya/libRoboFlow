/**
 * @file   thread_pool.h
 * @brief  进程级共享线程池，承载短异步任务与延时任务（轻量调度）。
 *
 * 适用场景：
 *   - 一次性短任务（`post`）：例如停流时异步关闭 PeerConnection、信令收到
 *     subscriber_join 后调度 CreateOffer 等"事件驱动 + 不阻塞主线程"的工作。
 *   - 周期或延时任务（`post_after`）：例如 stats 采样循环 — 用 task → sleep
 *     不会占满 worker，而是通过定时调度避免常驻线程。
 *   - 单飞串行队列（single-flight）：例如 SubscriberOfferPump，由调用方持
 *     "is_running task" flag，dispatch 入队时只投递一次 task，task 内部 drain
 *     队列；Stop 通过 condvar 等到 in-flight task 自然退出。这样能复用进程级
 *     pool，又保留串行语义。
 *
 * 不适用：长阻塞循环（V4L2 reader 等需要专属硬件亲和的 thread），仍应使用
 * 专用 std::thread；这类 thread 对调度语义有顺序与生命周期约束，混入共享 pool
 * 反而会造成 worker 饥饿与回归风险。
 */
#ifndef __RFLOW_CORE_THREAD_POOL_H__
#define __RFLOW_CORE_THREAD_POOL_H__

#include <chrono>
#include <functional>

namespace rflow::thread {

bool initialize(int worker_count = 0);
void shutdown();

/// 立即把 fn 投递到任意一个空闲 worker；fn 可空，空函数会被忽略。
void post(std::function<void()> fn);

/// 延迟 delay 后由调度线程投递到 worker。delay <= 0 等同 post()。
/// 如果 shutdown 在到期前发生，task 会被丢弃且不会被调用。
void post_after(std::chrono::milliseconds delay, std::function<void()> fn);

}  // namespace rflow::thread

#endif  // __RFLOW_CORE_THREAD_POOL_H__
