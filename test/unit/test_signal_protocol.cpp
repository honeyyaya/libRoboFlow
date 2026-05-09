// test_signal_protocol — 信令协议 line parser / builder 的语义测试。
//
// 验证范围：
//   - PeerRole / MessageType 与 string 的双向转换
//   - ParseEndpoint：去掉 ws:// / tcp:// 前缀；缺端口默认 8765；空主机失败
//   - EscapeJsonString / ExtractJsonString 的转义对偶
//   - ParseMessage：常用消息类型字段提取
//   - BuildRegisterLine / BuildMessageLine 与 ParseMessage 的 round-trip

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "core/signal/protocol.h"
#include "rflow/librflow_common.h"

using namespace rflow::signal;

TEST(PeerRoleTest, BiwayConversion) {
    EXPECT_STREQ(ToString(PeerRole::kPublisher), "publisher");
    EXPECT_STREQ(ToString(PeerRole::kSubscriber), "subscriber");
    EXPECT_STREQ(ToString(PeerRole::kUnknown), "unknown");

    EXPECT_EQ(PeerRoleFromString("publisher"), PeerRole::kPublisher);
    EXPECT_EQ(PeerRoleFromString("subscriber"), PeerRole::kSubscriber);
    EXPECT_EQ(PeerRoleFromString("garbage"), PeerRole::kUnknown);
    EXPECT_EQ(PeerRoleFromString(""), PeerRole::kUnknown);
}

TEST(MessageTypeTest, BiwayConversion) {
    constexpr struct {
        MessageType type;
        const char* str;
    } kCases[] = {
        {MessageType::kRegister, "register"},
        {MessageType::kWelcome, "welcome"},
        {MessageType::kOffer, "offer"},
        {MessageType::kAnswer, "answer"},
        {MessageType::kIce, "ice"},
        {MessageType::kSubscriberJoin, "subscriber_join"},
        {MessageType::kSubscriberLeave, "subscriber_leave"},
    };
    for (const auto& c : kCases) {
        EXPECT_STREQ(ToString(c.type), c.str);
        EXPECT_EQ(MessageTypeFromString(c.str), c.type);
    }
    EXPECT_EQ(MessageTypeFromString("nope"), MessageType::kUnknown);
}

TEST(ParseEndpointTest, NullOutFails) {
    EXPECT_FALSE(ParseEndpoint("127.0.0.1:8765", nullptr));
}

TEST(ParseEndpointTest, HostPort) {
    Endpoint ep;
    ASSERT_TRUE(ParseEndpoint("127.0.0.1:9000", &ep));
    EXPECT_EQ(ep.host, "127.0.0.1");
    EXPECT_EQ(ep.port, 9000);
}

TEST(ParseEndpointTest, StripsWsPrefix) {
    Endpoint ep;
    ASSERT_TRUE(ParseEndpoint("ws://example.com:1234", &ep));
    EXPECT_EQ(ep.host, "example.com");
    EXPECT_EQ(ep.port, 1234);
}

TEST(ParseEndpointTest, StripsTcpPrefix) {
    Endpoint ep;
    ASSERT_TRUE(ParseEndpoint("tcp://example.com:1234", &ep));
    EXPECT_EQ(ep.host, "example.com");
    EXPECT_EQ(ep.port, 1234);
}

TEST(ParseEndpointTest, MissingPortDefaults8765) {
    Endpoint ep;
    ASSERT_TRUE(ParseEndpoint("example.com", &ep));
    EXPECT_EQ(ep.host, "example.com");
    EXPECT_EQ(ep.port, 8765);
}

TEST(ParseEndpointTest, EmptyAddrFails) {
    Endpoint ep;
    EXPECT_FALSE(ParseEndpoint("", &ep));
}

