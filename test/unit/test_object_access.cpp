// test_object_access — 覆盖 common/abi/object_access_ops.h 的 magic 校验和 setter/getter 宏。

#include <gtest/gtest.h>

#include <cstdint>

#include "common/abi/handle.h"
#include "common/abi/object_access_ops.h"

namespace {

constexpr uint32_t kFakeMagic = 0xDEADBEEFu;

// 模拟一个 ABI opaque：第一个字段必须是 uint32_t magic（与真实 layout 一致）。
struct FakeOpaque {
    uint32_t magic;
    int      x{0};
    bool     has_x{false};
    int      y{0};
    int      z{0};
    bool     has_xy{false};
    bool     has_xyz{false};
};

rflow_err_t SetX(FakeOpaque* o, int v) {
    RFLOW_SET_VALUE_WITH_FLAG(o, kFakeMagic, x, has_x, v);
}

rflow_err_t GetX(const FakeOpaque* o, int* out) {
    RFLOW_GET_VALUE_WITH_FLAG(o, kFakeMagic, out, has_x, o->x);
}

rflow_err_t SetXY(FakeOpaque* o, int xv, int yv) {
    RFLOW_SET_2_VALUES_WITH_FLAG(o, kFakeMagic, x, xv, y, yv, has_xy);
}

rflow_err_t GetXY(const FakeOpaque* o, int* outx, int* outy) {
    RFLOW_GET_2_VALUES_WITH_FLAG(o, kFakeMagic, outx, outy, has_xy, o->x, o->y);
}

rflow_err_t SetXYZ(FakeOpaque* o, int xv, int yv, int zv) {
    RFLOW_SET_3_VALUES_WITH_FLAG(o, kFakeMagic, x, xv, y, yv, z, zv, has_xyz);
}

rflow_err_t GetXYZ(const FakeOpaque* o, int* outx, int* outy, int* outz) {
    RFLOW_GET_3_VALUES_WITH_FLAG(o, kFakeMagic, outx, outy, outz, has_xyz, o->x, o->y, o->z);
}

}  // namespace

using rflow::common::abi::CreateMagicObject;
using rflow::common::abi::DestroyMagicObject;

TEST(CreateMagicObjectTest, AllocatesAndStampsMagic) {
    FakeOpaque* obj = CreateMagicObject<FakeOpaque>(kFakeMagic);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->magic, kFakeMagic);
    DestroyMagicObject(obj, kFakeMagic);
}

TEST(DestroyMagicObjectTest, RejectsNull) {
    // 不应崩溃
    DestroyMagicObject<FakeOpaque>(nullptr, kFakeMagic);
}

TEST(DestroyMagicObjectTest, RejectsWrongMagic) {
    auto* obj  = CreateMagicObject<FakeOpaque>(kFakeMagic);
    obj->magic = 0xCAFEBABEu;
    // 错 magic — Destroy 应是 no-op，不释放
    DestroyMagicObject(obj, kFakeMagic);
    EXPECT_EQ(obj->magic, 0xCAFEBABEu) << "wrong magic must keep object intact";
    obj->magic = kFakeMagic;
    DestroyMagicObject(obj, kFakeMagic);
}

TEST(SetWithFlagTest, NullHandleRejected) {
    EXPECT_EQ(SetX(nullptr, 1), RFLOW_ERR_PARAM);
}

TEST(SetWithFlagTest, BadMagicRejected) {
    FakeOpaque o{};
    o.magic = 0x12345678u;
    EXPECT_EQ(SetX(&o, 1), RFLOW_ERR_PARAM);
}

TEST(GetWithFlagTest, NotSetReturnsNotFound) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    int       v = -1;
    EXPECT_EQ(GetX(&o, &v), RFLOW_ERR_NOT_FOUND);
    EXPECT_EQ(v, -1) << "out must remain untouched";
}

TEST(GetWithFlagTest, NullOutRejected) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    EXPECT_EQ(GetX(&o, nullptr), RFLOW_ERR_PARAM);
}

TEST(SetGetRoundtripTest, SingleField) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    EXPECT_EQ(SetX(&o, 42), RFLOW_OK);
    int v = 0;
    EXPECT_EQ(GetX(&o, &v), RFLOW_OK);
    EXPECT_EQ(v, 42);
}

TEST(SetGetRoundtripTest, TwoFields) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    EXPECT_EQ(SetXY(&o, 7, 8), RFLOW_OK);
    int a = 0, b = 0;
    EXPECT_EQ(GetXY(&o, &a, &b), RFLOW_OK);
    EXPECT_EQ(a, 7);
    EXPECT_EQ(b, 8);
}

TEST(SetGetRoundtripTest, TwoFieldsRejectsNullOut) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    EXPECT_EQ(SetXY(&o, 7, 8), RFLOW_OK);
    int a = 0;
    EXPECT_EQ(GetXY(&o, &a, nullptr), RFLOW_ERR_PARAM);
    EXPECT_EQ(GetXY(&o, nullptr, &a), RFLOW_ERR_PARAM);
}

TEST(SetGetRoundtripTest, ThreeFields) {
    FakeOpaque o{};
    o.magic = kFakeMagic;
    EXPECT_EQ(SetXYZ(&o, 1, 2, 3), RFLOW_OK);
    int a = 0, b = 0, c = 0;
    EXPECT_EQ(GetXYZ(&o, &a, &b, &c), RFLOW_OK);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(c, 3);
}
