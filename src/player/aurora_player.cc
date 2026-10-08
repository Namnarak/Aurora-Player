// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path ExecutablePath() {
  char buffer[4096] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return {};
  buffer[length] = '\0';
  return std::filesystem::path(buffer);
}

int Run(int argc, char* argv[]) {
  const std::filesystem::path self = ExecutablePath();
  if (self.empty()) {
    std::fprintf(stderr, "Aurora Player: cannot resolve executable path\n");
    return 127;
  }
  const std::filesystem::path runtime = self.parent_path() / "aurora";
  if (access(runtime.c_str(), X_OK) != 0) {
    std::fprintf(stderr, "Aurora Player: runtime is not executable: %s\n",
                 runtime.c_str());
    return 127;
  }

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc) + 1);
  arguments.push_back(runtime.string());
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == nullptr) {
      std::fprintf(stderr, "Aurora Player: null command-line argument\n");
      return 2;
    }
    arguments.emplace_back(argv[index]);
  }
  std::vector<char*> child_argv;
  child_argv.reserve(arguments.size() + 1);
  for (std::string& argument : arguments) child_argv.push_back(argument.data());
  child_argv.push_back(nullptr);
  execv(runtime.c_str(), child_argv.data());
  const int error_number = errno;
  std::fprintf(stderr, "Aurora Player: cannot start runtime: %s\n",
               std::strerror(error_number));
  return 127;
}

}  // namespace

int main(int argc, char* argv[]) { return Run(argc, argv); }
