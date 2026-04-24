#include "signal/protocol.h"

#include "rflow/librflow_common.h"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace rflow::signal {

namespace {

void AppendJsonStringField(std::ostringstream& oss,
                           const char*         key,
                           std::string_view    value,
                           bool&               first_field) {
    if (!first_field) {
        oss << ',';
    }
    first_field = false;
    oss << '"' << key << "\":\"" << EscapeJsonString(value) << '"';
}

void AppendJsonIntField(std::ostringstream& oss,
                        const char*         key,
                        int                 value,
                        bool&               first_field) {
    if (!first_field) {
        oss << ',';
    }
    first_field = false;
    oss << '"' << key << "\":" << value;
}

}  // namespace

const char* ToString(PeerRole role) {
    switch (role) {
        case PeerRole::kPublisher:  return "publisher";
        case PeerRole::kSubscriber: return "subscriber";
        default:                    return "unknown";
    }
}

PeerRole PeerRoleFromString(std::string_view role) {
    if (role == "publisher")  return PeerRole::kPublisher;
    if (role == "subscriber") return PeerRole::kSubscriber;
    return PeerRole::kUnknown;
}

const char* ToString(MessageType type) {
    switch (type) {
        case MessageType::kRegister:         return "register";
        case MessageType::kWelcome:          return "welcome";
        case MessageType::kOffer:            return "offer";
        case MessageType::kAnswer:           return "answer";
        case MessageType::kIce:              return "ice";
        case MessageType::kSubscriberJoin:   return "subscriber_join";
        case MessageType::kSubscriberLeave:  return "subscriber_leave";
        default:                             return "unknown";
    }
}

MessageType MessageTypeFromString(std::string_view type) {
    if (type == "register")         return MessageType::kRegister;
    if (type == "welcome")          return MessageType::kWelcome;
    if (type == "offer")            return MessageType::kOffer;
    if (type == "answer")           return MessageType::kAnswer;
    if (type == "ice")              return MessageType::kIce;
    if (type == "subscriber_join")  return MessageType::kSubscriberJoin;
    if (type == "subscriber_leave") return MessageType::kSubscriberLeave;
    return MessageType::kUnknown;
}

bool ParseEndpoint(std::string_view addr, Endpoint* out) {
    if (out == nullptr) {
        return false;
    }

    std::string s(addr);
    if (s.rfind("ws://", 0) == 0) {
        s = s.substr(5);
    } else if (s.rfind("tcp://", 0) == 0) {
        s = s.substr(6);
    }

    const size_t colon = s.find(':');
    if (colon == std::string::npos) {
        out->host = s;
        out->port = 8765;
        return !out->host.empty();
    }

    out->host = s.substr(0, colon);
    out->port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
    return !out->host.empty();
}

std::string EscapeJsonString(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string ExtractJsonString(std::string_view line, std::string_view key) {
    const std::string token = std::string("\"") + std::string(key) + "\":\"";
    size_t p = line.find(token);
    if (p == std::string::npos) {
        return "";
    }
    p += token.size();

    std::string out;
    out.reserve(line.size() - p);
    for (size_t i = p; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            const char n = line[i + 1];
            if (n == 'n') {
                out.push_back('\n');
            } else if (n == 'r') {
                out.push_back('\r');
            } else {
                out.push_back(n);
            }
            ++i;
            continue;
        }
        if (line[i] == '"') {
            break;
        }
        out.push_back(line[i]);
    }
    return out;
}

int ExtractJsonInt(std::string_view line, std::string_view key, int fallback) {
    const std::string token = std::string("\"") + std::string(key) + "\":";
    size_t p = line.find(token);
    if (p == std::string::npos) {
        return fallback;
    }
    p += token.size();
    return std::atoi(std::string(line.substr(p)).c_str());
}

