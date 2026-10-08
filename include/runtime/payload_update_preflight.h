#ifndef AURORA_RUNTIME_PAYLOAD_UPDATE_PREFLIGHT_H_
#define AURORA_RUNTIME_PAYLOAD_UPDATE_PREFLIGHT_H_

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "runtime/environment.h"
#include "runtime/runtime_paths.h"

namespace aurora {
namespace runtime {

struct PayloadUpdatePreflightResult {
  bool attempted = false;
  std::string error;
  // Last failure the updater reported on its own stderr. Empty when the
  // updater never ran or never explained itself.
  std::string details;

  explicit operator bool() const { return error.empty(); }
};

enum class LivePayloadUpdateCheckResult {
  kNone,
  kAvailable,
  kNewerReleaseUnavailableForGuestAbi,
};

// Main-thread polled monitor; network checks run in a separate updater process.
class LivePayloadUpdateMonitor final {
 public:
  LivePayloadUpdateMonitor(const Environment& environment,
                           const RuntimePaths& paths, int instance_descriptor);
  ~LivePayloadUpdateMonitor();
  LivePayloadUpdateMonitor(const LivePayloadUpdateMonitor&) = delete;
  LivePayloadUpdateMonitor& operator=(const LivePayloadUpdateMonitor&) = delete;

  LivePayloadUpdateCheckResult Poll();
  bool BeginScheduleAfterExit(std::string* error);
  // Polls the local readiness channel without waiting. nullopt means pending.
  std::optional<bool> PollScheduleAfterExit(std::string* error);
  bool handoff_in_progress() const { return readiness_descriptor_ >= 0; }
  bool has_ready_updater() const {
    return updater_ > 0 && updater_acknowledged_ &&
           !updater_termination_sent_;
  }
  bool deferred_update_authorized() const {
    return deferred_update_authorized_;
  }

 private:
  std::filesystem::path helper_;
  std::filesystem::path log_path_;
  std::vector<std::string> environment_;
  int instance_descriptor_ = -1;
  int checker_ = -1;
  int updater_ = -1;
  int readiness_descriptor_ = -1;
  std::chrono::steady_clock::time_point readiness_deadline_{};
  std::chrono::steady_clock::time_point terminate_deadline_{};
  bool updater_termination_sent_ = false;
  bool updater_kill_sent_ = false;
  bool updater_acknowledged_ = false;
  bool offered_ = false;
  bool unavailable_abi_notice_shown_ = false;
  // A prior offer or silent discovery authorized this exact update. Keep it
  // across helper exits so Aurora can requeue it without another prompt.
  bool deferred_update_authorized_ = false;
  std::optional<std::string> updater_exit_error_;
  std::chrono::steady_clock::time_point next_check_;

  void ReapUpdater();
  void FailHandoff();
};

PayloadUpdatePreflightResult RunPayloadUpdatePreflight(
    const Environment& environment, const RuntimePaths& paths,
    bool force_run_latest = false);

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_PAYLOAD_UPDATE_PREFLIGHT_H_
