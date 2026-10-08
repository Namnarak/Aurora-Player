#include "runtime/failure_dialog.h"

#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern char** environ;

#ifndef AURORA_INSTALL_LIBDIR
#define AURORA_INSTALL_LIBDIR "lib"
#endif

namespace aurora {
namespace runtime {
namespace {

constexpr char kMessagePacket = 'M';
constexpr char kFailurePacket = 'F';
constexpr char kSuccessPacket = 'S';
constexpr std::size_t kMaximumMessageBytes = 2048;

bool Enabled(const Environment& environment, std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  return value.has_value() && !value->empty() && *value != "0";
}

bool HasNonEmpty(const Environment& environment, std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  return value.has_value() && !value->empty();
}

bool IsExecutableRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) &&
         !std::filesystem::is_symlink(path, error) &&
         access(path.c_str(), X_OK) == 0;
}

std::filesystem::path DialogHelper(const Environment& environment) {
  const std::optional<std::string> helper_override =
      environment.Get("AURORA_FAILURE_DIALOG_HELPER");
  if (helper_override.has_value()) {
    const std::filesystem::path helper(*helper_override);
    return helper.is_absolute() && IsExecutableRegularFile(helper)
               ? helper
               : std::filesystem::path{};
  }

  const std::optional<std::string> project_override =
      environment.Get("AURORA_PROJECT_ROOT");
  if (project_override.has_value()) {
    const std::filesystem::path root(*project_override);
    if (root.is_absolute()) {
      const std::filesystem::path helper =
          root / "bin" / "aurora_failure_dialog";
      if (IsExecutableRegularFile(helper)) {
        return helper;
      }
    }
  }

  std::error_code error;
  const std::filesystem::path executable =
      std::filesystem::read_symlink("/proc/self/exe", error);
  if (error || executable.empty()) {
    return {};
  }
  const std::filesystem::path executable_dir = executable.parent_path();
  const std::vector<std::filesystem::path> candidates = {
      executable_dir / "aurora_failure_dialog",
      executable_dir.parent_path() / AURORA_INSTALL_LIBDIR / "aurora" /
          "aurora_failure_dialog",
      executable_dir.parent_path() / "libexec" / "aurora" /
          "aurora_failure_dialog",
  };
  for (const std::filesystem::path& candidate : candidates) {
    if (IsExecutableRegularFile(candidate)) {
      return candidate;
    }
  }
  return {};
}

void WaitForHelper(int helper_pid) {
  if (helper_pid <= 0) {
    return;
  }
  while (waitpid(helper_pid, nullptr, 0) < 0 && errno == EINTR) {
  }
}

bool SpawnOneShot(const std::filesystem::path& helper,
                  std::string_view option,
                  std::string_view message) {
  std::string helper_string = helper.string();
  std::string option_string(option);
  std::string message_string(message.substr(0, kMaximumMessageBytes));
  char* arguments[] = {helper_string.data(), option_string.data(),
                       message_string.data(), nullptr};
  pid_t child = -1;
  const int spawn_status =
      posix_spawn(&child, helper.c_str(), nullptr, nullptr, arguments, environ);
  if (spawn_status != 0) {
    return false;
  }
  WaitForHelper(child);
  return true;
}

std::pair<int, int> SpawnMonitor(const std::filesystem::path& helper) {
  int sockets[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
    return {-1, -1};
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(sockets[0]);
    close(sockets[1]);
    return {-1, -1};
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
  char monitor_option[] = "--monitor";
  char* arguments[] = {helper_string.data(), monitor_option, nullptr};
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
    return {-1, -1};
  }
  return {sockets[0], static_cast<int>(child)};
}

std::pair<int, int> SpawnChoiceDialog(const std::filesystem::path& helper,
                                     std::string_view heading,
                                     std::string_view message,
                                     std::string_view first_response,
                                     std::string_view second_response,
                                     std::string_view third_response,
                                     std::uint8_t enabled_responses) {
  int sockets[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
    return {-1, -1};
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(sockets[0]);
    close(sockets[1]);
    return {-1, -1};
  }
  int action_status = 0;
  if (sockets[0] != STDIN_FILENO) {
    action_status = posix_spawn_file_actions_addclose(&actions, sockets[0]);
  }
  if (action_status == 0 && sockets[1] != STDIN_FILENO) {
    action_status =
        posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
  }
  if (action_status == 0 && sockets[1] != STDIN_FILENO) {
    action_status = posix_spawn_file_actions_addclose(&actions, sockets[1]);
  }

  std::string helper_string = helper.string();
  std::string heading_string(heading.substr(0, kMaximumMessageBytes));
  std::string message_string(message.substr(0, kMaximumMessageBytes));
  std::string first_string(first_response.substr(0, 128));
  std::string second_string(second_response.substr(0, 128));
  std::string third_string(third_response.substr(0, 128));
  char enabled_string[] = {
      (enabled_responses & 0x01) != 0 ? '1' : '0',
      (enabled_responses & 0x02) != 0 ? '1' : '0',
      (enabled_responses & 0x04) != 0 ? '1' : '0', '\0'};
  char choice_option[] = "--choice";
  char* arguments[] = {helper_string.data(), choice_option,
                       heading_string.data(), message_string.data(),
                       first_string.data(), second_string.data(),
                       third_string.data(), enabled_string, nullptr};
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
    return {-1, -1};
  }
  const int flags = fcntl(sockets[0], F_GETFL);
  if (flags < 0 || fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    close(sockets[0]);
    (void)kill(child, SIGTERM);
    return {-1, static_cast<int>(child)};
  }
  return {sockets[0], static_cast<int>(child)};
}

