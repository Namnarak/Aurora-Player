#include "runtime/payload_update_preflight.h"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern char** environ;

namespace aurora {
namespace runtime {
namespace {

constexpr int kUpdaterProgressDescriptor = 10;
constexpr char kProgressFinishedPacket = 'S';

bool Enabled(const Environment& environment, std::string_view name) {
  const auto value = environment.Get(name);
  return value.has_value() && !value->empty() && *value != "0";
}

bool HasDisplay(const Environment& environment) {
  return environment.HasNonEmpty("DISPLAY") ||
         environment.HasNonEmpty("WAYLAND_DISPLAY");
}

bool IsExecutableRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) &&
         !std::filesystem::is_symlink(path, error) &&
         access(path.c_str(), X_OK) == 0;
}

std::filesystem::path UpdateHelper(const Environment& environment) {
  const auto helper_override = environment.Get("AURORA_UPDATE_HELPER");
  if (helper_override.has_value()) {
    const std::filesystem::path helper(*helper_override);
    if (helper.is_absolute() && IsExecutableRegularFile(helper)) {
      return helper;
    }
    return {};
  }

  const auto project_override = environment.Get("AURORA_PROJECT_ROOT");
  if (project_override.has_value()) {
    const std::filesystem::path root(*project_override);
    if (!root.is_absolute()) {
      return {};
    }
    for (const std::filesystem::path& helper :
         {root / "build" / "aurora_updater",
          root / "bin" / "aurora_updater"}) {
      if (IsExecutableRegularFile(helper)) {
        return helper;
      }
    }
    return {};
  }

  std::error_code error;
  const std::filesystem::path executable =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (error || executable.empty()) {
    return {};
  }
  const std::filesystem::path executable_dir = executable.parent_path();
  const std::vector<std::filesystem::path> candidates = {
      executable_dir / "aurora_updater",
      executable_dir.parent_path() / "libexec" / "aurora" /
          "aurora_updater",
      executable_dir.parent_path() / "lib" / "aurora" / "aurora_updater",
  };
  for (const std::filesystem::path& candidate : candidates) {
    if (IsExecutableRegularFile(candidate)) {
      return candidate;
    }
  }
  return {};
}

std::filesystem::path ProgressHelper(const Environment& environment) {
  const auto helper_override =
      environment.Get("AURORA_UPDATE_PROGRESS_HELPER");
  if (helper_override.has_value()) {
    const std::filesystem::path helper(*helper_override);
    return helper.is_absolute() && IsExecutableRegularFile(helper)
               ? helper
               : std::filesystem::path{};
  }

  std::error_code error;
  const std::filesystem::path executable =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (error || executable.empty()) {
    return {};
  }
  const std::filesystem::path helper =
      executable.parent_path() / "aurora_failure_dialog";
  return IsExecutableRegularFile(helper) ? helper : std::filesystem::path{};
}

void WaitForProcess(pid_t process) {
  if (process <= 0) {
    return;
  }
  while (waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
  }
}

class UpdateProgressMonitor final {
 public:
  UpdateProgressMonitor() = default;
  ~UpdateProgressMonitor() { Finish(); }

  UpdateProgressMonitor(const UpdateProgressMonitor&) = delete;
  UpdateProgressMonitor& operator=(const UpdateProgressMonitor&) = delete;

