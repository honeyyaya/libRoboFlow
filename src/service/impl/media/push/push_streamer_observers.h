/**
 * @file   push_streamer_observers.h
 * @brief  PushStreamer 内部使用的纯辅助 sink/observer。
 *
 * 这些类与 PushStreamer::Impl 没有耦合（没有访问其 private 状态），
 * 抽出来主要为了：
 *   - 把 push_streamer.cpp 主翻译单元从 ~1500 行收敛到只关注 RTC 编排；
 *   - 让单元测试可以独立 mock VideoSinkInterface 行为；
 *   - 给后续可能的 "loopback test mode" 一个独立替换点。
 *
 * 单纯 RTP/帧统计 sink 不放在 detail::push 命名空间下（因为它们包含状态），
 * 而是放在 rflow::service::impl::observers 子命名空间。
 */
#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_OBSERVERS_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_OBSERVERS_H__

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

#include "media/push/push_streamer.h"

namespace rflow::service::impl::observers {

/// 帧计数 sink，PushStreamer 装在主路径上。线程安全（atomic count）。
class FrameCountingSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    explicit FrameCountingSink(OnFrameCallback cb) : on_frame_(std::move(cb)) {}

    void OnFrame(const webrtc::VideoFrame& frame) override {
        unsigned int n = ++frame_count_;
        if (on_frame_) {
            on_frame_(n, frame.width(), frame.height());
        }
    }

    unsigned int GetFrameCount() const { return frame_count_.load(); }

private:
    std::atomic<unsigned int> frame_count_{0};
    OnFrameCallback on_frame_;
};

/// 解码后视频帧计数 sink（仅在 loopback 测试场景下挂到 receiver track）。
class DecodedFrameSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    void OnFrame(const webrtc::VideoFrame&) override { ++count_; }
    unsigned int GetCount() const { return count_.load(); }

private:
    std::atomic<unsigned int> count_{0};
};

/// 用于 push 端"自回环"测试 PeerConnection：把本地候选回灌给 sender，并统计接收端解码帧数。
class LoopbackPcObserver : public webrtc::PeerConnectionObserver {
public:
    using AddCandidateFn = std::function<void(const std::string&, int, const std::string&)>;

    explicit LoopbackPcObserver(AddCandidateFn add_to_sender)
        : add_to_sender_(std::move(add_to_sender)) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
    void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&) override;

    unsigned int GetDecodedCount() const;
    void Teardown();

private:
    void AttachSink(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> r);

    AddCandidateFn add_to_sender_;
    std::unique_ptr<DecodedFrameSink> decoded_sink_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> decoded_track_;
};

}  // namespace rflow::service::impl::observers

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_STREAMER_OBSERVERS_H__
