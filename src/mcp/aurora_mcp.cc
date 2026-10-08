// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#define JSON_NOEXCEPTION 1

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "runtime/environment.h"
#include "runtime/managed_process.h"
#include "runtime/runtime_paths.h"

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumRequestBytes = 1024 * 1024;
constexpr std::size_t kMaximumToolArguments = 64;
constexpr std::size_t kMaximumArgumentBytes = 4096;
constexpr int kDefaultLogLines = 80;
constexpr int kMaximumLogLines = 200;
constexpr std::size_t kDefaultLogBytes = 64 * 1024;
constexpr std::size_t kMaximumLogBytes = 128 * 1024;

std::filesystem::path ExecutablePath() {
  char buffer[4096] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) return {};
  buffer[length] = '\0';
  return std::filesystem::path(buffer);
}

struct ServerState {
  aurora::runtime::ProcessEnvironment environment;
  aurora::runtime::RuntimePaths paths;
  std::filesystem::path executable;
  std::optional<aurora::runtime::ManagedProcess> player;

  ServerState()
      : paths(aurora::runtime::RuntimePaths::FromEnvironment(environment)),
        executable(ExecutablePath()) {}
};

Json RpcResponse(const Json& id, const Json& result) {
  return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

Json RpcError(const Json& id, int code, const std::string& message) {
  return Json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", message}}}};
}

Json TextContent(const std::string& text) {
  return Json{{"type", "text"}, {"text", text}};
}

Json ToolResult(const Json& value) {
  const std::string text = value.is_string() ? value.get<std::string>()
                                             : value.dump();
  return Json{{"content", Json::array({TextContent(text)})},
              {"isError", false},
              {"structuredContent", value}};
}

Json ToolError(const std::string& message) {
  return Json{{"content", Json::array({TextContent(message)})},
              {"isError", true}};
}

Json LaunchSchema() {
  return Json{{"type", "object"},
              {"properties",
               {{"args",
                 {{"type", "array"},
                  {"items", {{"type", "string"}}},
                  {"maxItems", static_cast<int>(kMaximumToolArguments)}}}}},
              {"additionalProperties", false}};
}

Json StopSchema() {
  return Json{{"type", "object"},
              {"properties", {{"role", {{"type", "string"},
                                            {"enum", {"player"}}}}}},
              {"required", {"role"}},
              {"additionalProperties", false}};
}

Json LogsSchema() {
  return Json{{"type", "object"},
              {"properties",
               {{"lines", {{"type", "integer"}, {"minimum", 1},
                            {"maximum", kMaximumLogLines}}},
                {"max_bytes", {{"type", "integer"}, {"minimum", 1024},
                               {"maximum", kMaximumLogBytes}}}}},
              {"additionalProperties", false}};
}

Json Tools() {
  return Json::array({
      Json{{"name", "aurora_runtime_status"},
           {"description", "Inspect Aurora Player process state."},
           {"inputSchema", Json{{"type", "object"},
                                 {"additionalProperties", false}}}},
      Json{{"name", "aurora_player_launch"},
           {"description", "Launch Aurora Player with safe argument forwarding."},
           {"inputSchema", LaunchSchema()}},
      Json{{"name", "aurora_stop"},
           {"description", "Stop the managed Aurora Player process."},
           {"inputSchema", StopSchema()}},
      Json{{"name", "aurora_logs_tail"},
           {"description", "Read a bounded tail of Aurora's latest session log."},
           {"inputSchema", LogsSchema()}},
  });
}

bool ReadLaunchArguments(const Json& arguments, std::vector<std::string>* output,
                         std::string* error) {
  if (output == nullptr || error == nullptr) return false;
  output->clear();
  error->clear();
  if (!arguments.is_object()) {
    *error = "arguments must be an object";
    return false;
  }
  const auto found = arguments.find("args");
  if (found == arguments.end()) return true;
  if (!found->is_array()) {
    *error = "args must be an array of strings";
    return false;
  }
  if (found->size() > kMaximumToolArguments) {
    *error = "too many launch arguments";
    return false;
  }
  for (const Json& value : *found) {
    if (!value.is_string()) {
      *error = "launch arguments must be strings";
      return false;
    }
    const std::string argument = value.get<std::string>();
    if (argument.size() > kMaximumArgumentBytes ||
        argument.find('\0') != std::string::npos) {
      *error = "launch argument is too long or contains NUL";
      return false;
    }
    output->push_back(argument);
  }
  return true;
}

std::filesystem::path BinaryPath(const ServerState& state,
                                 const char* environment_name,
                                 const char* sibling_name) {
  const std::optional<std::string> override =
      state.environment.Get(environment_name);
  if (override.has_value() && !override->empty()) return *override;
  if (!state.executable.empty()) return state.executable.parent_path() / sibling_name;
  return sibling_name;
}

std::optional<aurora::runtime::ManagedProcess>* ProcessSlot(
    ServerState* state, std::string_view role) {
  if (role == "player") return &state->player;
  return nullptr;
}

Json Launch(ServerState* state, std::string_view role,
            const Json& arguments) {
  std::optional<aurora::runtime::ManagedProcess>* slot =
      ProcessSlot(state, role);
  if (slot == nullptr) return ToolError("role must be player");
  if (slot->has_value() && slot->value().running()) {
    return ToolError(std::string(role) + " is already running");
  }
  slot->reset();

  std::vector<std::string> launch_arguments;
  std::string argument_error;
  if (!ReadLaunchArguments(arguments, &launch_arguments, &argument_error)) {
    return ToolError(argument_error);
  }

  aurora::runtime::ManagedProcessSpec spec;
  spec.executable = BinaryPath(*state, "AURORA_PLAYER_PATH", "aurora_player");
  spec.arguments = std::move(launch_arguments);
  spec.environment.push_back("AURORA_LAUNCHED_BY=aurora-mcp");
  spec.inherit_stdio = false;
  std::string error;
  aurora::runtime::ManagedProcess process =
      aurora::runtime::ManagedProcess::Start(spec, &error);
  if (!process.valid()) {
    return ToolError(error.empty() ? "cannot start Aurora process" : error);
  }
  *slot = std::move(process);
  return ToolResult(Json{{"role", role},
                         {"pid", static_cast<std::int64_t>(slot->value().pid())},
                         {"running", true}});
}

Json ProcessState(std::optional<aurora::runtime::ManagedProcess>* slot,
                  std::string_view role) {
  Json result{{"role", role}, {"running", false}};
  if (slot == nullptr || !slot->has_value()) return result;
  std::string error;
  const std::optional<int> status = slot->value().Poll(&error);
  if (!error.empty()) {
    result["error"] = error;
    return result;
  }
  if (!status.has_value()) {
    result["running"] = true;
    result["pid"] = static_cast<std::int64_t>(slot->value().pid());
    return result;
  }
  if (WIFEXITED(*status)) {
    result["exit_code"] = WEXITSTATUS(*status);
  } else if (WIFSIGNALED(*status)) {
    result["signal"] = WTERMSIG(*status);
  }
  return result;
}

Json RuntimeStatus(ServerState* state) {
  const std::filesystem::path player =
      BinaryPath(*state, "AURORA_PLAYER_PATH", "aurora_player");
  return ToolResult(Json{{"player", ProcessState(&state->player, "player")},
                         {"binary", player.string()},
                         {"latest_log", (state->paths.logs_root() / "latest.log").string()}});
}

Json Stop(ServerState* state, const Json& arguments) {
  if (!arguments.is_object()) return ToolError("arguments must be an object");
  const auto role = arguments.find("role");
  if (role == arguments.end() || !role->is_string()) {
    return ToolError("role must be player");
  }
  std::optional<aurora::runtime::ManagedProcess>* slot =
      ProcessSlot(state, role->get<std::string>());
  if (slot == nullptr) return ToolError("role must be player");
  if (!slot->has_value()) {
    return ToolResult(Json{{"role", role->get<std::string>()}, {"stopped", true}});
  }
  std::string error;
  if (!slot->value().Terminate(std::chrono::milliseconds(1500), &error)) {
    return ToolError(error.empty() ? "cannot stop Aurora process" : error);
  }
  slot->reset();
  return ToolResult(Json{{"role", role->get<std::string>()}, {"stopped", true}});
}

std::optional<std::string> ReadLogTail(const std::filesystem::path& path,
                                       int line_limit, std::size_t max_bytes,
                                       std::string* error) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    if (error != nullptr) *error = "latest Aurora log is not available";
    return std::nullopt;
  }
  const std::streamoff size = input.tellg();
  if (size < 0) {
    if (error != nullptr) *error = "cannot seek latest Aurora log";
    return std::nullopt;
  }
  const std::streamoff start =
      size > static_cast<std::streamoff>(max_bytes)
          ? size - static_cast<std::streamoff>(max_bytes)
          : 0;
  input.seekg(start);
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  if (start != 0) {
    const std::size_t first_line = content.find('\n');
    if (first_line == std::string::npos) {
      content.clear();
    } else {
      content.erase(0, first_line + 1);
    }
  }
  std::size_t keep_from = 0;
  std::size_t search_end = content.size();
  for (int line = 0; line < line_limit; ++line) {
    if (search_end == 0) break;
    const std::size_t newline = content.rfind('\n', search_end - 1);
    if (newline == std::string::npos) {
      keep_from = 0;
      break;
    }
    keep_from = newline + 1;
    search_end = newline;
  }
  if (keep_from > 0) content.erase(0, keep_from);
  return content;
}

