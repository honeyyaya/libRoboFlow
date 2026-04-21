#include "robrt/librobrt_common.h"

#include <cstdlib>

extern "C" {

void librobrt_string_free(char* p) { std::free(p); }
void librobrt_buffer_free(void* p) { std::free(p); }

}  // extern "C"