bool SendPacket(int socket, char kind, std::string_view payload = {}) {
  std::string packet(1, kind);
  packet.append(payload.substr(0, kMaximumMessageBytes));
  const ssize_t sent = send(socket, packet.data(), packet.size(), MSG_NOSIGNAL);
  return sent == static_cast<ssize_t>(packet.size());
}

}  // namespace

bool FailureDialogsEnabled(const Environment& environment) {
  if (Enabled(environment, "AURORA_DISABLE_FAILURE_DIALOG") ||
      Enabled(environment, "AURORA_ISOLATED_CANARY") ||
      Enabled(environment, "AURORA_HEADLESS")) {
    return false;
  }
  return HasNonEmpty(environment, "DISPLAY") ||
         HasNonEmpty(environment, "WAYLAND_DISPLAY");
}

bool ShowFailureDialog(const Environment& environment,
                       std::string_view message) {
  if (!FailureDialogsEnabled(environment)) {
    return false;
  }
  const std::filesystem::path helper = DialogHelper(environment);
  return !helper.empty() && SpawnOneShot(helper, "--message", message);
}

bool ShowAlreadyRunningDialog(const Environment& environment,
                              std::string_view message) {
  if (!FailureDialogsEnabled(environment)) {
    return false;
  }
  const std::filesystem::path helper = DialogHelper(environment);
  return !helper.empty() &&
         SpawnOneShot(helper, "--already-running", message);
}

bool ShowWarningDialog(const Environment& environment,
                       std::string_view message) {
  if (!FailureDialogsEnabled(environment)) {
    return false;
  }
  const std::filesystem::path helper = DialogHelper(environment);
  return !helper.empty() && SpawnOneShot(helper, "--warning", message);
}

ChoiceDialog::ChoiceDialog(int socket, int helper_pid)
    : socket_(socket) {
  if (helper_pid > 0) helper_pids_.push_back(helper_pid);
}

ChoiceDialog::~ChoiceDialog() { Dismiss(); }

ChoiceDialog::ChoiceDialog(ChoiceDialog&& other)
    : socket_(std::exchange(other.socket_, -1)),
      helper_pids_(std::move(other.helper_pids_)),
      response_(std::exchange(other.response_, std::nullopt)) {}

ChoiceDialog& ChoiceDialog::operator=(ChoiceDialog&& other) {
  if (this == &other) {
    return *this;
  }
  Dismiss();
  socket_ = std::exchange(other.socket_, -1);
  helper_pids_.insert(helper_pids_.end(), other.helper_pids_.begin(),
                      other.helper_pids_.end());
  other.helper_pids_.clear();
  response_ = std::exchange(other.response_, std::nullopt);
  return *this;
}

