#ifndef __RFLOW_CORE_NET_POSIX_IO_H__
#define __RFLOW_CORE_NET_POSIX_IO_H__

#include <cstdint>
#include <cstddef>
#include <string>

namespace rflow::net {

// Internal utility: set socket/file descriptor non-blocking mode.
void SetNonBlocking(int fd);
// Internal utility: send full buffer on non-blocking socket with poll-based wait.
bool WriteAllWithPoll(int fd, const char* data, size_t len, int timeout_ms);
// Internal utility: receive until first newline with timeout budget.
bool RecvUntilNewline(int fd, std::string* first_line, std::string* rest, int timeout_ms);
// Internal utility: parse "host:port"/"ws://host:port"/"tcp://host:port".
void ParseHostPort(const std::string& addr, std::string& host, uint16_t& port);

}  // namespace rflow::net

#endif  // __RFLOW_CORE_NET_POSIX_IO_H__

