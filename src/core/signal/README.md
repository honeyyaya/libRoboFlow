# core/signal

This directory is the future shared signaling layer for both `src/client` and
`src/service`.

The minimum split is:

- `protocol.h/.cpp`
  - shared signaling message model
  - address parsing
  - lightweight JSON field extraction
  - signaling line parse/serialize
- `session.h`
  - transport-agnostic signaling session interface
  - shared config/delegate contract
- `signal.h`
  - module entry point that re-exports the protocol/session types

Deliberately left out of `core` for now:

- POSIX socket client implementation
- epoll-based signaling server
- publisher/subscriber routing tables
- stream-specific business rules

The intended dependency direction is:

1. `client` wraps `rflow::signal::Session` for pull-side needs.
2. `service` wraps `rflow::signal::Session` for SDK signaling client needs.
3. the standalone signaling server can reuse `protocol.h/.cpp`, but its routing
   loop stays in `service`.

Current status:

- `src/client/impl/signaling/signaling_client.*` is now a POSIX transport
  adapter that implements `rflow::signal::Session`.
- the client transport multiplexes multiple signaling sockets on one shared IO
  thread instead of one reader thread per session.
- the pull-side client only sends `answer` and `ice`; outbound `offer` is
  intentionally blocked in the transport.
- protocol parsing/serialization stays in `core/signal/protocol.*`.