  static UpdateProgressMonitor Start(const Environment& environment) {
    if (Enabled(environment, "AURORA_DISABLE_UPDATE_PROGRESS") ||
        Enabled(environment, "AURORA_DISABLE_FAILURE_DIALOG") ||
        Enabled(environment, "AURORA_ISOLATED_CANARY") ||
        Enabled(environment, "AURORA_HEADLESS") || !HasDisplay(environment)) {
      return {};
    }
    const std::filesystem::path helper = ProgressHelper(environment);
    if (helper.empty()) {
      return {};
    }

    int sockets[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
      return {};
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
      close(sockets[0]);
      close(sockets[1]);
      return {};
    }
    int action_status = 0;
    if (sockets[0] == STDIN_FILENO) {
      action_status = posix_spawn_file_actions_addclose(&actions, sockets[0]);
    }
    if (action_status == 0 && sockets[1] != STDIN_FILENO) {
      action_status =
          posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
    }
    if (action_status == 0 && sockets[0] != STDIN_FILENO) {
      action_status = posix_spawn_file_actions_addclose(&actions, sockets[0]);
    }
    if (action_status == 0 && sockets[1] != STDIN_FILENO) {
      action_status = posix_spawn_file_actions_addclose(&actions, sockets[1]);
    }

    std::string helper_string = helper.string();
    char progress_option[] = "--progress-monitor";
    char* arguments[] = {helper_string.data(), progress_option, nullptr};
    pid_t child = -1;
    int spawn_status = action_status;
    if (spawn_status == 0) {
      spawn_status = posix_spawn(&child, helper.c_str(), &actions, nullptr,
                                 arguments, environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    close(sockets[1]);
    if (spawn_status != 0) {
      close(sockets[0]);
      return {};
    }
    return UpdateProgressMonitor(sockets[0], child);
  }

  bool active() const { return socket_ >= 0; }
  int socket() const { return socket_; }

 private:
  UpdateProgressMonitor(int socket, pid_t process)
      : socket_(socket), process_(process) {}

  void Finish() {
    if (!active()) {
      return;
    }
    (void)send(socket_, &kProgressFinishedPacket,
               sizeof(kProgressFinishedPacket), MSG_NOSIGNAL);
    (void)shutdown(socket_, SHUT_RDWR);
    close(socket_);
    socket_ = -1;
    WaitForProcess(process_);
    process_ = -1;
  }

  int socket_ = -1;
  pid_t process_ = -1;
};

bool IsEnvironmentEntry(std::string_view entry, std::string_view name) {
  return entry.size() > name.size() && entry[name.size()] == '=' &&
         entry.substr(0, name.size()) == name;
}

bool IsOverriddenEnvironmentEntry(
    std::string_view entry,
    const std::vector<std::pair<std::string_view, std::filesystem::path>>&
        overrides) {
  for (const auto& [name, ignored] : overrides) {
    if (IsEnvironmentEntry(entry, name)) {
      return true;
    }
  }
  return false;
}

// The updater runs as a separate process, so the reason it failed ("api.
// pureapk.com request returned status 503") only ever reached the session log.
// Relay its stderr verbatim and keep the last failure it reported, so the
// launcher can tell the user what actually went wrong.
class UpdaterStderrRelay final {
 public:
  UpdaterStderrRelay() {
    if (pipe2(descriptors_, O_CLOEXEC) != 0) {
      descriptors_[0] = -1;
      descriptors_[1] = -1;
    }
  }

  ~UpdaterStderrRelay() {
    CloseDescriptor(&descriptors_[0]);
    CloseWrite();
  }

  UpdaterStderrRelay(const UpdaterStderrRelay&) = delete;
  UpdaterStderrRelay& operator=(const UpdaterStderrRelay&) = delete;

  bool active() const { return descriptors_[0] >= 0; }
  int write_descriptor() const { return descriptors_[1]; }
  void CloseWrite() { CloseDescriptor(&descriptors_[1]); }
  const std::string& failure() const { return failure_; }

  // Must run before waitpid: a child that fills the pipe blocks until it is
  // drained, and waiting first would deadlock the launcher.
  void Drain() {
    if (!active()) {
      return;
    }
    std::string pending;
    std::array<char, 4096> buffer{};
    while (true) {
      const ssize_t received =
          read(descriptors_[0], buffer.data(), buffer.size());
      if (received < 0 && errno == EINTR) {
        continue;
      }
      if (received <= 0) {
        break;
      }
      const auto bytes = static_cast<std::size_t>(received);
      WriteAll(buffer.data(), bytes);
      pending.append(buffer.data(), bytes);
      for (std::size_t end = pending.find('\n'); end != std::string::npos;
           end = pending.find('\n')) {
        RecordLine(std::string_view(pending).substr(0, end));
        pending.erase(0, end + 1);
      }
    }
    RecordLine(pending);
    CloseDescriptor(&descriptors_[0]);
  }

 private:
  // An Adwaita alert dialog stays readable well below the packet limit.
  static constexpr std::size_t kMaximumFailureBytes = 400;

  static void CloseDescriptor(int* descriptor) {
    if (*descriptor >= 0) {
      close(*descriptor);
      *descriptor = -1;
    }
  }

  static void WriteAll(const char* data, std::size_t bytes) {
    std::size_t offset = 0;
    while (offset < bytes) {
      const ssize_t written =
          write(STDERR_FILENO, data + offset, bytes - offset);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        return;
      }
      offset += static_cast<std::size_t>(written);
    }
  }

  // A message cut mid-codepoint fails UTF-8 validation in the dialog helper
  // and would be replaced by the generic text.
  static std::string_view TrimToCharacterBoundary(std::string_view text) {
    if (text.size() <= kMaximumFailureBytes) {
      return text;
    }
    std::size_t size = kMaximumFailureBytes;
    while (size > 0 &&
           (static_cast<unsigned char>(text[size]) & 0xC0U) == 0x80U) {
      --size;
    }
    return text.substr(0, size);
  }

  void RecordLine(std::string_view line) {
    constexpr std::string_view kPrefix = "[native-updater] ";
    constexpr std::string_view kWarningPrefix = "warning: ";
    if (line.size() <= kPrefix.size() ||
        line.substr(0, kPrefix.size()) != kPrefix) {
      return;
    }
    const std::string_view detail = line.substr(kPrefix.size());
    if (detail.substr(0, kWarningPrefix.size()) == kWarningPrefix) {
      return;
    }
    failure_.assign(TrimToCharacterBoundary(detail));
  }

  int descriptors_[2] = {-1, -1};
  std::string failure_;
};

std::vector<std::string> ChildEnvironment(const RuntimePaths& paths,
                                          bool progress_enabled) {
  const std::vector<std::pair<std::string_view, std::filesystem::path>>
      overrides = {
          {"AURORA_CONFIG_FILE", paths.config_file()},
          {"AURORA_CONFIG_ROOT", paths.config_root()},
          {"AURORA_DATA_ROOT", paths.data_root()},
          {"AURORA_CACHE_ROOT", paths.cache_root()},
          {"AURORA_STATE_ROOT", paths.state_root()},
      };
  std::vector<std::string> child_environment;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    if (!IsOverriddenEnvironmentEntry(*entry, overrides) &&
        !IsEnvironmentEntry(*entry, "AURORA_UPDATE_PROGRESS_FD")) {
      child_environment.emplace_back(*entry);
    }
  }
  for (const auto& [name, value] : overrides) {
    child_environment.emplace_back(std::string(name) + "=" + value.string());
  }
  if (progress_enabled) {
    child_environment.emplace_back("AURORA_UPDATE_PROGRESS_FD=" +
                                   std::to_string(kUpdaterProgressDescriptor));
  }
  return child_environment;
}

}  // namespace

