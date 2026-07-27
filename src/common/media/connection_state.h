#ifndef __RFLOW_COMMON_MEDIA_CONNECTION_STATE_H__
#define __RFLOW_COMMON_MEDIA_CONNECTION_STATE_H__

namespace rflow::common::media {

enum class ConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed,
};

}  // namespace rflow::common::media

#endif  // __RFLOW_COMMON_MEDIA_CONNECTION_STATE_H__
