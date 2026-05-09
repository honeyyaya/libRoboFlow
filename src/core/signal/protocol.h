#ifndef __RFLOW_CORE_SIGNAL_PROTOCOL_H__
#define __RFLOW_CORE_SIGNAL_PROTOCOL_H__

#include <cstdint>
#include <string>
#include <string_view>

namespace rflow::signal {

/// 信令线协议版本。
///   1 = 历史不带 "v" 字段的旧线协议（兼容窗口）。
///   2 = 引入 "v" 字段；旧端收到不识别会忽略，新端收到无 "v" 视作 1。
/// 升级语义：仅在协议字段集合发生不可向后兼容的扩展时增加。本次只是把版本
/// 号挂上线，没有破坏性改动；server / client 任意一端均可逐步升级。
constexpr int kSignalingProtocolVersion = 2;

enum class PeerRole {
    kUnknown = 0,
    kPublisher,
    kSubscriber,
};

enum class MessageType {
    kUnknown = 0,
    kRegister,
    kWelcome,
    kOffer,
    kAnswer,
    kIce,
    kSubscriberJoin,
    kSubscriberLeave,
};

struct Endpoint {
    std::string host;
    uint16_t    port{8765};
};

struct RegisterRequest {
    PeerRole    role{PeerRole::kUnknown};
    std::string stream_id;
    std::string device_id;
    int32_t     stream_index{-1};
};

struct Message {
    MessageType type{MessageType::kUnknown};
    std::string from;
    std::string to;

    std::string peer_id;
    std::string sdp;
    std::string mid;
    int         mline_index{0};
    std::string candidate;

    RegisterRequest registration;

    /// 对端声明的协议版本；ParseMessage 不存在 "v" 字段时填 1（旧端兼容）。
    /// BuildMessageLine / BuildRegisterLine 输出时自动写入 kSignalingProtocolVersion。
    int protocol_version{1};
};

const char* ToString(PeerRole role);
PeerRole    PeerRoleFromString(std::string_view role);

const char* ToString(MessageType type);
MessageType MessageTypeFromString(std::string_view type);

bool ParseEndpoint(std::string_view addr, Endpoint* out);

std::string EscapeJsonString(std::string_view input);
std::string ExtractJsonString(std::string_view line, std::string_view key);
int         ExtractJsonInt(std::string_view line, std::string_view key, int fallback = 0);

bool        ParseMessage(std::string_view line, Message* out);
std::string BuildRegisterLine(const RegisterRequest& req);
std::string BuildMessageLine(const Message& msg);

}  // namespace rflow::signal

#endif  // __RFLOW_CORE_SIGNAL_PROTOCOL_H__
