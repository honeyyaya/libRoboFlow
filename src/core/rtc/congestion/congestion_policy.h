#ifndef __RFLOW_CORE_RTC_CONGESTION_POLICY_H__
#define __RFLOW_CORE_RTC_CONGESTION_POLICY_H__

#include <cstdint>
#include <memory>
#include <string>

namespace rflow::rtc::congestion {

struct NetworkFeedback {
  int64_t rtt_ms{0};
  int64_t packet_loss_ppm{0};
  int64_t estimated_bandwidth_bps{0};
};

struct EncodeFeedback {
  int64_t frame_size_bytes{0};
  int64_t encode_time_ms{0};
};

struct CongestionDecision {
  int64_t target_bitrate_bps{0};
  int64_t pacing_bitrate_bps{0};
  bool request_keyframe{false};
};

class CongestionPolicy {
 public:
  virtual ~CongestionPolicy() = default;
  virtual const char* Name() const = 0;
  virtual void OnNetworkFeedback(const NetworkFeedback& feedback) = 0;
  virtual void OnEncodeFeedback(const EncodeFeedback& feedback) = 0;
  virtual CongestionDecision CurrentDecision() const = 0;
};

std::unique_ptr<CongestionPolicy> CreateCongestionPolicy(const std::string& policy_name,
                                                         int64_t start_bitrate_bps);

}  // namespace rflow::rtc::congestion

#endif  // __RFLOW_CORE_RTC_CONGESTION_POLICY_H__
