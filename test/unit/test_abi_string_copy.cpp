// test_abi_string_copy — 验证 ABI 出参字符串拷贝语义（截断/needed/EmptyNotFound）。
//
// 契约见 src/common/base/abi_string_copy.h：
//   - empty   → RFLOW_ERR_NOT_FOUND（不写 out_needed）
//   - buf=nullptr / buf_len=0 → RFLOW_ERR_TRUNCATED + out_needed=size+1
//   - buf_len < size+1 → RFLOW_ERR_TRUNCATED + 写 buf_len-1 + '\0'
//   - 其他 → RFLOW_OK + 完整 + '\0'

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "base/abi_string_copy.h"

using rflow::common::base::CopyOutString;

TEST(AbiStringCopyTest, EmptyReturnsNotFoundAndDoesNotWriteNeeded) {
    char        buf[16] = {};
    uint32_t    needed   = 0xFFFFFFFFu;
    rflow_err_t rc       = CopyOutString(std::string(""), buf, sizeof(buf), &needed);
    EXPECT_EQ(rc, RFLOW_ERR_NOT_FOUND);
    EXPECT_EQ(needed, 0xFFFFFFFFu) << "needed must NOT be touched on NOT_FOUND";
}

TEST(AbiStringCopyTest, NullBufReturnsTruncatedAndProbesNeeded) {
    uint32_t    needed = 0;
    rflow_err_t rc     = CopyOutString(std::string("hello"), nullptr, 0u, &needed);
    EXPECT_EQ(rc, RFLOW_ERR_TRUNCATED);
    EXPECT_EQ(needed, 6u) << "len('hello') + 1";
}

TEST(AbiStringCopyTest, BufZeroReturnsTruncatedAndProbesNeeded) {
    char        buf[1] = {'X'};
    uint32_t    needed = 0;
    rflow_err_t rc     = CopyOutString(std::string("ab"), buf, 0u, &needed);
    EXPECT_EQ(rc, RFLOW_ERR_TRUNCATED);
    EXPECT_EQ(needed, 3u);
    EXPECT_EQ(buf[0], 'X') << "must not write into buf when buf_len==0";
}

TEST(AbiStringCopyTest, BufTooSmallTruncatesAndAlwaysNullTerminates) {
    char        buf[4] = {'?', '?', '?', '?'};
    uint32_t    needed = 0;
    rflow_err_t rc     = CopyOutString(std::string("hello"), buf, 4u, &needed);
    EXPECT_EQ(rc, RFLOW_ERR_TRUNCATED);
    EXPECT_EQ(needed, 6u);
    EXPECT_STREQ(buf, "hel") << "writes buf_len-1 chars + null";
}

TEST(AbiStringCopyTest, ExactBufSizeOk) {
    char        buf[6] = {};
    uint32_t    needed = 0;
    rflow_err_t rc     = CopyOutString(std::string("hello"), buf, 6u, &needed);
    EXPECT_EQ(rc, RFLOW_OK);
    EXPECT_EQ(needed, 6u);
    EXPECT_STREQ(buf, "hello");
}

TEST(AbiStringCopyTest, BiggerBufOkLeavesNullTerminator) {
    char        buf[16] = {};
    uint32_t    needed  = 0;
    rflow_err_t rc      = CopyOutString(std::string("ok"), buf, sizeof(buf), &needed);
    EXPECT_EQ(rc, RFLOW_OK);
    EXPECT_EQ(needed, 3u);
    EXPECT_STREQ(buf, "ok");
}

TEST(AbiStringCopyTest, NullOutNeededIsAccepted) {
    char        buf[8] = {};
    rflow_err_t rc     = CopyOutString(std::string("xyz"), buf, sizeof(buf), nullptr);
    EXPECT_EQ(rc, RFLOW_OK);
    EXPECT_STREQ(buf, "xyz");
}

TEST(AbiStringCopyTest, EmbeddedNullsCopiedAsRawBytes) {
    // s.size() 是真实长度，包含中间的 \0；CopyOutString 用 memcpy + 末尾终止符，
    // 不应被中间的 \0 截断。
    std::string s("a\0b", 3);
    char        buf[8] = {};
    uint32_t    needed = 0;
    rflow_err_t rc     = CopyOutString(s, buf, sizeof(buf), &needed);
    EXPECT_EQ(rc, RFLOW_OK);
    EXPECT_EQ(needed, 4u);
    EXPECT_EQ(std::memcmp(buf, "a\0b\0", 4), 0);
}
