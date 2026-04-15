/**
 * Optional C++17 wrapper over the stable C ABI (commonvideo.h).
 * Use from Android NDK and Linux aarch64 hosts alike — same API surface.
 * Link libcommonvideo_linux.so / libcommonvideo_android.so per target (see docs/SCOPE.md).
 */
#ifndef COMMONVIDEO_HPP_
#define COMMONVIDEO_HPP_

#include "commonvideo.h"

#include <functional>
#include <memory>
#include <utility>

namespace commonvideo {

using Result = commonvideo_result_t;
using Handle = commonvideo_handle_t;
inline constexpr Handle kInvalidHandle = COMMONVIDEO_INVALID_HANDLE;

inline Result init(const char* config_json = nullptr) {
  return commonvideo_init(config_json);
}

inline void shutdown() { commonvideo_shutdown(); }

inline void set_log_level(commonvideo_log_level_t level) {
  commonvideo_set_log_level(level);
}

struct PeerConnectionCallbacks {
  std::function<void(Handle, commonvideo_sdp_type_t, const char* sdp)>
      on_local_description;
  std::function<void(Handle, const char* mid, int32_t mline_index,
                     const char* candidate)>
      on_ice_candidate;
  std::function<void(Handle, int32_t state)> on_connection_state;
  std::function<void(Handle, const commonvideo_stats_sample_t*)> on_stats;
};

namespace detail {

inline void trampoline_local_desc(commonvideo_handle_t pc,
                                  commonvideo_sdp_type_t type,
                                  const char* sdp, void* user) {
  auto* cb = static_cast<PeerConnectionCallbacks*>(user);
  if (cb && cb->on_local_description)
    cb->on_local_description(pc, type, sdp);
}

inline void trampoline_ice(commonvideo_handle_t pc, const char* mid,
                           int32_t mline_index, const char* candidate,
                           void* user) {
  auto* cb = static_cast<PeerConnectionCallbacks*>(user);
  if (cb && cb->on_ice_candidate)
    cb->on_ice_candidate(pc, mid, mline_index, candidate);
}

inline void trampoline_conn_state(commonvideo_handle_t pc, int32_t state,
                                  void* user) {
  auto* cb = static_cast<PeerConnectionCallbacks*>(user);
  if (cb && cb->on_connection_state)
    cb->on_connection_state(pc, state);
}

inline void trampoline_stats(commonvideo_handle_t pc,
                             const commonvideo_stats_sample_t* stats,
                             void* user) {
  auto* cb = static_cast<PeerConnectionCallbacks*>(user);
  if (cb && cb->on_stats) cb->on_stats(pc, stats);
}

}  // namespace detail

class PeerConnection {
 public:
  PeerConnection(const char* ice_servers_json, PeerConnectionCallbacks callbacks)
      : callbacks_(std::make_unique<PeerConnectionCallbacks>(
            std::move(callbacks))) {
    commonvideo_pc_callbacks_t cbs{};
    cbs.on_local_description = &detail::trampoline_local_desc;
    cbs.on_ice_candidate = &detail::trampoline_ice;
    cbs.on_connection_state = &detail::trampoline_conn_state;
    cbs.on_stats = &detail::trampoline_stats;
    cbs.user = callbacks_.get();
    commonvideo_result_t r =
        commonvideo_pc_create(ice_servers_json, &cbs, &pc_);
    if (r != COMMONVIDEO_OK) pc_ = kInvalidHandle;
  }

  ~PeerConnection() {
    if (pc_ != kInvalidHandle) {
      commonvideo_pc_destroy(pc_);
      pc_ = kInvalidHandle;
    }
  }

  PeerConnection(const PeerConnection&) = delete;
  PeerConnection& operator=(const PeerConnection&) = delete;

  PeerConnection(PeerConnection&& o) noexcept
      : pc_(o.pc_), callbacks_(std::move(o.callbacks_)) {
    o.pc_ = kInvalidHandle;
  }

  PeerConnection& operator=(PeerConnection&& o) noexcept {
    if (this != &o) {
      if (pc_ != kInvalidHandle) commonvideo_pc_destroy(pc_);
      pc_ = o.pc_;
      o.pc_ = kInvalidHandle;
      callbacks_ = std::move(o.callbacks_);
    }
    return *this;
  }

  [[nodiscard]] Handle handle() const { return pc_; }
  [[nodiscard]] bool valid() const { return pc_ != kInvalidHandle; }

  [[nodiscard]] Result create_offer() const {
    return commonvideo_pc_create_offer(pc_);
  }

  [[nodiscard]] Result create_answer() const {
    return commonvideo_pc_create_answer(pc_);
  }

  [[nodiscard]] Result set_local_description(commonvideo_sdp_type_t type,
                                             const char* sdp) const {
    return commonvideo_pc_set_local_description(pc_, type, sdp);
  }

  [[nodiscard]] Result set_remote_description(commonvideo_sdp_type_t type,
                                                const char* sdp) const {
    return commonvideo_pc_set_remote_description(pc_, type, sdp);
  }

  [[nodiscard]] Result add_ice_candidate(const char* mid, int32_t mline_index,
                                         const char* candidate) const {
    return commonvideo_pc_add_ice_candidate(pc_, mid, mline_index, candidate);
  }

  [[nodiscard]] Result add_video_track(const char* track_id) const {
    return commonvideo_pc_add_video_track(pc_, track_id);
  }

  [[nodiscard]] Result add_audio_track(const char* track_id) const {
    return commonvideo_pc_add_audio_track(pc_, track_id);
  }

 private:
  Handle pc_{kInvalidHandle};
  std::unique_ptr<PeerConnectionCallbacks> callbacks_;
};

}  // namespace commonvideo

#endif  // COMMONVIDEO_HPP_
