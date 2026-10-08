// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#ifndef AURORA_RUNTIME_MANAGED_PROCESS_H_
#define AURORA_RUNTIME_MANAGED_PROCESS_H_

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace aurora {
namespace runtime {

// A shell-free child-process description. The process owns its process group,
// so callers can stop a complete workflow without leaking Wine/helper child
// processes behind the root process.
struct ManagedProcessSpec {
  std::string executable;
  std::vector<std::string> arguments;
  std::filesystem::path working_directory;
  // Entries are NAME=VALUE overrides. The inherited environment is preserved
  // and these entries replace matching names.
  std::vector<std::string> environment;
  bool inherit_stdio = true;
};

class ManagedProcess final {
 public:
  ManagedProcess() = default;
  ~ManagedProcess();

  ManagedProcess(const ManagedProcess&) = delete;
  ManagedProcess& operator=(const ManagedProcess&) = delete;
  ManagedProcess(ManagedProcess&& other) noexcept;
  ManagedProcess& operator=(ManagedProcess&& other) noexcept;

  static ManagedProcess Start(const ManagedProcessSpec& spec,
                              std::string* error = nullptr);

  bool valid() const { return pid_ > 0 || exit_status_.has_value(); }
  bool running() const { return pid_ > 0; }
  pid_t pid() const { return pid_; }

  // Returns the wait status when the process has exited. A null result means
  // that it is still running; errors are reported through error.
  std::optional<int> Poll(std::string* error = nullptr);
  bool Wait(int* wait_status, std::string* error = nullptr);

  // Sends SIGTERM to the process group, escalates to SIGKILL after timeout,
  // and always reaps the root child before returning.
  bool Terminate(std::chrono::milliseconds timeout,
                 std::string* error = nullptr);

 private:
  explicit ManagedProcess(pid_t pid) : pid_(pid) {}

  pid_t pid_ = -1;
  std::optional<int> exit_status_;
};

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_MANAGED_PROCESS_H_
