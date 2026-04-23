/**
 * @file   lifecycle.cpp
 * @brief  set_global_config / init / uninit / connect / disconnect
**/

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/global_config_impl.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"

#include "internal/infrastructure.h"

#include <string>
#include <utility>
#include <vector>

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
        rflow::set_last_error("set_global_config must be called before init");
        return RFLOW_ERR_STATE;
    }
    if (!cfg || cfg->magic != rflow::kMagicGlobalConfig) {
        rflow::set_last_error("invalid global_config handle");
        return RFLOW_ERR_PARAM;
    }

    s.global_config = *cfg;
    s.global_config.magic = rflow::kMagicGlobalConfig;

    // 立即应用 log 配置（init 之前日志也可能产生）
    if (s.global_config.has_log) {
        rflow::logger_apply(s.global_config.log.level,
                            s.global_config.log.cb,
                            s.global_config.log.userdata);
    }
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
    std::string device_id = info->device_id;
    librflow_connect_cb_s cb_copy{};

    {
        std::lock_guard<std::mutex> lk(s.mu);

        if (s.lifecycle == rflow::client::LifecycleState::kUninit) {
            rflow::set_last_error("must call librflow_init before connect");
            return RFLOW_ERR_STATE;
        }
        if (s.lifecycle == rflow::client::LifecycleState::kConnected ||
            s.lifecycle == rflow::client::LifecycleState::kConnecting) {
            return RFLOW_ERR_STATE;
        }

        s.connect_info   = *info;
        s.connect_cb     = *cb;
        s.has_connect_cb = true;
        s.lifecycle      = rflow::client::LifecycleState::kConnecting;

        if (s.global_config.magic == rflow::kMagicGlobalConfig && s.global_config.has_signal) {
            signal_url = s.global_config.signal.url;
        }
        cb_copy = s.connect_cb;
    }

    // TODO: 当前无独立的 device 级鉴权通道，连接=拉起拉流子系统；
    //       后续若接入 core/signal 长连 + 设备鉴权，应在此先做握手再切 Connected。
    rflow_err_t err = rflow::client::on_connect_succeeded(signal_url, device_id);

    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (err != RFLOW_OK) {
            s.lifecycle      = rflow::client::LifecycleState::kInited;
            s.has_connect_cb = false;
            return err;
        }
        s.lifecycle = rflow::client::LifecycleState::kConnected;
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

}  // extern "C"
