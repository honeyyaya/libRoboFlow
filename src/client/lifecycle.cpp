/**
 * @file   lifecycle.cpp
 * @brief  set_global_config / init / uninit / connect / disconnect
**/

#include "rflow/Client/librflow_client_api.h"
#include "rflow/librflow_common.h"

#include "internal/handles.h"
#include "internal/state_ops.h"
#include "internal/state.h"

#include "common/abi/object_layouts.h"
#include "common/base/global_config_ops.h"
#include "common/public/last_error_api.h"
#include "common/public/logger_api.h"

#include "internal/infrastructure.h"

#include <string>
#include <utility>
#include <vector>

namespace {
constexpr const char* kErrorOrigin = "client/lifecycle";
}  // namespace

namespace rflow::client {

State& state() {
    static State s;
    return s;
}

}  // namespace rflow::client

extern "C" {

rflow_err_t librflow_set_global_config(librflow_global_config_t cfg) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kUninit) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "set_global_config must be called before init",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    if (!rflow::common::base::IsValidGlobalConfigHandle(cfg)) {
        rflow::set_last_error(RFLOW_ERR_PARAM,
                              "invalid global_config handle",
                              kErrorOrigin);
        return RFLOW_ERR_PARAM;
    }

    rflow::common::base::CopyGlobalConfig(s.global_config, *cfg);
    rflow::common::base::ApplyLogConfigIfPresent(s.global_config);
    return RFLOW_OK;
}

rflow_err_t librflow_init(void) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);

    if (s.lifecycle != rflow::client::LifecycleState::kUninit) return RFLOW_OK;

    rflow_err_t err = rflow::client::init_infrastructure();
    if (err != RFLOW_OK) return err;

    s.lifecycle = rflow::client::LifecycleState::kInited;
    RFLOW_LOGI("librflow_init OK");
    return RFLOW_OK;
}

rflow_err_t librflow_uninit(void) {
    auto& s = rflow::client::state();

    // disconnect 内部有独立锁逻辑，此处先脱锁调用，再做全局清理
    librflow_disconnect();

    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle == rflow::client::LifecycleState::kUninit) return RFLOW_OK;

    s.streams.clear();
    rflow::client::shutdown_infrastructure();

    s.lifecycle = rflow::client::LifecycleState::kUninit;
    RFLOW_LOGI("librflow_uninit OK");
    return RFLOW_OK;
}

rflow_err_t librflow_connect(librflow_connect_info_t info,
                              librflow_connect_cb_t   cb) {
    if (!info || info->magic != rflow::client::kMagicConnectInfo) return RFLOW_ERR_PARAM;
    if (!cb   || cb->magic   != rflow::client::kMagicConnectCb)   return RFLOW_ERR_PARAM;

    auto& s = rflow::client::state();

    std::string signal_url;
    std::string device_id;
    librflow_connect_cb_s cb_copy{};

    {
        std::lock_guard<std::mutex> lk(s.mu);
        const auto rc = rflow::client::internal::ValidateConnectTransitionLocked(s);
        if (rc == RFLOW_ERR_STATE && s.lifecycle == rflow::client::LifecycleState::kUninit) {
            rflow::set_last_error(RFLOW_ERR_STATE,
                                  "must call librflow_init before connect",
                                  kErrorOrigin);
            return rc;
        }
        if (rc != RFLOW_OK) return rc;
        rflow::client::internal::ApplyConnectTransitionLocked(s, *info, *cb, &signal_url, &device_id, &cb_copy);
    }

    // 设计说明：当前 client SDK 不维护 device 级长连鉴权通道。
    //   - 设备 ↔ 平台的 control plane（业务通知 / 业务请求）由信令服务器之外的
    //     业务后台单独承担；SDK 仅负责 RTC 信令子系统的拉起。
    //   - 因此 librflow_connect 在校验配置后仅会启动 RtcStreamManager，每路
    //     librflow_open_stream 才真正建立到信令服务器的 TCP 会话（每流一会话）。
    //   - 若未来引入设备鉴权握手，应在此处先建立 control session、握手成功后再
    //     切 kConnected；当前不需要，故不在此处启动新连接。
    rflow_err_t err = rflow::client::on_connect_succeeded(signal_url, device_id);

    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (err != RFLOW_OK) {
            rflow::client::internal::RollbackConnectFailedLocked(s);
            return err;
        }
        rflow::client::internal::CommitConnectedLocked(s);
    }

    // 用户回调在锁外触发，避免与 state.mu 重入
    if (cb_copy.on_state) {
        cb_copy.on_state(RFLOW_CONN_CONNECTED, RFLOW_OK, cb_copy.userdata);
    }
    RFLOW_LOGI("librflow_connect OK (signal=%s device_id=%s)",
               signal_url.c_str(), device_id.c_str());
    return RFLOW_OK;
}

