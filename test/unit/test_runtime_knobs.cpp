// test_runtime_knobs — 验证 core/runtime/runtime_knobs：
//   - 表内常量 ReadInt/ReadBool/ReadString 行为
//   - 越界 clamp、解析失败 fallback
//   - 未注册 name 的 fallback 不崩
//   - alias deprecation 在表里目前未启用，但读未注册 name 应该不抛异常

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "runtime/runtime_knobs.h"

namespace {

class KnobsTest : public ::testing::Test {
protected:
    void SetUp() override { touched_.clear(); }
    void TearDown() override {
        for (const auto& n : touched_) ::unsetenv(n.c_str());
        touched_.clear();
    }
    void Set(const char* name, const char* val) {
        ::setenv(name, val, 1);
        touched_.emplace_back(name);
    }
    void Unset(const char* name) {
        ::unsetenv(name);
        touched_.emplace_back(name);
    }

private:
    std::vector<std::string> touched_;
};

}  // namespace

namespace knob = rflow::core::runtime;

TEST_F(KnobsTest, BoolSpecDefaultIsFalse) {
    Unset("RFLOW_VERBOSE_SIGNAL");
    EXPECT_FALSE(knob::ReadBool("RFLOW_VERBOSE_SIGNAL"));
}

TEST_F(KnobsTest, BoolSpecTruthyParses) {
    for (const char* v : {"1", "y", "Y", "t", "T"}) {
        Set("RFLOW_VERBOSE_SIGNAL", v);
        EXPECT_TRUE(knob::ReadBool("RFLOW_VERBOSE_SIGNAL")) << "v=" << v;
    }
}

TEST_F(KnobsTest, IntSpecDefaultMatchesTable) {
    Unset("RFLOW_SVC_DEFAULT_FPS");
    EXPECT_EQ(knob::ReadInt("RFLOW_SVC_DEFAULT_FPS"), 30) << "table default";
}

TEST_F(KnobsTest, IntSpecValidParses) {
    Set("RFLOW_SVC_DEFAULT_FPS", "60");
    EXPECT_EQ(knob::ReadInt("RFLOW_SVC_DEFAULT_FPS"), 60);
}

TEST_F(KnobsTest, IntSpecAboveMaxClamps) {
    Set("RFLOW_SVC_DEFAULT_FPS", "9999");
    EXPECT_EQ(knob::ReadInt("RFLOW_SVC_DEFAULT_FPS"), 240);
}

TEST_F(KnobsTest, IntSpecBelowMinClamps) {
    Set("RFLOW_SVC_DEFAULT_FPS", "0");
    EXPECT_EQ(knob::ReadInt("RFLOW_SVC_DEFAULT_FPS"), 1);
}

TEST_F(KnobsTest, IntSpecJunkReturnsDefault) {
    Set("RFLOW_SVC_DEFAULT_FPS", "abc");
    EXPECT_EQ(knob::ReadInt("RFLOW_SVC_DEFAULT_FPS"), 30);
}

TEST_F(KnobsTest, StringSpecDefaultFromTable) {
    Unset("RFLOW_SVC_DEGRADATION_PREFERENCE");
    EXPECT_EQ(knob::ReadString("RFLOW_SVC_DEGRADATION_PREFERENCE"), "maintain_framerate");
}

TEST_F(KnobsTest, StringSpecOverride) {
    Set("RFLOW_SVC_DEGRADATION_PREFERENCE", "balanced");
    EXPECT_EQ(knob::ReadString("RFLOW_SVC_DEGRADATION_PREFERENCE"), "balanced");
}

TEST_F(KnobsTest, UnregisteredNameFallsBackSilently) {
    // 未注册 → fallback：bool=false, int=0, string=""
    Unset("RFLOW_TEST_NEVER_DEFINED_KNOB");
    EXPECT_FALSE(knob::ReadBool("RFLOW_TEST_NEVER_DEFINED_KNOB"));
    EXPECT_EQ(knob::ReadInt("RFLOW_TEST_NEVER_DEFINED_KNOB"), 0);
    EXPECT_EQ(knob::ReadString("RFLOW_TEST_NEVER_DEFINED_KNOB"), "");
}

TEST_F(KnobsTest, KnobTableHasContent) {
    auto v = knob::GetKnobTable();
    EXPECT_NE(v.specs, nullptr);
    EXPECT_GT(v.count, 0u);
    // 至少包含我们刚刚测试用到的名字
    bool found_fps = false, found_deg = false;
    for (size_t i = 0; i < v.count; ++i) {
        if (std::string(v.specs[i].name) == "RFLOW_SVC_DEFAULT_FPS") found_fps = true;
        if (std::string(v.specs[i].name) == "RFLOW_SVC_DEGRADATION_PREFERENCE") found_deg = true;
    }
    EXPECT_TRUE(found_fps);
    EXPECT_TRUE(found_deg);
}

TEST_F(KnobsTest, MarkdownDumpContainsHeaders) {
    const std::string md = knob::DumpAsMarkdown();
    EXPECT_NE(md.find("# libRoboFlow Runtime Knobs"), std::string::npos);
    // 至少包含一个分组与一个 knob
    EXPECT_NE(md.find("RFLOW_SVC_DEFAULT_FPS"), std::string::npos);
}
