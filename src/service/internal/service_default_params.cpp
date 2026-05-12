#include "internal/service_default_params.h"

namespace rflow::service::internal {

const ServiceDefaultParams& GetServiceDefaultParams() {
    // SDK 默认值保持稳定；业务侧需要覆盖时使用 stream_param / global_config API。
    static const ServiceDefaultParams params{};
    return params;
}

}  // namespace rflow::service::internal