LivePayloadUpdateMonitor::LivePayloadUpdateMonitor(
    const Environment& environment, const RuntimePaths& paths,
    int instance_descriptor)
    : instance_descriptor_(instance_descriptor),
      next_check_(std::chrono::steady_clock::now()) {
  if (instance_descriptor < 0 || Enabled(environment, "AURORA_SKIP_UPDATE_CHECK") ||
      Enabled(environment, "AURORA_ISOLATED_CANARY") ||
      Enabled(environment, "AURORA_HEADLESS")) {
    return;
  }
  helper_ = UpdateHelper(environment);
  environment_ = ChildEnvironment(paths, false);
  log_path_ = paths.state_root() / "roblox-deferred-update.log";
}

LivePayloadUpdateMonitor::~LivePayloadUpdateMonitor() {
  // Never delay app shutdown for a metadata request. An unfinished checker
  // is read-only and will be reaped by the system after this process exits.
  if (checker_ > 0) (void)waitpid(checker_, nullptr, WNOHANG);
  if (readiness_descriptor_ >= 0) close(readiness_descriptor_);
  if (updater_ > 0) (void)waitpid(updater_, nullptr, WNOHANG);
}

void LivePayloadUpdateMonitor::ReapUpdater() {
  if (updater_ <= 0) return;
  int status = 0;
  const pid_t completed = waitpid(updater_, &status, WNOHANG);
  if (completed == updater_ || (completed < 0 && errno != EINTR)) {
    updater_ = -1;
    if (updater_acknowledged_ && !updater_termination_sent_) {
      updater_exit_error_ =
          "The deferred Roblox updater exited before Aurora closed; see " +
          log_path_.string() + " for details";
      offered_ = false;
      next_check_ = std::chrono::steady_clock::now() +
                    std::chrono::minutes(15);
    }
    updater_acknowledged_ = false;
    updater_termination_sent_ = false;
    updater_kill_sent_ = false;
    return;
  }
  if (updater_termination_sent_ && !updater_kill_sent_ &&
      std::chrono::steady_clock::now() >= terminate_deadline_) {
    (void)kill(updater_, SIGKILL);
    updater_kill_sent_ = true;
  }
}

