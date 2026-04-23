#ifndef WEBRTC_DEMO_SIGNALING_IO_MANAGER_H_
#define WEBRTC_DEMO_SIGNALING_IO_MANAGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace webrtc_demo {

class SignalingClient;

struct SignalingClientSessionSlot {
    std::atomic<SignalingClient*> owner{nullptr};
    std::atomic<int>              fd{-1};
    std::atomic<bool>             active{true};
    std::string                   read_buffer;
};

class SignalingIoManager {
 public:
    static SignalingIoManager& Instance();

    bool RegisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot);
    void UnregisterSession(const std::shared_ptr<SignalingClientSessionSlot>& slot);

 private:
    SignalingIoManager() = default;
    ~SignalingIoManager();

    bool EnsureStarted();
    void Wake();
    void DrainWakePipe();
    std::vector<std::shared_ptr<SignalingClientSessionSlot>> SnapshotSessions();
    void DispatchLine(const std::shared_ptr<SignalingClientSessionSlot>& slot,
                      std::string_view                                   line);
    void CloseSession(const std::shared_ptr<SignalingClientSessionSlot>& slot,
                      std::string_view                                   error);
    void HandleReadable(const std::shared_ptr<SignalingClientSessionSlot>& slot);
    void Run();

    std::atomic<bool> running_{false};
    std::atomic<int>  wake_read_fd_{-1};
    std::atomic<int>  wake_write_fd_{-1};
    std::mutex        start_mu_;
    bool              started_{false};
    std::thread       io_thread_;

    std::mutex mu_;
    std::unordered_map<int, std::shared_ptr<SignalingClientSessionSlot>> sessions_;
};

}  // namespace webrtc_demo

#endif  // WEBRTC_DEMO_SIGNALING_IO_MANAGER_H_
