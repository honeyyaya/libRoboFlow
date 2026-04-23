/**
 * @file   server.h
 * @brief  Standalone signaling server entry point; shared between client/service, apps.
 *         Implements minimal publisher/subscriber routing over TCP + epoll using
 *         the line protocol defined in core/signal/protocol.h.
 */

#ifndef __RFLOW_CORE_SIGNAL_SERVER_H__
#define __RFLOW_CORE_SIGNAL_SERVER_H__

namespace rflow::signal::server {

int RunMain(int argc, char* argv[]);

}  // namespace rflow::signal::server

#endif  // __RFLOW_CORE_SIGNAL_SERVER_H__

