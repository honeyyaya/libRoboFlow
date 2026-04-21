#ifndef __ROBRT_CORE_THREAD_POOL_H__
#define __ROBRT_CORE_THREAD_POOL_H__

#include <functional>

namespace robrt::thread {

bool initialize(int worker_count = 0);
void shutdown();

void post(std::function<void()> fn);

}  // namespace robrt::thread

#endif  // __ROBRT_CORE_THREAD_POOL_H__
