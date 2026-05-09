// test_log_tagged — 验证 RFLOW_LOG_TAG_* 宏：
//   - 走 SDK logger 通道（业务可拦截）
//   - tag 前缀正确拼到消息开头
//   - level 与 RFLOW_LOG{I,W,E} 一致

#include <gtest/gtest.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "common/public/log_tagged.h"
#include "rflow/librflow_common.h"

namespace {

struct CapturedRecord {
    rflow_log_level_t level;
    std::string       message;
};

std::mutex                  g_capture_mu;
std::vector<CapturedRecord> g_captured;

void CaptureCallback(rflow_log_level_t level, const char* message, void* /*ud*/) {
    std::lock_guard<std::mutex> lk(g_capture_mu);
    g_captured.push_back({level, message ? std::string(message) : std::string()});
}

class LogTaggedTest : public ::testing::Test {
protected:
    void SetUp() override {
        ::rflow::logger_set_level(RFLOW_LOG_TRACE);
        ::rflow::logger_set_callback(&CaptureCallback, nullptr);
        std::lock_guard<std::mutex> lk(g_capture_mu);
        g_captured.clear();
    }
    void TearDown() override {
        ::rflow::logger_set_callback(nullptr, nullptr);
        ::rflow::logger_set_level(RFLOW_LOG_INFO);
    }
};

}  // namespace

TEST_F(LogTaggedTest, InfoRoutesThroughCallback) {
    RFLOW_LOG_TAG_I("UnitTag", "hello %s value=%d", "world", 42);

    std::lock_guard<std::mutex> lk(g_capture_mu);
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, RFLOW_LOG_INFO);
    EXPECT_EQ(g_captured[0].message, "[UnitTag] hello world value=42");
}

TEST_F(LogTaggedTest, ErrorAndWarningLevels) {
    RFLOW_LOG_TAG_E("Tag", "boom: %d", 7);
    RFLOW_LOG_TAG_W("Tag", "careful");

    std::lock_guard<std::mutex> lk(g_capture_mu);
    ASSERT_EQ(g_captured.size(), 2u);
    EXPECT_EQ(g_captured[0].level, RFLOW_LOG_ERROR);
    EXPECT_EQ(g_captured[0].message, "[Tag] boom: 7");
    EXPECT_EQ(g_captured[1].level, RFLOW_LOG_WARN);
    EXPECT_EQ(g_captured[1].message, "[Tag] careful");
}

TEST_F(LogTaggedTest, LevelFilterDropsLowerLevels) {
    ::rflow::logger_set_level(RFLOW_LOG_WARN);
    RFLOW_LOG_TAG_I("Tag", "filtered out");
    RFLOW_LOG_TAG_W("Tag", "kept");

    std::lock_guard<std::mutex> lk(g_capture_mu);
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].level, RFLOW_LOG_WARN);
    EXPECT_EQ(g_captured[0].message, "[Tag] kept");
}
