// test_subscriber_offer_pump — 验证 SubscriberOfferPump 的入队/串行 dispatch/Stop 时序。
//
// 行为契约见 subscriber_offer_pump.h：
//   - Start 至多一次；Enqueue 任意线程；Stop 阻塞等待 in-flight DrainOnce 结束并清队列。
//   - 新实现把 dispatch 通过 rflow::thread::post 投到进程级线程池，而不是常驻线程；
//     测试需要全局 init/shutdown thread_pool。

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/thread/thread_pool.h"
#include "subscriber_offer_pump.h"

using rflow::service::impl::SubscriberOfferPump;

namespace {

class SubscriberOfferPumpTest : public ::testing::Test {
 protected:
    static void SetUpTestSuite() { rflow::thread::initialize(2); }
    static void TearDownTestSuite() { rflow::thread::shutdown(); }
};

}  // namespace

#define TEST_PUMP(name) TEST_F(SubscriberOfferPumpTest, name)

TEST_PUMP(NotRunningByDefault) {
    SubscriberOfferPump pump;
    EXPECT_FALSE(pump.IsRunning());
    pump.Stop();
    EXPECT_FALSE(pump.IsRunning());
}

TEST_PUMP(StartFlipsRunning) {
    SubscriberOfferPump pump;
    pump.Start([](const std::string&) {});
    EXPECT_TRUE(pump.IsRunning());
    pump.Stop();
    EXPECT_FALSE(pump.IsRunning());
}

TEST_PUMP(EnqueuedItemsDispatchInOrder) {
    SubscriberOfferPump pump;

    std::mutex                mu;
    std::condition_variable   cv;
    std::vector<std::string>  seen;

    pump.Start([&](const std::string& peer_id) {
        std::lock_guard<std::mutex> lk(mu);
        seen.push_back(peer_id);
        cv.notify_all();
    });

    for (int i = 0; i < 5; ++i) pump.Enqueue("peer-" + std::to_string(i));

    {
        std::unique_lock<std::mutex> lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(2),
                                [&] { return seen.size() == 5; }));
    }
    pump.Stop();

    ASSERT_EQ(seen.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(seen[i], "peer-" + std::to_string(i));
    }
}

TEST_PUMP(StopDuringDispatchWaitsForLastInBatch) {
    SubscriberOfferPump pump;

    std::atomic<int>       dispatched{0};
    std::atomic<bool>      stop_requested{false};

    pump.Start([&](const std::string&) {
        dispatched.fetch_add(1, std::memory_order_acq_rel);
        // 模拟"业务慢回调"
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    for (int i = 0; i < 3; ++i) pump.Enqueue("p");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop_requested.store(true);
    pump.Stop();

    EXPECT_TRUE(stop_requested.load());
    // 至少 in-flight DrainOnce 内的当前 callback 已派发完（保守下界 1）；
    // 后续 callback 在 stopping_ 检查处提前 break。
    EXPECT_GE(dispatched.load(), 1);
    EXPECT_FALSE(pump.IsRunning());
}

TEST_PUMP(StartIsIdempotentAfterStart) {
    SubscriberOfferPump pump;
    std::atomic<int>    calls{0};
    pump.Start([&](const std::string&) { calls.fetch_add(1); });
    // 第二次 Start 应该被拒绝，不会启动第二条 worker（无法直接观察，但 IsRunning 仍 true）
    pump.Start([&](const std::string&) { calls.fetch_add(100); });
    EXPECT_TRUE(pump.IsRunning());
    pump.Enqueue("p1");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pump.Stop();
    // 只 dispatch 了第一个 callback（+1），不会 +100
    EXPECT_EQ(calls.load(), 1);
}

TEST_PUMP(StopIsIdempotent) {
    SubscriberOfferPump pump;
    pump.Start([](const std::string&) {});
    pump.Stop();
    pump.Stop();
    EXPECT_FALSE(pump.IsRunning());
}

// 新实现关键不变量：即使 thread_pool 有多个 worker，pump 也必须在任何时刻
// 至多有一个 dispatch 正在执行（single-flight）。
TEST_PUMP(DispatchIsSingleFlightAcrossPoolWorkers) {
    SubscriberOfferPump pump;

    std::atomic<int> in_flight{0};
    std::atomic<int> max_in_flight{0};
    std::atomic<int> total{0};

    pump.Start([&](const std::string&) {
        int n = in_flight.fetch_add(1, std::memory_order_acq_rel) + 1;
        int prev = max_in_flight.load(std::memory_order_acquire);
        while (n > prev &&
               !max_in_flight.compare_exchange_weak(prev, n, std::memory_order_acq_rel)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        in_flight.fetch_sub(1, std::memory_order_acq_rel);
        total.fetch_add(1, std::memory_order_acq_rel);
    });

    for (int i = 0; i < 32; ++i) pump.Enqueue("p" + std::to_string(i));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (total.load() < 32 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    pump.Stop();

    EXPECT_EQ(total.load(), 32);
    EXPECT_EQ(max_in_flight.load(), 1) << "dispatch must be serialized";
}
