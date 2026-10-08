// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/managed_process.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

extern char** environ;

namespace aurora {
namespace runtime {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

bool SetCloseOnExec(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFD);
  return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void ChildFailure(int descriptor, int error_number) {
  const char* bytes = reinterpret_cast<const char*>(&error_number);
  std::size_t offset = 0;
  while (offset < sizeof(error_number)) {
    const ssize_t written = write(descriptor, bytes + offset,
                                  sizeof(error_number) - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    break;
  }
  _exit(127);
}

std::filesystem::path AbsolutePath(const std::filesystem::path& path) {
  std::error_code filesystem_error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(path, filesystem_error);
  return filesystem_error ? path : absolute;
}

std::optional<std::string> ResolveExecutable(const std::string& executable,
                                             std::string* error) {
  if (executable.empty()) {
    SetError(error, "managed process executable is empty");
    return std::nullopt;
  }

  if (executable.find('/') != std::string::npos) {
    const std::filesystem::path path = AbsolutePath(executable);
    if (access(path.c_str(), X_OK) != 0) {
      SetError(error, "managed process executable is not executable: " +
                           path.string());
      return std::nullopt;
    }
    return path.string();
  }

  const char* path_variable = std::getenv("PATH");
  const std::string search_path =
      path_variable == nullptr ? "/usr/local/bin:/usr/bin:/bin"
                               : std::string(path_variable);
  std::size_t start = 0;
  while (start <= search_path.size()) {
    const std::size_t separator = search_path.find(':', start);
    const std::string directory =
        separator == std::string::npos
            ? search_path.substr(start)
            : search_path.substr(start, separator - start);
    const std::filesystem::path candidate =
        AbsolutePath(std::filesystem::path(directory.empty() ? "." : directory) /
                     executable);
    if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
    if (separator == std::string::npos) break;
    start = separator + 1;
  }
  SetError(error, "managed process executable was not found in PATH: " +
                       executable);
  return std::nullopt;
}

bool BuildEnvironment(const std::vector<std::string>& overrides,
                      std::vector<std::string>* values, std::string* error) {
  if (values == nullptr) {
    SetError(error, "managed process environment output is null");
    return false;
  }
  values->clear();
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    values->emplace_back(*entry);
  }
  for (const std::string& override_value : overrides) {
    const std::size_t separator = override_value.find('=');
    if (separator == std::string::npos || separator == 0) {
      SetError(error, "managed process environment entry must be NAME=VALUE");
      return false;
    }
    const std::string name = override_value.substr(0, separator);
    bool replaced = false;
    for (std::string& value : *values) {
      if (value.compare(0, name.size(), name) == 0 &&
          value.size() > name.size() && value[name.size()] == '=') {
        value = override_value;
        replaced = true;
        break;
      }
    }
    if (!replaced) values->push_back(override_value);
  }
  return true;
}

bool WaitForPid(pid_t pid, int* wait_status, std::string* error) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    SetError(error, "waitpid failed: " + std::string(std::strerror(errno)));
    return false;
  }
  if (wait_status != nullptr) *wait_status = status;
  return true;
}

void SignalProcessGroup(pid_t pid, int signal_number) {
  if (pid <= 0) return;
  // The child creates this group before exec. The direct-child fallback also
  // handles the tiny race before the parent's view of the group is updated.
  if (kill(-pid, signal_number) != 0 && errno != ESRCH) return;
  (void)kill(pid, signal_number);
}

}  // namespace

ManagedProcess::~ManagedProcess() {
  if (running()) {
    std::string ignored;
    (void)Terminate(std::chrono::milliseconds(750), &ignored);
  }
}

ManagedProcess::ManagedProcess(ManagedProcess&& other) noexcept
    : pid_(other.pid_), exit_status_(std::move(other.exit_status_)) {
  other.pid_ = -1;
  other.exit_status_.reset();
}

ManagedProcess& ManagedProcess::operator=(ManagedProcess&& other) noexcept {
  if (this == &other) return *this;
  if (running()) {
    std::string ignored;
    (void)Terminate(std::chrono::milliseconds(750), &ignored);
  }
  pid_ = other.pid_;
  exit_status_ = std::move(other.exit_status_);
  other.pid_ = -1;
  other.exit_status_.reset();
  return *this;
}

