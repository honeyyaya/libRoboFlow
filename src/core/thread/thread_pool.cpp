/**
 * @file   thread_pool.cpp
 * @brief  进程级共享线程池实现：N 个 worker + 1 个 scheduler 线程（处理 delay queue）。
 *
 * 设计要点：
 *   - 任务队列：`g_tasks`（std::queue<fn>）；worker 等条件变量唤醒后取一个任务执行。
 *   - 延时队列：`g_delayed`（按 due_time 排序的最小堆替代——这里用 multimap 简化）；
 *     单独的 scheduler 线程定时唤醒，把到期任务搬到主队列。
 *   - shutdown 时全部线程都参与排队，scheduler 也会被 g_cv 唤醒退出。
 *   - 异常隔离：任务抛异常被吞掉，避免 worker 线程退出。
**/

#include <signal.h>

#include "thread_pool.h"

#include "base/logging.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::vector<std::thread>          g_workers;
std::queue<std::function<void()>> g_tasks;

// 延时队列 multimap<due_time, fn>，按 due_time 升序；equal_range 自然 FIFO（map 实现）。
std::multimap<Clock::time_point, std::function<void()>> g_delayed;

std::mutex                        g_mu;
std::condition_variable           g_cv;          // 主队列：worker 等
std::condition_variable           g_sched_cv;    // 延时队列：scheduler 等
std::atomic<bool>                 g_stop{false};
std::thread                       g_scheduler;

void worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return g_stop.load() || !g_tasks.empty(); });
            if (g_stop.load() && g_tasks.empty()) return;
            task = std::move(g_tasks.front());
            g_tasks.pop();
        }
        try { task(); } catch (...) { /* 吞掉，避免线程崩溃 */ }
    }
}

void scheduler_loop() {
    while (true) {
        std::function<void()> ready_task;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            if (g_delayed.empty()) {
                g_sched_cv.wait(lk, [] { return g_stop.load() || !g_delayed.empty(); });
            } else {
                const auto due = g_delayed.begin()->first;
                g_sched_cv.wait_until(lk, due, [&] {
                    return g_stop.load() ||
                           (!g_delayed.empty() && g_delayed.begin()->first < due);
                });
            }
            if (g_stop.load()) return;
            const auto now = Clock::now();
            while (!g_delayed.empty() && g_delayed.begin()->first <= now) {
                g_tasks.emplace(std::move(g_delayed.begin()->second));
                g_delayed.erase(g_delayed.begin());
                g_cv.notify_one();
            }
        }
    }
}

}  // namespace

namespace rflow::thread {

bool initialize(int worker_count) {
    if (!g_workers.empty()) return true;
    if (worker_count <= 0) {
        worker_count = static_cast<int>(std::thread::hardware_concurrency());
        if (worker_count <= 0) worker_count = 4;
    }
    g_stop.store(false);
    for (int i = 0; i < worker_count; ++i) g_workers.emplace_back(worker_loop);
    g_scheduler = std::thread(scheduler_loop);
    RFLOW_CORE_LOGI("thread_pool: %d workers + 1 scheduler started", worker_count);
    return true;
}

void shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stop.store(true);
        g_delayed.clear();
    }
    g_cv.notify_all();
    g_sched_cv.notify_all();
    if (g_scheduler.joinable()) g_scheduler.join();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();
    std::queue<std::function<void()>>().swap(g_tasks);
    RFLOW_CORE_LOGI("thread_pool: shutdown");
}

void post(std::function<void()> fn) {
    if (!fn) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_tasks.emplace(std::move(fn));
    }
    g_cv.notify_one();
}

void post_after(std::chrono::milliseconds delay, std::function<void()> fn) {
    if (!fn) return;
    if (delay.count() <= 0) {
        post(std::move(fn));
        return;
    }
    const auto due = Clock::now() + delay;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_delayed.emplace(due, std::move(fn));
    }
    g_sched_cv.notify_one();
}

}  // namespace rflow::thread
