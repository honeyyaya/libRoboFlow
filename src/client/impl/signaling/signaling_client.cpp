#include "signaling_client.h"

#include "common/internal/logger.h"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

// NOTE: 旧工程同样是 POSIX socket；Android/Linux 可直接编译。
// TODO: 后续抽象为 core/net 下的跨平台 socket，或切换到已有的 core/signal 接入。
#if defined(_WIN32)
#  error "signaling_client.cpp targets POSIX; 在 Windows 上构建前需要替换 socket 层"
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rflow::client::impl {

namespace {

void ParseHostPort(const std::string& addr, std::string& host, uint16_t& port) {
    std::string s = addr;
    if (s.rfind("ws://",  0) == 0) s = s.substr(5);
    else if (s.rfind("tcp://", 0) == 0) s = s.substr(6);
    const size_t colon = s.find(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
    } else {
        host = s;
        port = 8765;
    }
}

void JsonEscape(std::ostringstream& oss, const std::string& in, bool include_ctrl) {
    for (char c : in) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': if (include_ctrl) { oss << "\\n"; } else { oss << c; } break;
            case '\r': if (include_ctrl) { oss << "\\r"; } else { oss << c; } break;
            default:   oss << c;
        }
    }
}

}  // namespace

SignalingClient::SignalingClient(std::string server_addr, std::string role)
    : server_addr_(std::move(server_addr)), role_(std::move(role)) {
    ParseHostPort(server_addr_, host_, port_);
}

SignalingClient::~SignalingClient() {
    Stop();
}

bool SignalingClient::Connect() {
    RFLOW_LOGI("[Signaling] connect %s:%u role=%s", host_.c_str(), port_, role_.c_str());
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (on_error_) on_error_(std::string("socket: ") + strerror(errno));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        if (on_error_) on_error_("invalid host: " + host_);
        ::close(fd);
        return false;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (on_error_) on_error_(std::string("connect: ") + strerror(errno));
        ::close(fd);
        return false;
    }
    sock_fd_.store(fd, std::memory_order_release);
    RFLOW_LOGI("[Signaling] tcp connected");

    std::ostringstream reg;
    reg << "{\"type\":\"register\",\"role\":\"" << role_ << "\"}\n";
    const std::string msg = reg.str();
    if (::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(msg.size())) {
        if (on_error_) on_error_("send register failed");
        ::close(fd);
        sock_fd_.store(-1, std::memory_order_release);
        return false;
    }
    RFLOW_LOGI("[Signaling] registered role=%s", role_.c_str());
    return true;
}

bool SignalingClient::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return true;
    if (!Connect()) {
        running_.store(false, std::memory_order_release);
        return false;
    }
    reader_thread_ = std::make_unique<std::thread>(&SignalingClient::ReaderLoop, this);
    return true;
}

void SignalingClient::Stop() {
    running_.store(false, std::memory_order_release);
    int fd = sock_fd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
    if (reader_thread_ && reader_thread_->joinable()) {
        reader_thread_->join();
    }
    reader_thread_.reset();
}

void SignalingClient::SendLine(const std::string& line) {
    int fd = sock_fd_.load(std::memory_order_acquire);
    if (fd < 0) return;
    const std::string msg = line + "\n";
    const ssize_t sent = ::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    (void)sent;
}

void SignalingClient::SendOffer(const std::string& sdp) {
    std::ostringstream oss;
    oss << "{\"type\":\"offer\",\"sdp\":\"";
    JsonEscape(oss, sdp, /*include_ctrl=*/true);
    oss << "\"}";
    SendLine(oss.str());
}

void SignalingClient::SendAnswer(const std::string& sdp) {
    std::ostringstream oss;
    oss << "{\"type\":\"answer\",\"sdp\":\"";
    JsonEscape(oss, sdp, /*include_ctrl=*/true);
    oss << "\"}";
    RFLOW_LOGI("[Signaling] send answer bytes=%zu", sdp.size());
    SendLine(oss.str());
}

