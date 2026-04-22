#include "webrtc_pull_manager.h"

#include "signaling/signaling_client.h"
#include "webrtc/webrtc_factory.h"
#include "webrtc/webrtc_pull_stream.h"

#include "common/internal/logger.h"

#include <utility>

namespace robrt::client::impl {

namespace {

// TODO: 真正从 librobrt_set_global_config(signal_config) 取；先保留旧 demo 默认值。
constexpr const char* kDefaultSignalingUrl = "192.168.3.20:8765";

std::string MakeRole(int32_t index) {
    // 旧协议仅 "subscriber"；这里把 index 编进 role，依赖信令服务器兼容/忽略多余字段。
    // TODO: 协议扩展字段后迁到 proper stream_id。
    return "subscriber:" + std::to_string(index);
}

}  // namespace

WebRtcPullManager& WebRtcPullManager::Instance() {
    static WebRtcPullManager g;
    return g;
}

robrt_err_t WebRtcPullManager::Init() {
    std::lock_guard<std::mutex> lk(mu_);
    if (inited_) return ROBRT_OK;

    factory_ = CreatePeerConnectionFactory();
    if (!factory_) {
        ROBRT_LOGE("[pull_mgr] create pc factory failed");
        return ROBRT_ERR_FAIL;
    }
    if (signaling_url_.empty()) signaling_url_ = kDefaultSignalingUrl;
    inited_ = true;
    ROBRT_LOGI("[pull_mgr] init ok, signaling=%s", signaling_url_.c_str());
    return ROBRT_OK;
}

void WebRtcPullManager::Shutdown() {
    std::unordered_map<int32_t, std::shared_ptr<WebRtcPullStream>> to_close;
    {
        std::lock_guard<std::mutex> lk(mu_);
        to_close.swap(streams_);
        factory_ = nullptr;
        inited_  = false;
    }
    for (auto& kv : to_close) {
        if (kv.second) kv.second->Close();
    }
    ROBRT_LOGI("[pull_mgr] shutdown");
}

void WebRtcPullManager::SetSignalingUrl(std::string url) {
    std::lock_guard<std::mutex> lk(mu_);
    signaling_url_ = std::move(url);
}

robrt_err_t WebRtcPullManager::OpenStream(int32_t index,
                                           std::shared_ptr<WebRtcPullStream>* out) {
    if (!out) return ROBRT_ERR_PARAM;
    std::shared_ptr<WebRtcPullStream> stream;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!inited_ || !factory_) return ROBRT_ERR_STATE;
        if (streams_.count(index)) return ROBRT_ERR_STREAM_ALREADY_OPEN;

        stream = std::make_shared<WebRtcPullStream>(index, factory_,
                                                    signaling_url_, MakeRole(index));
        streams_.emplace(index, stream);
    }
    if (!stream->Start()) {
        std::lock_guard<std::mutex> lk(mu_);
        streams_.erase(index);
        return ROBRT_ERR_CONN_NETWORK;
    }
    *out = stream;
    return ROBRT_OK;
}

robrt_err_t WebRtcPullManager::CloseStream(int32_t index) {
    std::shared_ptr<WebRtcPullStream> s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = streams_.find(index);
        if (it == streams_.end()) return ROBRT_OK;
        s = std::move(it->second);
        streams_.erase(it);
    }
    if (s) s->Close();
    return ROBRT_OK;
}

std::shared_ptr<WebRtcPullStream> WebRtcPullManager::FindStream(int32_t index) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = streams_.find(index);
    return it == streams_.end() ? nullptr : it->second;
}

}  // namespace robrt::client::impl
