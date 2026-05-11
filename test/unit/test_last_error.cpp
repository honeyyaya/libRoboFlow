// Unit tests for common/base/last_error.{h,cpp} 与 librflow_get_last_error C ABI。
// origin / timestamp_ms 等结构化字段不对外暴露，直接通过内部 snapshot 验证。

#include "base/last_error.h"

#include <thread>

#include "gtest/gtest.h"

extern "C" const char* librflow_get_last_error(void);

TEST(LastErrorTest, InitialStateIsOk) {
    rflow::clear_last_error();
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_OK);
    EXPECT_STREQ(snap.message, "");
    EXPECT_STREQ(snap.origin, "");
    EXPECT_EQ(snap.timestamp_ms, 0u);
}

TEST(LastErrorTest, SimpleSetterUpgradesOkToFail) {
    rflow::clear_last_error();
    rflow::set_last_error("boom");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_FAIL);
    EXPECT_STREQ(snap.message, "boom");
    EXPECT_GT(snap.timestamp_ms, 0u);
}

TEST(LastErrorTest, SimpleSetterPreservesPriorCode) {
    rflow::clear_last_error();
    rflow::set_last_error_full(RFLOW_ERR_PARAM, "param invalid", "client");
    rflow::set_last_error("more detail");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_PARAM);
    EXPECT_STREQ(snap.message, "more detail");
    EXPECT_STREQ(snap.origin, "client");
}

TEST(LastErrorTest, FullSetterPopulatesAllFields) {
    rflow::clear_last_error();
    rflow::set_last_error_full(RFLOW_ERR_NOT_FOUND, "no such stream", "service/stream");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_NOT_FOUND);
    EXPECT_STREQ(snap.message, "no such stream");
    EXPECT_STREQ(snap.origin, "service/stream");
    EXPECT_GT(snap.timestamp_ms, 0u);
}

TEST(LastErrorTest, AbiLegacyMessageStillWorks) {
    rflow::clear_last_error();
    rflow::set_last_error("legacy text");
    EXPECT_STREQ(librflow_get_last_error(), "legacy text");
}

TEST(LastErrorTest, AbiLegacyMessageReturnsEmptyOnClear) {
    rflow::clear_last_error();
    EXPECT_STREQ(librflow_get_last_error(), "");
}

TEST(LastErrorTest, MessageWithOriginPreservesUpgradeSemantics) {
    rflow::clear_last_error();
    rflow::set_last_error("oops", "client/lifecycle");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_FAIL);
    EXPECT_STREQ(snap.message, "oops");
    EXPECT_STREQ(snap.origin, "client/lifecycle");
}

TEST(LastErrorTest, MessageWithOriginAcceptsCharStar) {
    rflow::clear_last_error();
    rflow::set_last_error("char-star", "service/stream");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_STREQ(snap.message, "char-star");
    EXPECT_STREQ(snap.origin, "service/stream");
}

TEST(LastErrorTest, MessageWithOriginNullOriginIsEmpty) {
    rflow::clear_last_error();
    rflow::set_last_error("with-nullptr", static_cast<const char*>(nullptr));
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_STREQ(snap.message, "with-nullptr");
    EXPECT_STREQ(snap.origin, "");
}

TEST(LastErrorTest, CodeMessageOriginOverloadHonorsCode) {
    rflow::clear_last_error();
    rflow::set_last_error(RFLOW_ERR_PARAM, "bad arg", "client/lifecycle");
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_PARAM);
    EXPECT_STREQ(snap.message, "bad arg");
    EXPECT_STREQ(snap.origin, "client/lifecycle");
}

TEST(LastErrorTest, ThreadLocalIsolation) {
    rflow::clear_last_error();
    rflow::set_last_error_full(RFLOW_ERR_PARAM, "main thread", "main");

    rflow::LastErrorSnapshot worker_snap;
    std::thread t([&worker_snap]() {
        // 线程独立：worker 第一次读到的应该是干净状态。
        worker_snap = rflow::get_last_error_snapshot();
    });
    t.join();

    EXPECT_EQ(worker_snap.code, RFLOW_OK);
    EXPECT_STREQ(worker_snap.message, "");

    // 主线程状态不应被 worker 改写。
    auto snap = rflow::get_last_error_snapshot();
    EXPECT_EQ(snap.code, RFLOW_ERR_PARAM);
    EXPECT_STREQ(snap.message, "main thread");
}