void SignalingClient::SendIceCandidate(const std::string& mid, int mline_index,
                                       const std::string& candidate) {
    RFLOW_LOGD("[Signaling] send ice mid=%s mline=%d len=%zu",
               mid.c_str(), mline_index, candidate.size());
    std::ostringstream oss;
    oss << "{\"type\":\"ice\",\"mid\":\"" << mid << "\","
        << "\"mlineIndex\":" << mline_index << ","
        << "\"candidate\":\"";
    JsonEscape(oss, candidate, /*include_ctrl=*/false);
    oss << "\"}";
    SendLine(oss.str());
}

void SignalingClient::ReaderLoop() {
    std::string buf;
    char tmp[65536];
    while (running_.load(std::memory_order_acquire)) {
        int fd = sock_fd_.load(std::memory_order_acquire);
        if (fd < 0) break;
        ssize_t n = ::recv(fd, tmp, sizeof(tmp) - 1, 0);
        if (n > 0) {
            tmp[n] = '\0';
            buf.append(tmp, static_cast<size_t>(n));
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                ParseAndDispatch(line);
            }
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
}

// TODO: 替换为 nlohmann::json 或类似库；当前手写 parser 仅满足旧协议。
void SignalingClient::ParseAndDispatch(const std::string& line) {
    if (line.empty()) return;
    if (line.find("\"type\":\"register\"") != std::string::npos) return;

    auto extract_sdp = [&](const std::string& tag, std::string& out) -> bool {
        size_t p = line.find(tag);
        if (p == std::string::npos) return false;
        p += tag.size();
        for (size_t i = p; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                char n = line[i + 1];
                if      (n == 'n') out += '\n';
                else if (n == 'r') out += '\r';
                else if (n == '"') out += '"';
                else               out += n;
                ++i;
            } else if (line[i] == '"') {
                break;
            } else {
                out += line[i];
            }
        }
        return true;
    };

    if (line.find("\"type\":\"answer\"") != std::string::npos ||
        line.find("\"type\": \"answer\"") != std::string::npos) {
        std::string sdp;
        if (extract_sdp("\"sdp\":\"", sdp) && on_answer_) on_answer_("answer", sdp);
        return;
    }
    if (line.find("\"type\":\"offer\"") != std::string::npos ||
        line.find("\"type\": \"offer\"") != std::string::npos) {
        std::string sdp;
        if (extract_sdp("\"sdp\":\"", sdp)) {
            RFLOW_LOGI("[Signaling] recv offer bytes=%zu", sdp.size());
            if (on_offer_) on_offer_("offer", sdp);
        }
        return;
    }
    if (line.find("\"type\":\"ice\"") != std::string::npos ||
        line.find("\"type\": \"ice\"") != std::string::npos) {
        std::string mid;
        std::string candidate;
        int mline = 0;
        // "\"mid\":\"" 长度为 7
        size_t p = line.find("\"mid\":\"");
        if (p != std::string::npos) {
            p += 7;
            const size_t end = line.find('"', p);
            if (end != std::string::npos) mid = line.substr(p, end - p);
        }
        p = line.find("\"mlineIndex\":");
        if (p != std::string::npos) mline = std::atoi(line.c_str() + p + 13);
        p = line.find("\"candidate\":\"");
        if (p != std::string::npos) {
            p += 13;
            for (size_t i = p; i < line.size(); ++i) {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    candidate += line[i + 1];
                    ++i;
                } else if (line[i] == '"') {
                    break;
                } else {
                    candidate += line[i];
                }
            }
        }
        RFLOW_LOGD("[Signaling] recv ice mid=%s mline=%d len=%zu",
                   mid.c_str(), mline, candidate.size());
        if (on_ice_ && !candidate.empty()) on_ice_(mid, mline, candidate);
    }
}

}  // namespace rflow::client::impl
