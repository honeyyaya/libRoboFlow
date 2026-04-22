#ifndef __RFLOW_CORE_THREAD_POOL_H__
#define __RFLOW_CORE_THREAD_POOL_H__

#include <functional>

namespace rflow::thread {

bool initialize(int worker_count = 0);
void shutdown();

void post(std::function<void()> fn);

}  // namespace rflow::thread

#endif  // __RFLOW_CORE_THREAD_POOL_H__
