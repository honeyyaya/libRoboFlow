#include "robrt/librobrt_common.h"
#include "robrt/librobrt_version.h"

extern "C" {

void librobrt_get_version(uint32_t* major, uint32_t* minor, uint32_t* patch) {
    if (major) *major = LIBROBRT_MAJOR;
    if (minor) *minor = LIBROBRT_MINOR;
    if (patch) *patch = LIBROBRT_MICRO;
}

const char* librobrt_get_build_info(void) {
    return "robrt "
#ifdef COMMIT_VERSION
           COMMIT_VERSION
#else
           "unknown"
#endif
           " built at "
#ifdef BUILD_TIME
           BUILD_TIME
#else
           "unknown"
#endif
           ;
}

}  // extern "C"
