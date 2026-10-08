// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/external_client_detector.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace aurora {
namespace runtime {
namespace {

constexpr std::uint16_t kDefaultStudioMcpPort = 13469;

struct ProcessObservation {
  DetectedClient client;
  pid_t pid = -1;
  std::string executable;
};

std::string Lower(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<char>(std::tolower(character)));
  }
  return result;
}

bool Contains(std::string_view value, std::string_view needle) {
  return value.find(needle) != std::string_view::npos;
}

std::string BaseName(std::string_view value) {
  const std::size_t separator = value.rfind('/');
  const std::size_t start = separator == std::string_view::npos
                                ? 0
                                : separator + 1;
  return Lower(value.substr(start));
}

std::string FirstArgument(std::string_view cmdline) {
  const std::size_t end = cmdline.find('\0');
  return std::string(cmdline.substr(0, end));
}

bool IsNumeric(std::string_view value) {
  if (value.empty()) return false;
  for (const unsigned char character : value) {
    if (!std::isdigit(character)) return false;
  }
  return true;
}

std::string ReadTextFile(const std::filesystem::path& path, bool binary) {
  std::ifstream input(path, binary ? std::ios::binary : std::ios::in);
  if (!input) return {};
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

std::string ReadComm(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) return {};
  std::string value;
  std::getline(input, value);
  return value;
}

bool IsExecutable(const std::filesystem::path& path) {
  return !path.empty() && access(path.c_str(), X_OK) == 0;
}

bool IsRegularFile(const std::filesystem::path& path) {
  std::error_code filesystem_error;
  const std::filesystem::file_status status =
      std::filesystem::status(path, filesystem_error);
  return !filesystem_error && std::filesystem::is_regular_file(status);
}

std::filesystem::path HomePath() {
  const char* home = std::getenv("HOME");
  return home == nullptr || home[0] == '\0' ? std::filesystem::path(".")
                                             : std::filesystem::path(home);
}

std::filesystem::path StudioRuntimeVersionsPath() {
  const char* configured_runtime = std::getenv("AURORA_STUDIO_RUNTIME_DATA");
  if (configured_runtime != nullptr && configured_runtime[0] != '\0') {
    return std::filesystem::path(configured_runtime) / "versions";
  }
  const char* configured_data = std::getenv("XDG_DATA_HOME");
  const std::filesystem::path root =
      configured_data != nullptr && configured_data[0] == '/'
          ? std::filesystem::path(configured_data)
          : HomePath() / ".local/share";
  return root / "aurora/studio-runtime/versions";
}

std::optional<std::filesystem::path> FindOnPath(std::string_view name) {
  const char* configured_path = std::getenv("PATH");
  const std::string search_path = configured_path == nullptr
                                      ? "/usr/local/bin:/usr/bin:/bin"
                                      : std::string(configured_path);
  const std::string filename(name);
  std::size_t start = 0;
  while (start <= search_path.size()) {
    const std::size_t separator = search_path.find(':', start);
    const std::string directory =
        separator == std::string::npos
            ? search_path.substr(start)
            : search_path.substr(start, separator - start);
    const std::filesystem::path candidate =
        std::filesystem::path(directory.empty() ? "." : directory) / filename;
    if (IsExecutable(candidate)) return candidate;
    if (separator == std::string::npos) break;
    start = separator + 1;
  }
  return std::nullopt;
}

bool IsRegularExecutable(const std::filesystem::path& path) {
  return IsRegularFile(path) && IsExecutable(path);
}

