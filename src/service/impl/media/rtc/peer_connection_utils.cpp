#include "media/rtc/peer_connection_utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <future>
#include <thread>

#include "rtc_base/thread.h"

#include "base/logger.h"

namespace rflow::service::impl::media_util {

bool ClosePeerConnectionWithDeadline(
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const char* log_component,
    const char* log_tag,
    int timeout_sec) {
    if (!pc) {
        return true;
    }
    std::packaged_task<void()> task([pc]() { pc->Close(); });
    std::future<void> done = task.get_future();
    std::thread worker(std::move(task));
    if (done.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready) {
        RFLOW_LOGW("[%s] %s PeerConnection::Close exceeded %ds; continuing shutdown",
                   log_component, log_tag, timeout_sec);
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}

void StopWebrtcThreadWithDeadline(webrtc::Thread* thread,
                                  const char* log_component,
                                  int timeout_sec) {
    if (!thread) {
        return;
    }
    std::packaged_task<void()> task([thread]() { thread->Stop(); });
    std::future<void> done = task.get_future();
    std::thread worker(std::move(task));
    if (done.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready) {
        RFLOW_LOGW("[%s] RTC signaling Thread::Stop exceeded %ds; continuing shutdown",
                   log_component, timeout_sec);
        worker.detach();
        return;
    }
    worker.join();
}

webrtc::Priority ParseVideoNetworkPriority(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (unsigned char c : s) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    if (lower == "very_low" || lower == "verylow") {
        return webrtc::Priority::kVeryLow;
    }
    if (lower == "low") {
        return webrtc::Priority::kLow;
    }
    if (lower == "medium") {
        return webrtc::Priority::kMedium;
    }
    if (lower == "high") {
        return webrtc::Priority::kHigh;
    }
    return webrtc::Priority::kHigh;
}

webrtc::PeerConnectionInterface::RTCOfferAnswerOptions MakeVideoOfferOptions() {
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.num_simulcast_layers = 1;
    options.offer_to_receive_audio = 0;
    return options;
}

}  // namespace rflow::service::impl::media_util
