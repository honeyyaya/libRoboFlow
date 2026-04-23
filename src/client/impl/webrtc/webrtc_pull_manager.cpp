#include "webrtc_pull_manager.h"

#include "core/rtc/rtc.h"
#include "signaling/signaling_client.h"
#include "webrtc/webrtc_pull_stream.h"

#include "client/internal/handles.h"  // librflow_stream_param_s
#include "common/internal/logger.h"

#include <utility>
#include <vector>

namespace rflow::client::impl {

namespace {

// 当 signal_config 未配置时的兜底地址；保留旧 demo 值便于本地调试。
constexpr const char* kDefaultSignalingUrl = "192.168.3.20:8765";

}  // namespace

WebRtcPullManager& WebRtcPullManager::Instance() {
    static WebRtcPullManager g;
    return g;
}

rflow_err_t WebRtcPullManager::Init(std::string signal_url, std::string device_id) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!rflow::rtc::peer_connection_factory()) {
        RFLOW_LOGE("[pull_mgr] peer_connection_factory null (librflow_init + rtc required)");
        return RFLOW_ERR_STATE;
    }

    signaling_url_ = signal_url.empty() ? std::string(kDefaultSignalingUrl)
                                        : std::move(signal_url);
    device_id_     = std::move(device_id);
    inited_        = true;
    RFLOW_LOGI("[pull_mgr] init ok, signaling=%s device_id=%s",
               signaling_url_.c_str(), device_id_.c_str());
    return RFLOW_OK;
}

void WebRtcPullManager::Shutdown() {
    std::vector<std::shared_ptr<WebRtcPullStream>> to_close;
    {
        std::lock_guard<std::mutex> lk(mu_);
        to_close.reserve(open_indices_.size());
        for (auto& kv : open_indices_) {
            if (auto sp = kv.second.lock()) to_close.push_back(std::move(sp));
        }
        open_indices_.clear();
        inited_ = false;
    }
    for (auto& s : to_close) s->Close();
    RFLOW_LOGI("[pull_mgr] shutdown (closed=%zu)", to_close.size());
}

rflow_err_t WebRtcPullManager::OpenStream(int32_t index,
                                          const ::librflow_stream_param_s* /*param*/,
                                          StateSink state_sink,
                                          FrameSink frame_sink,
                                          std::shared_ptr<WebRtcPullStream>* out) {
    if (!out) return RFLOW_ERR_PARAM;
    *out = nullptr;

    std::shared_ptr<WebRtcPullStream> stream;
    std::string url;
    std::string device_id;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!inited_) return RFLOW_ERR_STATE;

        factory = rflow::rtc::peer_connection_factory();
        if (!factory) return RFLOW_ERR_STATE;

        // index 去重：若历史 weak_ptr 失效则清掉
        auto it = open_indices_.find(index);
        if (it != open_indices_.end()) {
            if (it->second.lock()) return RFLOW_ERR_STREAM_ALREADY_OPEN;
            open_indices_.erase(it);
        }

        url       = signaling_url_;
        device_id = device_id_;

        stream = std::make_shared<WebRtcPullStream>(index, std::move(factory),
                                                    url, device_id);
        open_indices_.emplace(index, stream);
    }

    // sinks 必须在 Start 之前设好：Start 内部 EmitState(OPENING) 会同步触达 state_sink
    stream->SetFrameSink(std::move(frame_sink));
    stream->SetStateSink(std::move(state_sink));

    if (!stream->Start()) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            open_indices_.erase(index);
        }
        return RFLOW_ERR_CONN_NETWORK;
    }

    *out = std::move(stream);
    return RFLOW_OK;
}

}  // namespace rflow::client::impl
