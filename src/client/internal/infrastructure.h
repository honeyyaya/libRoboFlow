/**
 * @file   infrastructure.h
 * @brief  Client 侧子系统编排：C API(lifecycle) 不直接写死 core 调用顺序，便于扩展 hook。
 *
 * 顺序与约定：
 *   - init / shutdown_infrastructure：thread → rtc → signal；shutdown 为逆序。不含业务拉流管理。
 *   - on_connect_succeeded / on_disconnect：在已 init、且信令/连接层就绪或断开时，启停 RtcStreamManager（仅 WebRTC 构建）。
 */

#ifndef __RFLOW_CLIENT_INFRASTRUCTURE_H__
#define __RFLOW_CLIENT_INFRASTRUCTURE_H__

#include <string>

#include "rflow/librflow_common.h"

namespace rflow::client {

rflow_err_t init_infrastructure();
void        shutdown_infrastructure();

/**
 * 连接成功后启用拉流子系统。
 * @param signal_url  信令服务器地址；空串走 impl 内置默认
 * @param device_id   设备 ID（会带入 signaling register 扩展字段）
 */
rflow_err_t on_connect_succeeded(const std::string& signal_url,
                                 const std::string& device_id);
void        on_disconnect();

}  // namespace rflow::client

#endif  // __RFLOW_CLIENT_INFRASTRUCTURE_H__
