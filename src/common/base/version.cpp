#include "rflow/librflow_common.h"
#include "rflow/librflow_version.h"

extern "C" {

void librflow_get_version(uint32_t* major, uint32_t* minor, uint32_t* patch) {
    if (major) *major = LIBRFLOW_MAJOR;
    if (minor) *minor = LIBRFLOW_MINOR;
    if (patch) *patch = LIBRFLOW_MICRO;
}

const char* librflow_get_build_info(void) {
    return "rflow "
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
