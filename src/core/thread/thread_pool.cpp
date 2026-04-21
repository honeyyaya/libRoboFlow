/**
 * @file   thread_pool.cpp
 * @brief  简易线程池（占位实现，后续替换为 libevent / custom executor）
**/

#include "thread_pool.h"

#include "common/internal/logger.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace {

std::vector<std::thread>          g_workers;
std::queue<std::function<void()>> g_tasks;
std::mutex                        g_mu;
std::condition_variable           g_cv;
std::atomic<bool>                 g_stop{false};

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

}  // namespace

namespace robrt::thread {

bool initialize(int worker_count) {
    if (!g_workers.empty()) return true;
    if (worker_count <= 0) {
        worker_count = static_cast<int>(std::thread::hardware_concurrency());
        if (worker_count <= 0) worker_count = 4;
    }
    g_stop.store(false);
    for (int i = 0; i < worker_count; ++i) g_workers.emplace_back(worker_loop);
    ROBRT_LOGI("thread_pool: %d workers started", worker_count);
    return true;
}

void shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stop.store(true);
    }
    g_cv.notify_all();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();
    std::queue<std::function<void()>>().swap(g_tasks);
    ROBRT_LOGI("thread_pool: shutdown");
}

void post(std::function<void()> fn) {
    if (!fn) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_tasks.emplace(std::move(fn));
    }
    g_cv.notify_one();
}

}  // namespace robrt::thread
