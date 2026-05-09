#include "core/rtc/congestion/congestion_policy.h"

#include <algorithm>

namespace rflow::rtc::congestion {

class BalancedCongestionPolicy final : public CongestionPolicy {
 public:
  explicit BalancedCongestionPolicy(int64_t start_bps) : target_bps_(std::max<int64_t>(100000, start_bps)) {}

  const char* Name() const override { return "balanced"; }

  void OnNetworkFeedback(const NetworkFeedback& feedback) override {
    if (feedback.estimated_bandwidth_bps > 0) {
      target_bps_ = std::max<int64_t>(100000, feedback.estimated_bandwidth_bps * 9 / 10);
    }
    if (feedback.packet_loss_ppm > 120000) {  // >12%
      target_bps_ = target_bps_ * 8 / 10;
    }
    if (feedback.rtt_ms > 250) {
      target_bps_ = target_bps_ * 9 / 10;
    }
    target_bps_ = std::clamp<int64_t>(target_bps_, 100000, 50000000);
  }

  void OnEncodeFeedback(const EncodeFeedback& feedback) override {
    if (feedback.encode_time_ms > 35) {
      target_bps_ = target_bps_ * 95 / 100;
    }
    last_frame_size_bytes_ = feedback.frame_size_bytes;
  }

  CongestionDecision CurrentDecision() const override {
    CongestionDecision d;
    d.target_bitrate_bps = target_bps_;
    d.pacing_bitrate_bps = target_bps_ * 11 / 10;
    d.request_keyframe = false;
    return d;
  }

 private:
  int64_t target_bps_{500000};
  int64_t last_frame_size_bytes_{0};
};

class UltraLowLatencyPolicy final : public CongestionPolicy {
 public:
  explicit UltraLowLatencyPolicy(int64_t start_bps) : target_bps_(std::max<int64_t>(120000, start_bps)) {}

  const char* Name() const override { return "ultra_low_latency"; }

  void OnNetworkFeedback(const NetworkFeedback& feedback) override {
    if (feedback.estimated_bandwidth_bps > 0) {
      target_bps_ = std::max<int64_t>(120000, feedback.estimated_bandwidth_bps * 8 / 10);
    }
    if (feedback.packet_loss_ppm > 80000 || feedback.rtt_ms > 180) {
      target_bps_ = target_bps_ * 85 / 100;
    }
    target_bps_ = std::clamp<int64_t>(target_bps_, 120000, 50000000);
  }

  void OnEncodeFeedback(const EncodeFeedback& feedback) override {
    if (feedback.encode_time_ms > 20) {
      target_bps_ = target_bps_ * 92 / 100;
    }
  }

  CongestionDecision CurrentDecision() const override {
    CongestionDecision d;
    d.target_bitrate_bps = target_bps_;
    d.pacing_bitrate_bps = target_bps_;
    d.request_keyframe = false;
    return d;
  }

 private:
  int64_t target_bps_{500000};
};

std::unique_ptr<CongestionPolicy> CreateCongestionPolicy(const std::string& policy_name,
                                                         int64_t start_bitrate_bps) {
  if (policy_name == "ultra_low_latency") {
    return std::make_unique<UltraLowLatencyPolicy>(start_bitrate_bps);
  }
  return std::make_unique<BalancedCongestionPolicy>(start_bitrate_bps);
}

}  // namespace rflow::rtc::congestion