std::optional<std::filesystem::path> FindKnownExecutable(
    DetectedClient client) {
  std::vector<std::filesystem::path> candidates;
  const std::filesystem::path home = HomePath();
  switch (client) {
    case DetectedClient::kAntigravity:
      if (const auto path = FindOnPath("antigravity"); path.has_value()) {
        return path;
      }
      candidates = {"/usr/bin/antigravity", "/opt/Antigravity/antigravity",
                   "/opt/antigravity/antigravity",
                   home / ".local/bin/antigravity"};
      break;
    case DetectedClient::kCodexCli:
      if (const auto path = FindOnPath("codex"); path.has_value()) {
        return path;
      }
      candidates = {home / ".local/bin/codex"};
      break;
    case DetectedClient::kClaudeCode:
      if (const auto path = FindOnPath("claude"); path.has_value()) {
        return path;
      }
      candidates = {home / ".local/bin/claude", home / ".npm/bin/claude"};
      break;
    case DetectedClient::kClaudeDesktop:
      candidates = {"/usr/bin/claude-desktop",
                    "/usr/lib/claude-desktop/claude-desktop",
                    "/opt/Claude/claude",
                    home / ".local/bin/claude-desktop"};
      break;
    case DetectedClient::kCursor:
      if (const auto path = FindOnPath("cursor"); path.has_value()) {
        return path;
      }
      candidates = {"/usr/bin/cursor", "/opt/Cursor/cursor",
                    "/opt/cursor/cursor", home / ".local/bin/cursor"};
      break;
    case DetectedClient::kGeminiCli:
      if (const auto path = FindOnPath("gemini"); path.has_value()) {
        return path;
      }
      if (const auto path = FindOnPath("gemini-cli"); path.has_value()) {
        return path;
      }
      candidates = {home / ".local/bin/gemini", home / ".npm/bin/gemini",
                   home / ".local/bin/gemini-cli"};
      break;
    case DetectedClient::kVisualStudioCode:
      if (const auto path = FindOnPath("code"); path.has_value()) {
        return path;
      }
      if (const auto path = FindOnPath("code-insiders"); path.has_value()) {
        return path;
      }
      candidates = {"/usr/bin/code", "/usr/share/code/code",
                    "/usr/bin/code-insiders", "/usr/share/code-insiders/code",
                    "/opt/visual-studio-code/code",
                    home / ".local/bin/code"};
      break;
    case DetectedClient::kCodexDesktop:
      candidates = {"/opt/codex-desktop/ChatGPT",
                    "/usr/bin/codex-desktop",
                    home / ".local/bin/codex-desktop"};
      break;
    case DetectedClient::kRobloxStudioMcp:
      // The Studio MCP payload lives inside Aurora's independently managed
      // Studio runtime and is discovered below by FindStudioMcpPayload().
      break;
  }
  for (const std::filesystem::path& candidate : candidates) {
    if (IsRegularExecutable(candidate)) return candidate;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> FindStudioMcpPayload() {
  const char* configured = std::getenv("AURORA_ROBLOX_STUDIO_MCP");
  if (configured != nullptr && configured[0] != '\0' &&
      IsRegularFile(configured)) {
    return std::filesystem::path(configured);
  }

  const std::filesystem::path versions = StudioRuntimeVersionsPath();
  std::error_code filesystem_error;
  for (std::filesystem::directory_iterator iterator(versions, filesystem_error),
       end;
       iterator != end && !filesystem_error; iterator.increment(filesystem_error)) {
    const std::filesystem::path candidate = iterator->path() / "StudioMCP.exe";
    if (IsRegularFile(candidate)) return candidate;
  }
  return std::nullopt;
}

std::size_t IndexFor(DetectedClient client) {
  switch (client) {
    case DetectedClient::kAntigravity: return 0;
    case DetectedClient::kCodexCli: return 1;
    case DetectedClient::kClaudeCode: return 2;
    case DetectedClient::kClaudeDesktop: return 3;
    case DetectedClient::kCursor: return 4;
    case DetectedClient::kGeminiCli: return 5;
    case DetectedClient::kVisualStudioCode: return 6;
    case DetectedClient::kCodexDesktop: return 7;
    case DetectedClient::kRobloxStudioMcp: return 8;
  }
  return 0;
}

std::uint16_t StudioMcpPort() {
  const char* configured = std::getenv("AURORA_ROBLOX_STUDIO_MCP_PORT");
  if (configured == nullptr || configured[0] == '\0') {
    return kDefaultStudioMcpPort;
  }
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(configured, &end, 10);
  if (errno != 0 || end == configured || *end != '\0' || parsed < 1 ||
      parsed > 65535) {
    return kDefaultStudioMcpPort;
  }
  return static_cast<std::uint16_t>(parsed);
}

bool LocalPortAccepts(std::uint16_t port) {
  const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) return false;

  const int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(socket_fd);
    return false;
  }

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
    close(socket_fd);
    return false;
  }

  const int result = connect(socket_fd, reinterpret_cast<sockaddr*>(&address),
                             sizeof(address));
  if (result == 0) {
    close(socket_fd);
    return true;
  }
  if (errno != EINPROGRESS) {
    close(socket_fd);
    return false;
  }

  pollfd descriptor = {socket_fd, POLLOUT, 0};
  const int poll_result = poll(&descriptor, 1, 100);
  if (poll_result <= 0 || (descriptor.revents & (POLLOUT | POLLERR | POLLHUP)) == 0) {
    close(socket_fd);
    return false;
  }
  int socket_error = 0;
  socklen_t socket_error_size = sizeof(socket_error);
  const bool connected =
      getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                 &socket_error_size) == 0 &&
      socket_error == 0;
  close(socket_fd);
  return connected;
}

}  // namespace