Json LogsTail(ServerState* state, const Json& arguments) {
  if (!arguments.is_object()) return ToolError("arguments must be an object");
  int lines = kDefaultLogLines;
  std::size_t max_bytes = kDefaultLogBytes;
  const auto lines_value = arguments.find("lines");
  if (lines_value != arguments.end()) {
    if (!lines_value->is_number_integer()) return ToolError("lines must be an integer");
    lines = lines_value->get<int>();
  }
  const auto bytes_value = arguments.find("max_bytes");
  if (bytes_value != arguments.end()) {
    if (!bytes_value->is_number_integer()) {
      return ToolError("max_bytes must be an integer");
    }
    const std::int64_t requested = bytes_value->get<std::int64_t>();
    if (requested < 1024) return ToolError("max_bytes is too small");
    max_bytes = static_cast<std::size_t>(requested);
  }
  if (lines < 1 || lines > kMaximumLogLines) {
    return ToolError("lines must be between 1 and 200");
  }
  max_bytes = std::min(max_bytes, kMaximumLogBytes);
  std::string error;
  const std::optional<std::string> content = ReadLogTail(
      state->paths.logs_root() / "latest.log", lines, max_bytes, &error);
  if (!content.has_value()) return ToolError(error);
  return ToolResult(Json{{"path", (state->paths.logs_root() / "latest.log").string()},
                         {"lines", lines},
                         {"text", *content}});
}