TEST(JsonEscapeTest, BasicChars) {
    EXPECT_EQ(EscapeJsonString("plain"), "plain");
    EXPECT_EQ(EscapeJsonString("a\"b"), "a\\\"b");
    EXPECT_EQ(EscapeJsonString("a\\b"), "a\\\\b");
    EXPECT_EQ(EscapeJsonString("a\nb"), "a\\nb");
    EXPECT_EQ(EscapeJsonString("a\rb"), "a\\rb");
}

TEST(ExtractJsonStringTest, MissingKeyReturnsEmpty) {
    EXPECT_EQ(ExtractJsonString(R"({"type":"answer"})", "sdp"), "");
}

TEST(ExtractJsonStringTest, EscapeRoundtrip) {
    const std::string raw  = "a\"b\\c\nd";
    const std::string esc  = EscapeJsonString(raw);
    const std::string line = std::string(R"({"sdp":")") + esc + R"("})";
    EXPECT_EQ(ExtractJsonString(line, "sdp"), raw);
}

TEST(ExtractJsonIntTest, MissingKeyReturnsFallback) {
    EXPECT_EQ(ExtractJsonInt(R"({"type":"ice"})", "mlineIndex", -1), -1);
}

TEST(ExtractJsonIntTest, ParsesValue) {
    EXPECT_EQ(ExtractJsonInt(R"({"x":42,"mlineIndex":7})", "mlineIndex", 0), 7);
}

TEST(ParseMessageTest, NullOrEmptyFails) {
    Message m;
    EXPECT_FALSE(ParseMessage("", &m));
    EXPECT_FALSE(ParseMessage(R"({"type":"answer"})", nullptr));
}

TEST(ParseMessageTest, UnknownTypeFails) {
    Message m;
    EXPECT_FALSE(ParseMessage(R"({"type":"???"})", &m));
}

TEST(ParseMessageTest, OfferKeepsSdp) {
    Message m;
    ASSERT_TRUE(ParseMessage(R"({"type":"offer","from":"p1","sdp":"v=0..."})", &m));
    EXPECT_EQ(m.type, MessageType::kOffer);
    EXPECT_EQ(m.from, "p1");
    EXPECT_EQ(m.sdp, "v=0...");
}

TEST(ParseMessageTest, IceKeepsAllFields) {
    Message m;
    ASSERT_TRUE(ParseMessage(
        R"({"type":"ice","to":"sub1","mid":"0","mlineIndex":3,"candidate":"candidate:1"})", &m));
    EXPECT_EQ(m.type, MessageType::kIce);
    EXPECT_EQ(m.to, "sub1");
    EXPECT_EQ(m.mid, "0");
    EXPECT_EQ(m.mline_index, 3);
    EXPECT_EQ(m.candidate, "candidate:1");
}

TEST(ParseMessageTest, RegisterKeepsAllFields) {
    Message m;
    ASSERT_TRUE(ParseMessage(
        R"({"type":"register","role":"publisher","stream_id":"dev:0","device_id":"dev","stream_index":2})",
        &m));
    EXPECT_EQ(m.type, MessageType::kRegister);
    EXPECT_EQ(m.registration.role, PeerRole::kPublisher);
    EXPECT_EQ(m.registration.stream_id, "dev:0");
    EXPECT_EQ(m.registration.device_id, "dev");
    EXPECT_EQ(m.registration.stream_index, 2);
}

TEST(BuildRegisterLineTest, FillsDefaultStreamIdFromDeviceAndIndex) {
    RegisterRequest r;
    r.role         = PeerRole::kSubscriber;
    r.device_id    = "cam01";
    r.stream_index = 3;
    const std::string line = BuildRegisterLine(r);
    Message           m;
    ASSERT_TRUE(ParseMessage(line, &m));
    EXPECT_EQ(m.registration.role, PeerRole::kSubscriber);
    EXPECT_EQ(m.registration.stream_id, "cam01:3");
    EXPECT_EQ(m.registration.device_id, "cam01");
    EXPECT_EQ(m.registration.stream_index, 3);
}

TEST(BuildRegisterLineTest, NoDeviceUsesDefault) {
    RegisterRequest r;
    r.stream_index = 1;
    Message m;
    ASSERT_TRUE(ParseMessage(BuildRegisterLine(r), &m));
    // 默认 device 来自 RFLOW_DEFAULT_DEVICE_ID 宏（"demo_device"）
    EXPECT_EQ(m.registration.stream_id, std::string(RFLOW_DEFAULT_DEVICE_ID) + ":1");
}

TEST(BuildMessageLineTest, OfferRoundtrip) {
    Message m;
    m.type = MessageType::kOffer;
    m.from = "pub";
    m.to   = "sub";
    m.sdp  = "sdp\nwith\nnewlines";
    Message back;
    ASSERT_TRUE(ParseMessage(BuildMessageLine(m), &back));
    EXPECT_EQ(back.type, MessageType::kOffer);
    EXPECT_EQ(back.from, "pub");
    EXPECT_EQ(back.to, "sub");
    EXPECT_EQ(back.sdp, m.sdp);
}

TEST(BuildMessageLineTest, IceRoundtrip) {
    Message m;
    m.type        = MessageType::kIce;
    m.from        = "pub";
    m.to          = "sub";
    m.mid         = "0";
    m.mline_index = 1;
    m.candidate   = "candidate:foo \"bar\" \\baz";
    Message back;
    ASSERT_TRUE(ParseMessage(BuildMessageLine(m), &back));
    EXPECT_EQ(back.type, MessageType::kIce);
    EXPECT_EQ(back.mid, "0");
    EXPECT_EQ(back.mline_index, 1);
    EXPECT_EQ(back.candidate, m.candidate);
}

TEST(BuildMessageLineTest, SubscriberJoinNoBodyFields) {
    Message m;
    m.type = MessageType::kSubscriberJoin;
    m.from = "sub42";
    Message back;
    ASSERT_TRUE(ParseMessage(BuildMessageLine(m), &back));
    EXPECT_EQ(back.type, MessageType::kSubscriberJoin);
    EXPECT_EQ(back.from, "sub42");
}

TEST(ProtocolVersionTest, BuiltLinesCarryVersion) {
    Message m;
    m.type = MessageType::kOffer;
    m.sdp = "x";
    const std::string line = BuildMessageLine(m);
    EXPECT_NE(line.find("\"v\":"), std::string::npos);

    RegisterRequest r;
    r.role = PeerRole::kPublisher;
    r.stream_id = "s1";
    EXPECT_NE(BuildRegisterLine(r).find("\"v\":"), std::string::npos);
}

TEST(ProtocolVersionTest, ParsedMessageReflectsLocalVersion) {
    Message m;
    m.type = MessageType::kOffer;
    m.sdp = "y";
    Message back;
    ASSERT_TRUE(ParseMessage(BuildMessageLine(m), &back));
    EXPECT_EQ(back.protocol_version, kSignalingProtocolVersion);
}

TEST(ProtocolVersionTest, MissingVersionDefaultsToOne) {
    // 模拟旧端发出的 register（不带 v 字段），ParseMessage 应回填 1。
    const std::string legacy = R"({"type":"register","role":"publisher","stream_id":"s2"})";
    Message back;
    ASSERT_TRUE(ParseMessage(legacy, &back));
    EXPECT_EQ(back.protocol_version, 1);
    EXPECT_EQ(back.registration.role, PeerRole::kPublisher);
    EXPECT_EQ(back.registration.stream_id, "s2");
}

TEST(ProtocolVersionTest, ExplicitFutureVersionPropagatesUnchanged) {
    // 未来版本号；ParseMessage 不应该 clip。
    const std::string fut = R"({"type":"offer","v":99,"sdp":"x"})";
    Message back;
    ASSERT_TRUE(ParseMessage(fut, &back));
    EXPECT_EQ(back.protocol_version, 99);
}