ExternalClientDetector::ExternalClientDetector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

std::optional<DetectedClient> ExternalClientDetector::ClassifyProcess(
    std::string_view comm, std::string_view cmdline,
    std::string_view executable) {
  const std::string lower_comm = Lower(comm);
  const std::string lower_cmdline = Lower(cmdline);
  const std::string lower_executable = Lower(executable);
  const std::string executable_name = BaseName(executable);
  const std::string first_argument = BaseName(FirstArgument(cmdline));

  if (Contains(lower_comm, "studiomcp") ||
      Contains(lower_executable, "studiomcp") ||
      Contains(lower_cmdline, "studiomcp.exe")) {
    return DetectedClient::kRobloxStudioMcp;
  }

  // The desktop bundle ships a computer-use helper under its plugin tree.
  // It is not the Codex application or CLI, so do not let that helper make
  // the desktop status look active when the main app is closed.
  if (Contains(lower_executable, "/codex-desktop/") &&
      (Contains(lower_executable, "/plugins/") ||
       Contains(lower_executable, "codex-computer-use"))) {
    return std::nullopt;
  }

  if (Contains(lower_executable, "claude-desktop") ||
      Contains(lower_cmdline, "claude-desktop") ||
      Contains(lower_executable, "/usr/lib/claude/")) {
    return DetectedClient::kClaudeDesktop;
  }
  if (Contains(lower_executable, "/claude-code/") ||
      Contains(lower_cmdline, "claude-code") || executable_name == "claude" ||
      lower_comm == "claude" || first_argument == "claude") {
    return DetectedClient::kClaudeCode;
  }

  if (executable_name == "antigravity" || lower_comm == "antigravity" ||
      first_argument == "antigravity" ||
      Contains(lower_executable, "/antigravity/")) {
    return DetectedClient::kAntigravity;
  }

  if (Contains(lower_executable, "/codex-desktop/") ||
      Contains(lower_executable, "codex-desktop") ||
      Contains(lower_cmdline, "codex-desktop") ||
      (executable_name == "chatgpt" && Contains(lower_executable, "/opt/"))) {
    return DetectedClient::kCodexDesktop;
  }
  if (executable_name == "codex" || lower_comm == "codex" ||
      first_argument == "codex") {
    return DetectedClient::kCodexCli;
  }

  if (executable_name == "cursor" || lower_comm == "cursor" ||
      first_argument == "cursor" || Contains(lower_executable, "/cursor/")) {
    return DetectedClient::kCursor;
  }

  if (executable_name == "gemini" || executable_name == "gemini-cli" ||
      lower_comm == "gemini" || lower_comm == "gemini-cli" ||
      first_argument == "gemini" || first_argument == "gemini-cli" ||
      Contains(lower_executable, "/gemini-cli/")) {
    return DetectedClient::kGeminiCli;
  }

  if (executable_name == "code" || executable_name == "code-insiders" ||
      lower_comm == "code" || lower_comm == "code-insiders" ||
      first_argument == "code" || first_argument == "code-insiders" ||
      Contains(lower_executable, "/visual-studio-code/")) {
    return DetectedClient::kVisualStudioCode;
  }
  return std::nullopt;
}

const char* ExternalClientDetector::Id(DetectedClient client) {
  switch (client) {
    case DetectedClient::kAntigravity: return "antigravity";
    case DetectedClient::kCodexCli: return "codex_cli";
    case DetectedClient::kClaudeCode: return "claude_code";
    case DetectedClient::kClaudeDesktop: return "claude_desktop";
    case DetectedClient::kCursor: return "cursor";
    case DetectedClient::kGeminiCli: return "gemini_cli";
    case DetectedClient::kVisualStudioCode: return "visual_studio_code";
    case DetectedClient::kCodexDesktop: return "codex_desktop";
    case DetectedClient::kRobloxStudioMcp: return "roblox_studio_mcp";
  }
  return "unknown";
}

const char* ExternalClientDetector::Name(DetectedClient client) {
  switch (client) {
    case DetectedClient::kAntigravity: return "Antigravity";
    case DetectedClient::kCodexCli: return "Codex CLI";
    case DetectedClient::kClaudeCode: return "Claude Code";
    case DetectedClient::kClaudeDesktop: return "Claude Desktop";
    case DetectedClient::kCursor: return "Cursor";
    case DetectedClient::kGeminiCli: return "Gemini CLI";
    case DetectedClient::kVisualStudioCode: return "Visual Studio Code";
    case DetectedClient::kCodexDesktop: return "Codex Desktop / ChatGPT";
    case DetectedClient::kRobloxStudioMcp: return "Roblox Studio MCP";
  }
  return "Unknown client";
}