Json ToolCall(ServerState* state, const std::string& name,
              const Json& arguments) {
  if (name == "aurora_runtime_status") return RuntimeStatus(state);
  if (name == "aurora_player_launch") return Launch(state, "player", arguments);
  if (name == "aurora_stop") return Stop(state, arguments);
  if (name == "aurora_logs_tail") return LogsTail(state, arguments);
  return ToolError("unknown Aurora tool: " + name);
}

std::optional<Json> HandleRequest(const Json& request, ServerState* state) {
  if (!request.is_object()) return RpcError(nullptr, -32600, "request must be an object");
  const auto version = request.find("jsonrpc");
  const auto method_value = request.find("method");
  if (version == request.end() || !version->is_string() ||
      version->get<std::string>() != "2.0" || method_value == request.end() ||
      !method_value->is_string()) {
    const bool has_id = request.find("id") != request.end();
    if (!has_id) return std::nullopt;
    return RpcError(request.at("id"), -32600, "invalid JSON-RPC request");
  }
  const bool has_id = request.find("id") != request.end();
  const Json id = has_id ? request.at("id") : Json(nullptr);
  const std::string method = method_value->get<std::string>();
  auto response = [&has_id, &id](const Json& result) -> std::optional<Json> {
    return has_id ? std::optional<Json>(RpcResponse(id, result))
                  : std::nullopt;
  };
  auto error = [&has_id, &id](int code,
                              const std::string& message) -> std::optional<Json> {
    return has_id ? std::optional<Json>(RpcError(id, code, message))
                  : std::nullopt;
  };

  if (method == "notifications/initialized" || method == "notifications/cancelled") {
    return std::nullopt;
  }
  if (method == "initialize") {
    return response(Json{{"protocolVersion", "2024-11-05"},
                         {"capabilities", {{"tools", {{"listChanged", false}}}}},
                         {"serverInfo", {{"name", "aurora-mcp"},
                                          {"version", "1.0.3"}}}});
  }
  if (method == "ping") return response(Json::object());
  if (method == "tools/list") return response(Json{{"tools", Tools()}});
  if (method != "tools/call") return error(-32601, "method not found");

  const auto params = request.find("params");
  if (params == request.end() || !params->is_object()) {
    return error(-32602, "tools/call params must be an object");
  }
  const auto name = params->find("name");
  if (name == params->end() || !name->is_string()) {
    return error(-32602, "tools/call requires a tool name");
  }
  const auto arguments = params->find("arguments");
  const Json tool_arguments =
      arguments == params->end() ? Json::object() : *arguments;
  return response(ToolCall(state, name->get<std::string>(), tool_arguments));
}

}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);
  ServerState state;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() > kMaximumRequestBytes) {
      std::cout << RpcError(nullptr, -32600, "request is too large").dump()
                << '\n'
                << std::flush;
      continue;
    }
    const Json request = Json::parse(line, nullptr, false, true);
    if (request.is_discarded()) {
      std::cout << RpcError(nullptr, -32700, "parse error").dump() << '\n'
                << std::flush;
      continue;
    }
    const std::optional<Json> response = HandleRequest(request, &state);
    if (response.has_value()) {
      std::cout << response->dump() << '\n' << std::flush;
    }
  }
  return 0;
}
