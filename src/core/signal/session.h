#ifndef __RFLOW_CORE_SIGNAL_SESSION_H__
#define __RFLOW_CORE_SIGNAL_SESSION_H__

#include "signal/protocol.h"

#include <string_view>

namespace rflow::signal {

struct SessionConfig {
    std::string     server_addr;
    RegisterRequest registration;
    bool            remember_last_remote_peer{true};
};

class SessionDelegate {
 public:
    virtual ~SessionDelegate() = default;

    virtual void OnSignalMessage(const Message& msg) = 0;
    virtual void OnSignalError(std::string_view error) = 0;
};

class Session {
 public:
    virtual ~Session() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Send(const Message& msg) = 0;
    virtual bool IsRunning() const = 0;
    virtual void SetDelegate(SessionDelegate* delegate) = 0;
};

}  // namespace rflow::signal

#endif  // __RFLOW_CORE_SIGNAL_SESSION_H__
