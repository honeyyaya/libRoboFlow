#ifndef __RFLOW_INTERNAL_LAST_ERROR_H__
#define __RFLOW_INTERNAL_LAST_ERROR_H__

#include <string>

namespace rflow {

void set_last_error(const std::string& msg);
void set_last_error(const char* msg);

}  // namespace rflow

#endif  // __RFLOW_INTERNAL_LAST_ERROR_H__