void LivePayloadUpdateMonitor::FailHandoff() {
  if (readiness_descriptor_ >= 0) {
    close(readiness_descriptor_);
    readiness_descriptor_ = -1;
  }
  if (updater_ > 0 && !updater_termination_sent_) {
    (void)kill(updater_, SIGTERM);
    updater_termination_sent_ = true;
    terminate_deadline_ = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(500);
  }
  updater_acknowledged_ = false;
  offered_ = false;
}

LivePayloadUpdateCheckResult LivePayloadUpdateMonitor::Poll() {
  ReapUpdater();
  if (helper_.empty() || offered_ || readiness_descriptor_ >= 0 ||
      updater_ > 0) return LivePayloadUpdateCheckResult::kNone;
  if (checker_ > 0) {
    int status = 0;
    const pid_t completed = waitpid(checker_, &status, WNOHANG);
    if (completed == 0 || (completed < 0 && errno == EINTR)) {
      return LivePayloadUpdateCheckResult::kNone;
    }
    checker_ = -1;
    next_check_ = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    if (completed <= 0 || !WIFEXITED(status)) {
      return LivePayloadUpdateCheckResult::kNone;
    }
    const int result = WEXITSTATUS(status);
    // Exit 10 is the updater-owned compatible offer protocol. Exit 11 means
    // Roblox is newer overall, but has no package for this guest ABI.
    if (result == 10) {
      offered_ = true;
      return LivePayloadUpdateCheckResult::kAvailable;
    }
    if (result == 11 && !unavailable_abi_notice_shown_) {
      unavailable_abi_notice_shown_ = true;
      return LivePayloadUpdateCheckResult::kNewerReleaseUnavailableForGuestAbi;
    }
    return LivePayloadUpdateCheckResult::kNone;
  }
  if (std::chrono::steady_clock::now() < next_check_) {
    return LivePayloadUpdateCheckResult::kNone;
  }
  next_check_ = std::chrono::steady_clock::now() + std::chrono::minutes(15);
  std::vector<char*> environment;
  for (auto& entry : environment_) environment.push_back(entry.data());
  environment.push_back(nullptr);
  std::string helper = helper_.string();
  char command[] = "check-available";
  char* arguments[] = {helper.data(), command, nullptr};
  pid_t child = -1;
  const int status = posix_spawn(&child, helper.c_str(), nullptr, nullptr,
                                 arguments, environment.data());
  if (status == 0) {
    checker_ = child;
  } else {
    std::fprintf(stderr, "  [update] cannot start metadata check: %s\n",
                 std::strerror(status));
  }
  return LivePayloadUpdateCheckResult::kNone;
}

