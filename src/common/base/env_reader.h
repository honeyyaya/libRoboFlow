#ifndef __RFLOW_COMMON_BASE_ENV_READER_H__
#define __RFLOW_COMMON_BASE_ENV_READER_H__

#include <cstdlib>

namespace rflow::common::base {

inline int ReadEnvIntInRange(const char* name, int fallback, int min_v, int max_v) {
    const char* env_value = std::getenv(name);
    if (!env_value || !env_value[0]) return fallback;
    char* parse_end = nullptr;
    const long parsed_value = std::strtol(env_value, &parse_end, 10);
    if (parse_end == env_value || (parse_end && *parse_end != '\0')) return fallback;
    if (parsed_value < min_v) return min_v;
    if (parsed_value > max_v) return max_v;
    return static_cast<int>(parsed_value);
}

inline bool ReadEnvBool(const char* name, bool fallback) {
    const char* env_value = std::getenv(name);
    if (!env_value || !env_value[0]) return fallback;
    if (env_value[0] == '1' || env_value[0] == 'y' || env_value[0] == 'Y' || env_value[0] == 't' ||
        env_value[0] == 'T') {
        return true;
    }
    if (env_value[0] == '0' || env_value[0] == 'n' || env_value[0] == 'N' || env_value[0] == 'f' ||
        env_value[0] == 'F') {
        return false;
    }
    return fallback;
}

inline size_t ReadEnvSizeInRange(const char* name, size_t fallback, size_t min_v, size_t max_v) {
    const char* env_value = std::getenv(name);
    if (!env_value || !env_value[0]) return fallback;
    char* parse_end = nullptr;
    const unsigned long long parsed_value = std::strtoull(env_value, &parse_end, 10);
    if (parse_end == env_value || (parse_end && *parse_end != '\0')) return fallback;
    if (parsed_value < min_v) return min_v;
    if (parsed_value > max_v) return max_v;
    return static_cast<size_t>(parsed_value);
}

}  // namespace rflow::common::base

namespace rflow::common::util {
using rflow::common::base::ReadEnvBool;
using rflow::common::base::ReadEnvIntInRange;
using rflow::common::base::ReadEnvSizeInRange;
}  // namespace rflow::common::util

#endif  // __RFLOW_COMMON_BASE_ENV_READER_H__
