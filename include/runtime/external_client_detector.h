// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#ifndef AURORA_RUNTIME_EXTERNAL_CLIENT_DETECTOR_H_
#define AURORA_RUNTIME_EXTERNAL_CLIENT_DETECTOR_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace aurora {
namespace runtime {

// These are the local clients that Roblox Studio exposes through Quick
// Connect, plus the Codex desktop app and the local Studio bridge.  The
// detector deliberately reports applications and CLIs separately so a
// running CLI session is not hidden by a desktop process.
enum class DetectedClient {
  kAntigravity,
  kCodexCli,
  kClaudeCode,
  kClaudeDesktop,
  kCursor,
  kGeminiCli,
  kVisualStudioCode,
  kCodexDesktop,
  kRobloxStudioMcp,
};

struct ExternalClientStatus {
  DetectedClient client;
  std::string id;
  std::string name;
  std::string kind;
  bool installed = false;
  bool running = false;
  bool connected = false;
  pid_t pid = -1;
  std::uint16_t port = 0;
  std::string executable;
  std::string detail;
};

class ExternalClientDetector final {
 public:
  // proc_root is injectable so classification and process enumeration can be
  // tested without depending on the host's live process table.
  explicit ExternalClientDetector(
      std::filesystem::path proc_root = std::filesystem::path("/proc"));

  std::vector<ExternalClientStatus> Detect() const;

  // ClassifyProcess is intentionally pure.  It accepts /proc-style values:
  // comm, NUL-separated cmdline, and the resolved executable path.
  static std::optional<DetectedClient> ClassifyProcess(
      std::string_view comm, std::string_view cmdline,
      std::string_view executable);

  static const char* Id(DetectedClient client);
  static const char* Name(DetectedClient client);
  static const char* Kind(DetectedClient client);

 private:
  std::filesystem::path proc_root_;
};

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_EXTERNAL_CLIENT_DETECTOR_H_