ChoiceDialog ChoiceDialog::Start(const Environment& environment,
                                std::string_view heading,
                                std::string_view message,
                                std::string_view first_response,
                                std::string_view second_response,
                                std::string_view third_response,
                                std::uint8_t enabled_responses) {
  if (!FailureDialogsEnabled(environment)) {
    return {};
  }
  const std::filesystem::path helper = DialogHelper(environment);
  if (helper.empty()) {
    return {};
  }
  const auto [socket, helper_pid] = SpawnChoiceDialog(
      helper, heading, message, first_response, second_response, third_response,
      enabled_responses);
  return ChoiceDialog(socket, helper_pid);
}

std::optional<int> ChoiceDialog::Poll() {
  ReapHelpers();
  if (response_.has_value()) {
    return std::exchange(response_, std::nullopt);
  }
  if (socket_ < 0) return std::nullopt;
  char response = '2';
  ssize_t received = -1;
  do {
    received = recv(socket_, &response, sizeof(response), 0);
  } while (received < 0 && errno == EINTR);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return std::nullopt;
  }
  close(socket_);
  socket_ = -1;
  response_ = received == 1 && response >= '0' && response <= '2'
                  ? response - '0'
                  : 2;
  return std::exchange(response_, std::nullopt);
}

void ChoiceDialog::Dismiss() {
  if (socket_ >= 0) {
    close(socket_);
    socket_ = -1;
  }

  ReapHelpers();
}

void ChoiceDialog::ReapHelpers() {
  auto helper = helper_pids_.begin();
  while (helper != helper_pids_.end()) {
    const pid_t completed = waitpid(*helper, nullptr, WNOHANG);
    if (completed == *helper || (completed < 0 && errno != EINTR)) {
      helper = helper_pids_.erase(helper);
    } else {
      ++helper;
    }
  }
}

FailureDialogMonitor::FailureDialogMonitor(int socket, int helper_pid)
    : socket_(socket), helper_pid_(helper_pid) {}

FailureDialogMonitor::~FailureDialogMonitor() { Finish(kFailurePacket); }

FailureDialogMonitor::FailureDialogMonitor(
    FailureDialogMonitor&& other) noexcept
    : socket_(std::exchange(other.socket_, -1)),
      helper_pid_(std::exchange(other.helper_pid_, -1)) {}

FailureDialogMonitor& FailureDialogMonitor::operator=(
    FailureDialogMonitor&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  Finish(kFailurePacket);
  socket_ = std::exchange(other.socket_, -1);
  helper_pid_ = std::exchange(other.helper_pid_, -1);
  return *this;
}

FailureDialogMonitor FailureDialogMonitor::Start(
    const Environment& environment, std::string_view initial_message) {
  if (!FailureDialogsEnabled(environment)) {
    return {};
  }
  const std::filesystem::path helper = DialogHelper(environment);
  if (helper.empty()) {
    return {};
  }
  const auto [socket, helper_pid] = SpawnMonitor(helper);
  FailureDialogMonitor monitor(socket, helper_pid);
  if (monitor.active()) {
    monitor.SetMessage(initial_message);
  }
  return monitor;
}

void FailureDialogMonitor::SetMessage(std::string_view message) {
  if (!active() || message.empty()) {
    return;
  }
  if (!SendPacket(socket_, kMessagePacket, message)) {
    close(socket_);
    socket_ = -1;
    WaitForHelper(helper_pid_);
    helper_pid_ = -1;
  }
}

void FailureDialogMonitor::MarkSuccessful() { Finish(kSuccessPacket); }

void FailureDialogMonitor::Finish(char disposition) {
  if (!active()) {
    return;
  }
  (void)SendPacket(socket_, disposition);
  (void)shutdown(socket_, SHUT_RDWR);
  close(socket_);
  socket_ = -1;
  WaitForHelper(helper_pid_);
  helper_pid_ = -1;
}

}  // namespace runtime
}  // namespace aurora
