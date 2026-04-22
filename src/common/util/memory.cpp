#include "rflow/librflow_common.h"

#include <cstdlib>

extern "C" {

void librflow_string_free(char* p) { std::free(p); }
void librflow_buffer_free(void* p) { std::free(p); }

}  // extern "C"