const char* ExternalClientDetector::Kind(DetectedClient client) {
  switch (client) {
    case DetectedClient::kAntigravity:
    case DetectedClient::kCursor:
      return "ai_ide";
    case DetectedClient::kVisualStudioCode:
      return "ide";
    case DetectedClient::kClaudeDesktop:
    case DetectedClient::kCodexDesktop:
      return "ai_app";
    case DetectedClient::kClaudeCode:
    case DetectedClient::kGeminiCli:
    case DetectedClient::kCodexCli:
      return "ai_cli";
    case DetectedClient::kRobloxStudioMcp:
      return "mcp_bridge";
  }
  return "unknown";
}

std::vector<ExternalClientStatus> ExternalClientDetector::Detect() const {
  constexpr std::array<DetectedClient, 9> kClients = {
      DetectedClient::kAntigravity, DetectedClient::kCodexCli,
      DetectedClient::kClaudeCode, DetectedClient::kClaudeDesktop,
      DetectedClient::kCursor, DetectedClient::kGeminiCli,
      DetectedClient::kVisualStudioCode, DetectedClient::kCodexDesktop,
      DetectedClient::kRobloxStudioMcp};

  std::vector<ExternalClientStatus> result;
  result.reserve(kClients.size());
  for (const DetectedClient client : kClients) {
    ExternalClientStatus status;
    status.client = client;
    status.id = Id(client);
    status.name = Name(client);
    status.kind = Kind(client);
    if (const auto installed = FindKnownExecutable(client);
        installed.has_value()) {
      status.installed = true;
      status.executable = installed->string();
    }
    if (client == DetectedClient::kRobloxStudioMcp) {
      status.port = StudioMcpPort();
      if (const auto payload = FindStudioMcpPayload(); payload.has_value()) {
        status.installed = true;
        if (status.executable.empty()) status.executable = payload->string();
      }
    }
    result.push_back(std::move(status));
  }

  std::error_code filesystem_error;
  for (std::filesystem::directory_iterator iterator(
           proc_root_, std::filesystem::directory_options::skip_permission_denied,
           filesystem_error),
       end;
       iterator != end && !filesystem_error;
       iterator.increment(filesystem_error)) {
    const std::string pid_name = iterator->path().filename().string();
    if (!IsNumeric(pid_name)) continue;
    char* pid_end = nullptr;
    errno = 0;
    const long parsed_pid = std::strtol(pid_name.c_str(), &pid_end, 10);
    if (errno != 0 || pid_end == pid_name.c_str() || *pid_end != '\0' ||
        parsed_pid <= 0) {
      continue;
    }
    const std::filesystem::path process_root = iterator->path();
    const std::string comm = ReadComm(process_root / "comm");
    const std::string cmdline = ReadTextFile(process_root / "cmdline", true);
    std::error_code link_error;
    const std::filesystem::path executable_path =
        std::filesystem::read_symlink(process_root / "exe", link_error);
    const std::string executable = link_error ? std::string()
                                               : executable_path.string();
    const auto client = ClassifyProcess(comm, cmdline, executable);
    if (!client.has_value()) continue;

    ProcessObservation observation{*client, static_cast<pid_t>(parsed_pid),
                                   executable.empty() ? comm : executable};
    ExternalClientStatus& status = result[IndexFor(observation.client)];
    status.installed = true;
    status.running = true;
    if (status.pid <= 0 || observation.pid < status.pid) {
      status.pid = observation.pid;
      status.executable = observation.executable;
    }
  }

  ExternalClientStatus& studio = result[IndexFor(DetectedClient::kRobloxStudioMcp)];
  studio.connected = studio.port != 0 && LocalPortAccepts(studio.port);
  if (studio.connected) {
    studio.installed = true;
    studio.running = true;
    studio.detail = "MCP bridge reachable on 127.0.0.1:" +
                    std::to_string(studio.port);
  }

  for (ExternalClientStatus& status : result) {
    if (status.connected) continue;
    if (status.running) {
      status.detail = status.client == DetectedClient::kRobloxStudioMcp
                          ? "StudioMCP is running; waiting for Roblox Studio"
                          : status.name + " process detected";
    } else if (status.installed) {
      status.detail = "Installed but not running";
    } else {
      status.detail = "Executable not found";
    }
  }
  return result;
}

}  // namespace runtime
}  // namespace aurora