rflow_err_t librflow_disconnect(void) {
    auto& s = rflow::client::state();

    // 先在锁内取快照（streams 保活 + connect_cb 拷贝），再在锁外做耗时关闭/回调
    std::vector<std::shared_ptr<librflow_stream_s>> streams_snap;
    librflow_connect_cb_s cb_copy{};
    bool has_cb = false;

    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.lifecycle != rflow::client::LifecycleState::kConnected &&
            s.lifecycle != rflow::client::LifecycleState::kConnecting) {
            return RFLOW_OK;
        }
        streams_snap.reserve(s.streams.size());
        for (auto& kv : s.streams) {
            if (kv.second) streams_snap.push_back(kv.second);
        }
        s.streams.clear();

        has_cb            = s.has_connect_cb;
        cb_copy           = s.connect_cb;
        s.has_connect_cb  = false;
        s.lifecycle       = rflow::client::LifecycleState::kInited;
    }

    // 锁外：Shutdown → 每路 stream->Close() 会经 state_sink 触达 on_state(CLOSED)；
    // 此时 streams_snap 仍持有 stream_s 的强引用，弱引用 lock 成功，用户回调安全收到 handle。
    rflow::client::on_disconnect();

    // 清理本地快照 —— 触发 stream_s 析构（impl shared_ptr 释放；Close 已幂等不会重复跑）
    streams_snap.clear();

    if (has_cb && cb_copy.on_state) {
        cb_copy.on_state(RFLOW_CONN_DISCONNECTED, RFLOW_OK, cb_copy.userdata);
    }
    RFLOW_LOGI("librflow_disconnect OK");
    return RFLOW_OK;
}

/**
 * librflow_send_notice / librflow_service_reply
 *
 * 设计说明：当前 SDK 的信令通道仅承载 RTC offer/answer/ICE，不承载
 * 设备 ↔ 业务后台 的通用 notice/service-reply 控制面消息（这部分链路一般由
 * 业务自有 MQ / HTTP / MQTT 通道承载）。在协议未扩展前，这两个 API 不会真正
 * 发包，对调用方返回 RFLOW_ERR_NOT_SUPPORT 比静默 OK 更诚实，避免上层误以为
 * 已成功送达。
 *
 * 如果未来 P4 协议升级新增 control-plane MessageType（见 docs/RUNTIME_KNOBS.md
 * 与 protocol.h），实现可在 lifecycle.cpp 中改为：
 *   - 复用 librflow_connect 时建立的 device control session
 *   - Send(Message) 对应的新 MessageType
 * 现阶段我们先把语义对外讲清楚，避免长期 TODO 污染 lifecycle 路径。
 */
rflow_err_t librflow_send_notice(int32_t index, const void* payload, uint32_t len) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::client::LifecycleState::kConnected) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "librflow_send_notice: client not connected",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    (void)index;
    (void)payload;
    (void)len;
    rflow::set_last_error(
        RFLOW_ERR_NOT_SUPPORT,
        "librflow_send_notice: control-plane channel not implemented; "
        "use your business MQ/HTTP path instead",
        kErrorOrigin);
    return RFLOW_ERR_NOT_SUPPORT;
}

/// 与 librflow_service_reply 行为完全一致；后者是 ABI 头公开的对称命名。
/// 保留旧符号是为了与已经链接旧二进制的调用方保持兼容。
rflow_err_t librflow_reply_to_service_req(uint64_t req_id, rflow_err_t status,
                                          const void* payload, uint32_t len) {
    auto& s = rflow::client::state();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.lifecycle != rflow::client::LifecycleState::kConnected) {
        rflow::set_last_error(RFLOW_ERR_STATE,
                              "librflow_service_reply: client not connected",
                              kErrorOrigin);
        return RFLOW_ERR_STATE;
    }
    (void)req_id;
    (void)status;
    (void)payload;
    (void)len;
    rflow::set_last_error(
        RFLOW_ERR_NOT_SUPPORT,
        "librflow_service_reply: control-plane channel not implemented; "
        "use your business MQ/HTTP path instead",
        kErrorOrigin);
    return RFLOW_ERR_NOT_SUPPORT;
}

rflow_err_t librflow_service_reply(uint64_t req_id, rflow_err_t status,
                                   const void* payload, uint32_t len) {
    return librflow_reply_to_service_req(req_id, status, payload, len);
}

}  // extern "C"