bool LivePayloadUpdateMonitor::BeginScheduleAfterExit(std::string* error) {
  // This function is called only after an explicit choice or an updater-owned
  // availability result. Preserve that authorization across transient helper
  // failures so Aurora can retry the same update on a later close.
  deferred_update_authorized_ = true;
  offered_ = false;
  ReapUpdater();
  if (readiness_descriptor_ >= 0 || updater_ > 0) {
    *error = "A previous Roblox updater handoff is still shutting down";
    return false;
  }
  // A pidfd pins the exact process identity, including when its PID is reused.
  const int process = static_cast<int>(syscall(SYS_pidfd_open, getpid(), 0));
  if (process < 0) {
    *error = "Cannot wait safely for Aurora to close: " +
             std::string(std::strerror(errno));
    return false;
  }
  // Sources must not overlap the fixed child descriptors.
  const int process_source = fcntl(process, F_DUPFD_CLOEXEC, 20);
  close(process);
  const int lock_source = fcntl(instance_descriptor_, F_DUPFD_CLOEXEC, 20);
  if (process_source < 0 || lock_source < 0) {
    if (process_source >= 0) close(process_source);
    if (lock_source >= 0) close(lock_source);
    *error = "Cannot preserve the Aurora process and instance lock";
    return false;
  }
  int readiness[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, readiness) != 0) {
    close(process_source);
    close(lock_source);
    *error = "Cannot create the updater readiness channel: " +
             std::string(std::strerror(errno));
    return false;
  }
  const int parent_flags = fcntl(readiness[0], F_GETFL);
  if (parent_flags < 0 ||
      fcntl(readiness[0], F_SETFL, parent_flags | O_NONBLOCK) != 0) {
    close(readiness[0]);
    close(readiness[1]);
    close(process_source);
    close(lock_source);
    *error = "Cannot configure the updater readiness channel";
    return false;
  }
  const int readiness_source = fcntl(readiness[1], F_DUPFD_CLOEXEC, 20);
  close(readiness[1]);
  readiness[1] = -1;
  if (readiness_source < 0) {
    close(readiness[0]);
    close(process_source);
    close(lock_source);
    *error = "Cannot prepare the updater readiness channel";
    return false;
  }
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  int status = posix_spawn_file_actions_init(&actions);
  const bool actions_ready = status == 0;
  if (status == 0) status = posix_spawnattr_init(&attributes);
  const bool attributes_ready = status == 0;
  if (status == 0) status = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
  if (status == 0) status = posix_spawn_file_actions_adddup2(&actions, process_source, 10);
  if (status == 0) status = posix_spawn_file_actions_adddup2(&actions, lock_source, 11);
  if (status == 0) status = posix_spawn_file_actions_adddup2(&actions, readiness_source, 12);
  if (status == 0) status = posix_spawn_file_actions_addopen(
      &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  if (status == 0) status = posix_spawn_file_actions_addopen(
      &actions, STDOUT_FILENO, log_path_.c_str(),
      O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
  if (status == 0) status = posix_spawn_file_actions_adddup2(
      &actions, STDOUT_FILENO, STDERR_FILENO);
  std::vector<char*> environment;
  for (auto& entry : environment_) environment.push_back(entry.data());
  environment.push_back(nullptr);
  std::string helper = helper_.string();
  char command[] = "update-after-exit";
  char* arguments[] = {helper.data(), command, nullptr};
  pid_t child = -1;
  if (status == 0) status = posix_spawn(&child, helper.c_str(), &actions,
                                       &attributes, arguments, environment.data());
  if (attributes_ready) posix_spawnattr_destroy(&attributes);
  if (actions_ready) posix_spawn_file_actions_destroy(&actions);
  close(process_source);
  close(lock_source);
  close(readiness_source);
  if (status != 0) {
    close(readiness[0]);
    *error = "Cannot schedule the Roblox update: " + std::string(std::strerror(status));
    return false;
  }
  updater_ = static_cast<int>(child);
  updater_acknowledged_ = false;
  readiness_descriptor_ = readiness[0];
  readiness_deadline_ = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  error->clear();
  return true;
}

std::optional<bool> LivePayloadUpdateMonitor::PollScheduleAfterExit(
    std::string* error) {
  if (readiness_descriptor_ < 0) {
    ReapUpdater();
    if (updater_exit_error_.has_value()) {
      *error = std::move(*updater_exit_error_);
      updater_exit_error_.reset();
      return false;
    }
    return std::nullopt;
  }

  pollfd ready_event{readiness_descriptor_, POLLIN | POLLHUP | POLLERR, 0};
  const int ready_status = poll(&ready_event, 1, 0);
  if (ready_status < 0 && errno != EINTR) {
    *error = "Cannot read the updater readiness channel: " +
             std::string(std::strerror(errno));
    FailHandoff();
    return false;
  }
  if (ready_status > 0) {
    char ready = 0;
    const ssize_t ready_bytes =
        recv(readiness_descriptor_, &ready, sizeof(ready), 0);
    if (ready_bytes == 1 && ready == 'R') {
      close(readiness_descriptor_);
      readiness_descriptor_ = -1;
      ReapUpdater();
      if (updater_ <= 0) {
        *error = "The updater exited before completing its exit handoff";
        offered_ = false;
        return false;
      }
      updater_acknowledged_ = true;
      offered_ = true;
      std::fprintf(stderr,
                   "  [update] scheduled verified update after Aurora exits; log: %s\n",
                   log_path_.c_str());
      return true;
    }
    if (ready_bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // HUP can be reported alongside queued socket data; retry on the next
      // frame if another reader has not made the packet available yet.
    } else {
      *error = "The updater could not validate the Aurora exit handoff";
      FailHandoff();
      return false;
    }
  }

  ReapUpdater();
  if (updater_ <= 0) {
    *error = "The updater exited before becoming ready";
    FailHandoff();
    return false;
  }
  if (std::chrono::steady_clock::now() >= readiness_deadline_) {
    *error = "The updater did not become ready in time";
    FailHandoff();
    return false;
  }
  return std::nullopt;
}

PayloadUpdatePreflightResult RunPayloadUpdatePreflight(
    const Environment& environment, const RuntimePaths& paths,
    bool force_run_latest) {
  PayloadUpdatePreflightResult result;
  if (!force_run_latest &&
      (Enabled(environment, "AURORA_SKIP_UPDATE_CHECK") ||
       environment.HasNonEmpty("ROBLOX_LIB_PATH"))) {
    return result;
  }

  const std::filesystem::path helper = UpdateHelper(environment);
  if (helper.empty()) {
    if (force_run_latest) {
      result.error = "cannot locate the Aurora update helper";
    }
    return result;
  }

  result.attempted = true;
  std::string helper_string = helper.string();
  char update[] = "update";
  char startup_preflight[] = "--startup-preflight";
  char force_latest[] = "--force-run-latest";
  char* arguments[] = {helper_string.data(), update,
                       force_run_latest ? force_latest : startup_preflight,
                       nullptr};
  UpdateProgressMonitor progress = UpdateProgressMonitor::Start(environment);
  std::vector<std::string> child_environment =
      ChildEnvironment(paths, progress.active());
  std::vector<char*> child_environment_pointers;
  child_environment_pointers.reserve(child_environment.size() + 1);
  for (std::string& entry : child_environment) {
    child_environment_pointers.push_back(entry.data());
  }
  child_environment_pointers.push_back(nullptr);
  UpdaterStderrRelay stderr_relay;
  posix_spawn_file_actions_t actions;
  bool actions_initialized = false;
  int action_status = 0;
  if (progress.active() || stderr_relay.active()) {
    action_status = posix_spawn_file_actions_init(&actions);
    actions_initialized = action_status == 0;
  }
  if (action_status == 0 && progress.active()) {
    action_status = posix_spawn_file_actions_adddup2(
        &actions, progress.socket(), kUpdaterProgressDescriptor);
  }
  if (action_status == 0 && stderr_relay.active()) {
    action_status = posix_spawn_file_actions_adddup2(
        &actions, stderr_relay.write_descriptor(), STDERR_FILENO);
  }
  if (action_status != 0) {
    if (actions_initialized) {
      posix_spawn_file_actions_destroy(&actions);
    }
    result.error = "cannot prepare Roblox update progress channel: " +
                   std::string(std::strerror(action_status));
    return result;
  }
  pid_t child = -1;
  const int spawn_status = posix_spawn(
      &child, helper.c_str(), actions_initialized ? &actions : nullptr, nullptr,
      arguments, child_environment_pointers.data());
  if (actions_initialized) {
    posix_spawn_file_actions_destroy(&actions);
  }
  if (spawn_status != 0) {
    result.error = "cannot start Roblox update preflight: " +
                   std::string(std::strerror(spawn_status));
    return result;
  }

  // The child owns the write end now; the relay must observe end-of-file.
  stderr_relay.CloseWrite();
  stderr_relay.Drain();

  int child_status = 0;
  while (waitpid(child, &child_status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    result.error = "cannot wait for Roblox update preflight: " +
                   std::string(std::strerror(errno));
    return result;
  }
  if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
    result.error = force_run_latest ? "forced latest Roblox run failed"
                                     : "Roblox update preflight failed";
    result.details = stderr_relay.failure();
  }
  return result;
}

}  // namespace runtime
}  // namespace aurora
