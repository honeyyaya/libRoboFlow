/**
 * CommonVideoSDK — stable C ABI (Linux aarch64 & Android arm64-v8a).
 * Hosts link platform shared libraries:
 * libcommonvideo_linux.so (Linux aarch64), libcommonvideo_android.so (Android arm64-v8a).
 * Optional C++ wrapper: commonvideo.hpp. Do not include other C++ headers from this API surface.
 */
#ifndef COMMONVIDEO_H_
#define COMMONVIDEO_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  define COMMONVIDEO_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && defined(COMMONVIDEO_BUILD_SHARED)
#  define COMMONVIDEO_EXPORT __attribute__((visibility("default")))
#else
#  define COMMONVIDEO_EXPORT
#endif

/** Opaque handles; 0 is never valid. */
typedef uint64_t commonvideo_handle_t;

#define COMMONVIDEO_INVALID_HANDLE ((commonvideo_handle_t)0)

/**
 * API result codes. Stable across minor versions; new codes append only.
 */
typedef enum commonvideo_result {
  COMMONVIDEO_OK = 0,
  COMMONVIDEO_ERR_INVALID_ARG = 1,
  COMMONVIDEO_ERR_INVALID_STATE = 2,
  COMMONVIDEO_ERR_OUT_OF_MEMORY = 3,
  COMMONVIDEO_ERR_INTERNAL = 4,
  COMMONVIDEO_ERR_NOT_INITIALIZED = 5,
  COMMONVIDEO_ERR_TIMEOUT = 6,
  COMMONVIDEO_ERR_MEDIA = 7,
  COMMONVIDEO_ERR_NETWORK = 8,
} commonvideo_result_t;

/**
 * SDP type for local/remote description.
 */
typedef enum commonvideo_sdp_type {
  COMMONVIDEO_SDP_TYPE_OFFER = 0,
  COMMONVIDEO_SDP_TYPE_ANSWER = 1,
  COMMONVIDEO_SDP_TYPE_PRANSWER = 2,
  COMMONVIDEO_SDP_TYPE_ROLLBACK = 3,
} commonvideo_sdp_type_t;

/**
 * Logging severity for commonvideo_set_log_level.
 */
typedef enum commonvideo_log_level {
  COMMONVIDEO_LOG_VERBOSE = 0,
  COMMONVIDEO_LOG_INFO = 1,
  COMMONVIDEO_LOG_WARNING = 2,
  COMMONVIDEO_LOG_ERROR = 3,
  COMMONVIDEO_LOG_NONE = 4,
} commonvideo_log_level_t;

/**
 * Threading: unless noted, API functions may be called from any thread but
 * MUST NOT be re-entered from within the same API's callback on the SDK
 * worker thread without deferring (implementation may use internal locks).
 *
 * Callbacks (on_*): always invoked from SDK-owned worker thread(s), not
 * Android main thread. Host must not block indefinitely inside callbacks.
 */

typedef struct commonvideo_stats_sample {
  uint32_t packets_sent;
  uint32_t packets_lost;
  uint64_t bytes_sent;
  int32_t rtt_ms; /* -1 if unknown */
} commonvideo_stats_sample_t;

/* ---------- Global lifecycle ---------- */

COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_init(const char* config_json);
/**
 * config_json: optional JSON for global flags (e.g. {"ice_servers":[...]}).
 * May be NULL for defaults. UTF-8, null-terminated.
 */

COMMONVIDEO_EXPORT void commonvideo_shutdown(void);

COMMONVIDEO_EXPORT void commonvideo_set_log_level(commonvideo_log_level_t level);

/* ---------- PeerConnection ---------- */

typedef void (*commonvideo_on_local_description_fn)(
    commonvideo_handle_t pc, commonvideo_sdp_type_t type, const char* sdp,
    void* user);
typedef void (*commonvideo_on_ice_candidate_fn)(
    commonvideo_handle_t pc, const char* mid, int32_t mline_index,
    const char* candidate, void* user);
typedef void (*commonvideo_on_connection_state_fn)(
    commonvideo_handle_t pc, int32_t state, void* user);
typedef void (*commonvideo_on_stats_fn)(
    commonvideo_handle_t pc, const commonvideo_stats_sample_t* stats,
    void* user);

typedef struct commonvideo_pc_callbacks {
  commonvideo_on_local_description_fn on_local_description;
  commonvideo_on_ice_candidate_fn on_ice_candidate;
  commonvideo_on_connection_state_fn on_connection_state;
  commonvideo_on_stats_fn on_stats;
  void* user;
} commonvideo_pc_callbacks_t;

/**
 * Creates a PeerConnection. Returns handle via out_pc.
 * ice_servers_json: UTF-8 JSON array, e.g.
 * [{"urls":"stun:stun.l.google.com:19302"}, {"urls":"turn:...","username":"u","credential":"p"}]
 */
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_create(
    const char* ice_servers_json,
    const commonvideo_pc_callbacks_t* callbacks,
    commonvideo_handle_t* out_pc);

COMMONVIDEO_EXPORT void commonvideo_pc_destroy(commonvideo_handle_t pc);

/**
 * Offer/Answer: triggers on_local_description when ready.
 */
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_create_offer(
    commonvideo_handle_t pc);
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_create_answer(
    commonvideo_handle_t pc);

COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_set_local_description(
    commonvideo_handle_t pc, commonvideo_sdp_type_t type, const char* sdp);
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_set_remote_description(
    commonvideo_handle_t pc, commonvideo_sdp_type_t type, const char* sdp);

COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_add_ice_candidate(
    commonvideo_handle_t pc, const char* mid, int32_t mline_index,
    const char* candidate);

/**
 * Optional: Phase 2 send path — add custom video/audio track.
 * Not implemented in Phase 1; returns COMMONVIDEO_ERR_INVALID_STATE if unsupported.
 */
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_add_video_track(
    commonvideo_handle_t pc, const char* track_id);
COMMONVIDEO_EXPORT commonvideo_result_t commonvideo_pc_add_audio_track(
    commonvideo_handle_t pc, const char* track_id);

#ifdef __cplusplus
}
#endif

#endif /* COMMONVIDEO_H_ */
