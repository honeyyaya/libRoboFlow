// test_pending_ice_buffer — 验证 ICE candidate 缓存 / 应用 / drain 的边界行为。

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "rtc/pending_ice_buffer.h"

using rflow::core::rtc::PendingIce;
using rflow::core::rtc::PendingIceBuffer;

TEST(PendingIceBufferTest, InitialStateNotApplied) {
    PendingIceBuffer buf;
    EXPECT_FALSE(buf.IsApplied());
    EXPECT_EQ(buf.SizeApprox(), 0u);
}

TEST(PendingIceBufferTest, PushIncrementsSize) {
    PendingIceBuffer buf;
    EXPECT_EQ(buf.Push("0", 0, "candidate:a"), 1u);
    EXPECT_EQ(buf.Push("0", 1, "candidate:b"), 2u);
    EXPECT_EQ(buf.SizeApprox(), 2u);
}

TEST(PendingIceBufferTest, MarkAppliedAndDrainReturnsAllAndFlipsFlag) {
    PendingIceBuffer buf;
    buf.Push("a", 0, "ca");
    buf.Push("b", 1, "cb");

    auto out = buf.MarkAppliedAndDrain();
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].mid, "a");
    EXPECT_EQ(out[0].mline_index, 0);
    EXPECT_EQ(out[0].candidate, "ca");
    EXPECT_EQ(out[1].mid, "b");
    EXPECT_EQ(out[1].mline_index, 1);
    EXPECT_EQ(out[1].candidate, "cb");

    EXPECT_TRUE(buf.IsApplied());
    EXPECT_EQ(buf.SizeApprox(), 0u);
}

TEST(PendingIceBufferTest, ResetUnappliedClearsBufferAndFlag) {
    PendingIceBuffer buf;
    buf.Push("a", 0, "ca");
    buf.MarkAppliedAndDrain();
    ASSERT_TRUE(buf.IsApplied());

    buf.ResetUnapplied();
    EXPECT_FALSE(buf.IsApplied());
    EXPECT_EQ(buf.SizeApprox(), 0u);
}

TEST(PendingIceBufferTest, ClearKeepsAppliedFlag) {
    PendingIceBuffer buf;
    buf.Push("a", 0, "ca");
    buf.MarkAppliedAndDrain();
    ASSERT_TRUE(buf.IsApplied());

    buf.Push("late", 0, "c-late");
    buf.Clear();
    EXPECT_TRUE(buf.IsApplied()) << "Clear must not reset applied flag";
    EXPECT_EQ(buf.SizeApprox(), 0u);
}

TEST(PendingIceBufferTest, ConcurrentPushAndDrain) {
    PendingIceBuffer        buf;
    constexpr int           kProducers      = 4;
    constexpr int           kPerProducer    = 1024;
    std::atomic<int>        total_pushed{0};
    std::vector<std::thread> ts;
    ts.reserve(kProducers);
    for (int i = 0; i < kProducers; ++i) {
        ts.emplace_back([&buf, &total_pushed] {
            for (int k = 0; k < kPerProducer; ++k) {
                buf.Push("m", k, "cand");
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : ts) t.join();

    auto drained = buf.MarkAppliedAndDrain();
    EXPECT_EQ(static_cast<int>(drained.size()), total_pushed.load());
    EXPECT_TRUE(buf.IsApplied());
}
