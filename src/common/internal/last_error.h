#ifndef __ROBRT_INTERNAL_LAST_ERROR_H__
#define __ROBRT_INTERNAL_LAST_ERROR_H__

#include <string>

namespace robrt {

void set_last_error(const std::string& msg);
void set_last_error(const char* msg);

}  // namespace robrt

#endif  // __ROBRT_INTERNAL_LAST_ERROR_H__
