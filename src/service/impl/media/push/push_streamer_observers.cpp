#include "media/push/push_streamer_observers.h"

#include "api/media_stream_interface.h"
#include "api/rtp_receiver_interface.h"

namespace rflow::service::impl::observers {

void LoopbackPcObserver::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    if (!add_to_sender_ || !candidate) {
        return;
    }
    std::string sdp;
    if (!candidate->ToString(&sdp)) {
        return;
    }
    add_to_sender_(candidate->sdp_mid(), candidate->sdp_mline_index(), sdp);
}

void LoopbackPcObserver::OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    AttachSink(transceiver ? transceiver->receiver() : nullptr);
}

void LoopbackPcObserver::OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                                    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&) {
    AttachSink(receiver);
}

unsigned int LoopbackPcObserver::GetDecodedCount() const {
    return decoded_sink_ ? decoded_sink_->GetCount() : 0;
}

void LoopbackPcObserver::Teardown() {
    if (decoded_track_ && decoded_sink_) {
        decoded_track_->RemoveSink(decoded_sink_.get());
    }
    decoded_sink_.reset();
    decoded_track_ = nullptr;
}

void LoopbackPcObserver::AttachSink(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> r) {
    if (!r) {
        return;
    }
    auto track = r->track();
    if (!track || track->kind() != webrtc::MediaStreamTrackInterface::kVideoKind) {
        return;
    }
    auto* vt = static_cast<webrtc::VideoTrackInterface*>(track.get());
    if (!vt) {
        return;
    }
    if (!decoded_sink_) {
        decoded_sink_ = std::make_unique<DecodedFrameSink>();
    }
    // OnTrack + OnAddTrack 可能各回调一次；换 track 时必须先从旧 track 摘掉 sink，
    // 否则 Teardown 后旧 track 仍向已销毁的 DecodedFrameSink 投递帧 → 段错误。
    if (decoded_track_.get() == vt) {
        return;
    }
    if (decoded_track_) {
        decoded_track_->RemoveSink(decoded_sink_.get());
    }
    decoded_track_ = webrtc::scoped_refptr<webrtc::VideoTrackInterface>(vt);
    decoded_track_->AddOrUpdateSink(decoded_sink_.get(), webrtc::VideoSinkWants());
}

}  // namespace rflow::service::impl::observers