ManagedProcess ManagedProcess::Start(const ManagedProcessSpec& spec,
                                     std::string* error) {
  if (error != nullptr) error->clear();
  const std::optional<std::string> resolved =
      ResolveExecutable(spec.executable, error);
  if (!resolved.has_value()) return {};

  std::vector<std::string> environment;
  if (!BuildEnvironment(spec.environment, &environment, error)) return {};

  std::vector<std::string> arguments;
  arguments.reserve(spec.arguments.size() + 1);
  arguments.push_back(*resolved);
  for (const std::string& argument : spec.arguments) {
    if (argument.find('\0') != std::string::npos) {
      SetError(error, "managed process argument contains NUL");
      return {};
    }
    arguments.push_back(argument);
  }
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  std::vector<char*> envp;
  envp.reserve(environment.size() + 1);
  for (std::string& value : environment) envp.push_back(value.data());
  envp.push_back(nullptr);

  int exec_error_pipe[2] = {-1, -1};
  if (pipe(exec_error_pipe) != 0 ||
      !SetCloseOnExec(exec_error_pipe[0]) ||
      !SetCloseOnExec(exec_error_pipe[1])) {
    const int saved_errno = errno;
    if (exec_error_pipe[0] >= 0) close(exec_error_pipe[0]);
    if (exec_error_pipe[1] >= 0) close(exec_error_pipe[1]);
    SetError(error, "cannot create managed process error pipe: " +
                         std::string(std::strerror(saved_errno)));
    return {};
  }

  const pid_t child = fork();
  if (child < 0) {
    const int saved_errno = errno;
    close(exec_error_pipe[0]);
    close(exec_error_pipe[1]);
    SetError(error, "cannot fork managed process: " +
                         std::string(std::strerror(saved_errno)));
    return {};
  }

  if (child == 0) {
    close(exec_error_pipe[0]);
    if (setpgid(0, 0) != 0) ChildFailure(exec_error_pipe[1], errno);
    if (!spec.working_directory.empty() &&
        chdir(spec.working_directory.c_str()) != 0) {
      ChildFailure(exec_error_pipe[1], errno);
    }
    if (!spec.inherit_stdio) {
      const int null_device = open("/dev/null", O_RDWR);
      if (null_device < 0) ChildFailure(exec_error_pipe[1], errno);
      if (dup2(null_device, STDIN_FILENO) < 0 ||
          dup2(null_device, STDOUT_FILENO) < 0 ||
          dup2(null_device, STDERR_FILENO) < 0) {
        const int saved_errno = errno;
        close(null_device);
        ChildFailure(exec_error_pipe[1], saved_errno);
      }
      if (null_device > STDERR_FILENO) close(null_device);
    }
    execve(argv[0], argv.data(), envp.data());
    ChildFailure(exec_error_pipe[1], errno);
  }

  close(exec_error_pipe[1]);
  int child_errno = 0;
  ssize_t received = 0;
  do {
    received = read(exec_error_pipe[0], &child_errno, sizeof(child_errno));
  } while (received < 0 && errno == EINTR);
  const int read_errno = errno;
  close(exec_error_pipe[0]);
  if (received > 0) {
    int ignored_status = 0;
    (void)WaitForPid(child, &ignored_status, nullptr);
    SetError(error, "managed process failed before exec: " +
                         std::string(std::strerror(child_errno)));
    return {};
  }
  if (received < 0) {
    SignalProcessGroup(child, SIGKILL);
    int ignored_status = 0;
    (void)WaitForPid(child, &ignored_status, nullptr);
    SetError(error, "cannot read managed process status: " +
                         std::string(std::strerror(read_errno)));
    return {};
  }
  return ManagedProcess(child);
}

std::optional<int> ManagedProcess::Poll(std::string* error) {
  if (error != nullptr) error->clear();
  if (exit_status_.has_value()) return exit_status_;
  if (pid_ <= 0) return std::nullopt;

  int status = 0;
  const pid_t result = waitpid(pid_, &status, WNOHANG);
  if (result == 0) return std::nullopt;
  if (result < 0) {
    if (errno == EINTR) return std::nullopt;
    SetError(error, "waitpid poll failed: " +
                         std::string(std::strerror(errno)));
    return std::nullopt;
  }
  pid_ = -1;
  exit_status_ = status;
  return exit_status_;
}

bool ManagedProcess::Wait(int* wait_status, std::string* error) {
  if (error != nullptr) error->clear();
  if (exit_status_.has_value()) {
    if (wait_status != nullptr) *wait_status = *exit_status_;
    return true;
  }
  if (pid_ <= 0) {
    SetError(error, "managed process is not running");
    return false;
  }
  int status = 0;
  if (!WaitForPid(pid_, &status, error)) return false;
  pid_ = -1;
  exit_status_ = status;
  if (wait_status != nullptr) *wait_status = status;
  return true;
}

bool ManagedProcess::Terminate(std::chrono::milliseconds timeout,
                               std::string* error) {
  if (error != nullptr) error->clear();
  if (!running()) {
    return true;
  }

  SignalProcessGroup(pid_, SIGTERM);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::max(timeout, std::chrono::milliseconds::zero());
  for (;;) {
    std::string poll_error;
    const std::optional<int> status = Poll(&poll_error);
    if (!poll_error.empty()) {
      SetError(error, poll_error);
      return false;
    }
    if (status.has_value()) return true;
    if (std::chrono::steady_clock::now() >= deadline) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  SignalProcessGroup(pid_, SIGKILL);
  return Wait(nullptr, error);
}

}  // namespace runtime
}  // namespace aurora
