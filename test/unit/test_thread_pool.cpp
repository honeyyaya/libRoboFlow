// Unit tests for core/thread/thread_pool.h.
// 关注点：post/post_after 调度顺序、shutdown 安全、空函数容忍。
//
// 注意：每个 TEST 自己 init/shutdown 以避免单测之间状态泄漏；GoogleTest 是单线程
// 跑 TEST 的，全局静态状态可控。

#include "thread/thread_pool.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "gtest/gtest.h"

namespace {
class ThreadPoolTest : public ::testing::Test {
 protected:
    void SetUp() override { rflow::thread::initialize(2); }
    void TearDown() override { rflow::thread::shutdown(); }
};
}  // namespace

TEST_F(ThreadPoolTest, PostExecutesTask) {
    std::atomic<int> counter{0};
    rflow::thread::post([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    for (int i = 0; i < 200 && counter.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, PostNullIsTolerated) {
    rflow::thread::post(std::function<void()>{});
    rflow::thread::post_after(std::chrono::milliseconds(10), std::function<void()>{});
    SUCCEED();
}

TEST_F(ThreadPoolTest, PostAfterRespectsDelay) {
    std::atomic<bool> done{false};
    auto t0 = std::chrono::steady_clock::now();
    rflow::thread::post_after(std::chrono::milliseconds(80),
                              [&done] { done.store(true, std::memory_order_release); });
    while (!done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 60);
}

TEST_F(ThreadPoolTest, PostAfterZeroFastPath) {
    std::atomic<bool> done{false};
    rflow::thread::post_after(std::chrono::milliseconds(0),
                              [&done] { done.store(true, std::memory_order_release); });
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(done.load());
}

TEST_F(ThreadPoolTest, ManyPostsAllRun) {
    constexpr int kN = 200;
    std::atomic<int> counter{0};
    for (int i = 0; i < kN; ++i) {
        rflow::thread::post([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    for (int i = 0; i < 400 && counter.load() < kN; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(counter.load(), kN);
}

TEST(ThreadPoolShutdownTest, ShutdownCancelsPendingDelayed) {
    rflow::thread::initialize(2);
    std::atomic<int> counter{0};
    rflow::thread::post_after(std::chrono::milliseconds(2000),
                              [&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    // 立刻 shutdown，应当丢弃 pending delayed task。
    rflow::thread::shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter.load(), 0);
}