bool ParseMessage(std::string_view line, Message* out) {
    if (out == nullptr || line.empty()) {
        return false;
    }

    Message msg;
    msg.type = MessageTypeFromString(ExtractJsonString(line, "type"));
    if (msg.type == MessageType::kUnknown) {
        return false;
    }

    msg.from = ExtractJsonString(line, "from");
    msg.to = ExtractJsonString(line, "to");

    switch (msg.type) {
        case MessageType::kRegister:
            msg.registration.role = PeerRoleFromString(ExtractJsonString(line, "role"));
            msg.registration.stream_id = ExtractJsonString(line, "stream_id");
            msg.registration.device_id = ExtractJsonString(line, "device_id");
            msg.registration.stream_index = ExtractJsonInt(line, "stream_index", -1);
            break;
        case MessageType::kWelcome:
            msg.peer_id = ExtractJsonString(line, "id");
            break;
        case MessageType::kOffer:
        case MessageType::kAnswer:
            msg.sdp = ExtractJsonString(line, "sdp");
            break;
        case MessageType::kIce:
            msg.mid = ExtractJsonString(line, "mid");
            msg.mline_index = ExtractJsonInt(line, "mlineIndex", 0);
            msg.candidate = ExtractJsonString(line, "candidate");
            break;
        case MessageType::kSubscriberJoin:
        case MessageType::kSubscriberLeave:
            break;
        default:
            break;
    }

    *out = std::move(msg);
    return true;
}

std::string BuildRegisterLine(const RegisterRequest& req) {
    // 与 service/stream.cpp 中 stream_id 规则一致；否则 subscriber 缺省 stream_id 时
    // 信令服务器会落到 "livestream"，与 publisher 的 "device_id:idx" 不一致，永远收不到 offer。
    RegisterRequest r = req;
    if (r.stream_id.empty() && r.stream_index >= 0) {
        std::string dev = r.device_id;
        if (dev.empty()) {
            dev = RFLOW_DEFAULT_DEVICE_ID;
        }
        r.stream_id = dev + ":" + std::to_string(r.stream_index);
    }

    std::ostringstream oss;
    bool first_field = true;
    oss << '{';
    AppendJsonStringField(oss, "type", "register", first_field);
    if (r.role != PeerRole::kUnknown) {
        AppendJsonStringField(oss, "role", ToString(r.role), first_field);
    }
    if (!r.stream_id.empty()) {
        AppendJsonStringField(oss, "stream_id", r.stream_id, first_field);
    }
    if (!r.device_id.empty()) {
        AppendJsonStringField(oss, "device_id", r.device_id, first_field);
    }
    if (r.stream_index >= 0) {
        AppendJsonIntField(oss, "stream_index", r.stream_index, first_field);
    }
    oss << '}';
    return oss.str();
}

std::string BuildMessageLine(const Message& msg) {
    std::ostringstream oss;
    bool first_field = true;
    oss << '{';
    AppendJsonStringField(oss, "type", ToString(msg.type), first_field);

    if (!msg.from.empty()) {
        AppendJsonStringField(oss, "from", msg.from, first_field);
    }
    if (!msg.to.empty()) {
        AppendJsonStringField(oss, "to", msg.to, first_field);
    }

    switch (msg.type) {
        case MessageType::kRegister:
            if (msg.registration.role != PeerRole::kUnknown) {
                AppendJsonStringField(oss, "role", ToString(msg.registration.role), first_field);
            }
            if (!msg.registration.stream_id.empty()) {
                AppendJsonStringField(oss, "stream_id", msg.registration.stream_id, first_field);
            }
            if (!msg.registration.device_id.empty()) {
                AppendJsonStringField(oss, "device_id", msg.registration.device_id, first_field);
            }
            if (msg.registration.stream_index >= 0) {
                AppendJsonIntField(oss, "stream_index", msg.registration.stream_index, first_field);
            }
            break;
        case MessageType::kWelcome:
            if (!msg.peer_id.empty()) {
                AppendJsonStringField(oss, "id", msg.peer_id, first_field);
            }
            break;
        case MessageType::kOffer:
        case MessageType::kAnswer:
            AppendJsonStringField(oss, "sdp", msg.sdp, first_field);
            break;
        case MessageType::kIce:
            AppendJsonStringField(oss, "mid", msg.mid, first_field);
            AppendJsonIntField(oss, "mlineIndex", msg.mline_index, first_field);
            AppendJsonStringField(oss, "candidate", msg.candidate, first_field);
            break;
        case MessageType::kSubscriberJoin:
        case MessageType::kSubscriberLeave:
        case MessageType::kUnknown:
            break;
    }

    oss << '}';
    return oss.str();
}

}  // namespace rflow::signal
