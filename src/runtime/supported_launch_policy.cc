#include "runtime/supported_launch_policy.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace aurora {
namespace runtime {
namespace {

bool SetDefault(const char* name, const std::string& value,
                std::string* error) {
  const char* current = std::getenv(name);
  if (current != nullptr && current[0] != '\0') {
    return true;
  }
  if (setenv(name, value.c_str(), 1) == 0) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string("cannot publish supported launch default: ") + name;
  }
  return false;
}

std::filesystem::path CurrentExecutable() {
  std::array<char, 4097> path{};
  const ssize_t length =
      readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (length <= 0 || static_cast<std::size_t>(length) >= path.size()) {
    return {};
  }
  path[static_cast<std::size_t>(length)] = '\0';
  return path.data();
}

bool ApplyInstalledResourceDefaults(std::string* error) {
  const std::filesystem::path executable = CurrentExecutable();
  if (executable.empty()) {
    return true;
  }
  const std::filesystem::path runtime_directory = executable.parent_path();
  std::filesystem::path prefix;
  std::filesystem::path helper;
  std::filesystem::path progress_helper;
  std::filesystem::path update_helper;
  if (runtime_directory.filename() == "aurora" &&
      runtime_directory.parent_path().filename() == "lib") {
    prefix = runtime_directory.parent_path().parent_path();
    helper = runtime_directory / "aurora_webview_helper";
    progress_helper = runtime_directory / "aurora_failure_dialog";
    update_helper = runtime_directory / "aurora_updater";
  } else if (runtime_directory.filename() == "bin") {
    prefix = runtime_directory.parent_path();
    helper = prefix / "lib/aurora/aurora_webview_helper";
    progress_helper = prefix / "lib/aurora/aurora_failure_dialog";
    update_helper = prefix / "lib/aurora/aurora_updater";
  } else {
    return true;
  }
  const std::filesystem::path data_directory = prefix / "share/aurora";
  const std::filesystem::path metadata_directory = data_directory / "metadata";
  std::error_code filesystem_error;
  if (!std::filesystem::is_directory(metadata_directory, filesystem_error)) {
    return true;
  }

  const std::vector<std::pair<const char*, std::filesystem::path>> defaults = {
      {"AURORA_PROJECT_ROOT", data_directory},
      {"AURORA_COMPATIBILITY_MANIFEST",
       metadata_directory / "roblox_compatibility.json"},
      {"AURORA_UPDATE_COMPATIBILITY_PATH",
       metadata_directory / "roblox_compatibility.json"},
      {"AURORA_UPDATE_SIGNING_TRUST_PATH",
       metadata_directory / "roblox_signing_certificates.json"},
      {"AURORA_BOOTSTRAP_SOURCES_PATH",
       metadata_directory / "roblox_bootstrap_sources.json"},
      {"AURORA_UPDATE_CANARY_BIN", executable},
      {"AURORA_UPDATE_HELPER", update_helper},
      {"AURORA_RUNTIME_LIBRARY_DIR", helper.parent_path()},
      {"AURORA_WEBVIEW_HELPER", helper},
      {"AURORA_UPDATE_PROGRESS_HELPER", progress_helper},
  };
  for (const auto& [name, value] : defaults) {
    if (!SetDefault(name, value.string(), error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ApplySupportedLaunchPolicy(bool interactive, std::string* error) {
  // A packaging launcher may reload its environment for each native child.
  // Keep the bundled default separate from the updater's per-candidate
  // manifest, which must survive both normal launches and isolated canaries.
  const char* packaged_manifest =
      std::getenv("AURORA_PACKAGED_COMPATIBILITY_MANIFEST");
  if (packaged_manifest != nullptr && packaged_manifest[0] != '\0' &&
      !SetDefault("AURORA_COMPATIBILITY_MANIFEST", packaged_manifest, error)) {
    return false;
  }
  if (!ApplyInstalledResourceDefaults(error)) {
    return false;
  }
  if (!interactive) {
    return true;
  }

  const std::vector<std::pair<const char*, std::string>> defaults = {
      {"AURORA_ALLOW_NO_COOKIE_LUA_APP", "1"},
      {"AURORA_VALIDATE_ROBLOX_COOKIE", "0"},
      {"AURORA_ENGINE_DETACH", "0"},
      {"AURORA_CALL_REAL_APP_BRIDGE_INIT", "1"},
      {"AURORA_CALL_REAL_APP_BRIDGE_INIT_THREAD", "0"},
      {"AURORA_SKIP_LIBROBLOX_CTORS", "0"},
      {"AURORA_HOST_JNI_SINGLETON_SEED", "0"},
      {"AURORA_INIT_CLIENT_SETTINGS", "1"},
      {"AURORA_HEADLESS", "0"},
      {"AURORA_STARTUP_THREAD_TIMEOUT_MS", "120000"},
      {"AURORA_CALL_REAL_APP_BRIDGE_START", "1"},
      {"AURORA_STEP_START_APP_WITH_PARAMS", "1"},
      {"AURORA_UPDATE_SURFACE_APP_AFTER_START_APP", "1"},
      {"AURORA_CALL_REAL_APP_BRIDGE_UPDATE_SURFACE", "1"},
      {"AURORA_START_LUA_APP_DM", "1"},
      {"AURORA_STEP_START_LUA_APP_DM", "1"},
      {"AURORA_POST_CLIENT_SETTINGS", "1"},
      {"AURORA_STEP_POST_CLIENT_SETTINGS", "1"},
      {"AURORA_FETCH_CLIENT_SETTINGS", "1"},
      {"AURORA_START_LUA_APP_DM_AFTER_START_APP", "0"},
      {"AURORA_START_LUA_APP_DM_INLINE", "1"},
      {"AURORA_START_LUA_APP_DM_DELAY_MS", "0"},
      {"AURORA_START_GAME_WITH_PARAM", "0"},
      {"AURORA_RESUME_GAME_WITH_PLATFORM_PARAMS_AFTER_START_GAME", "0"},
      {"AURORA_SEND_APP_READY", "0"},
      {"AURORA_SEND_GAME_LOADED", "0"},
      {"AURORA_MAIN_THREAD_MESSAGE_PUMP", "1"},
      {"AURORA_AUTO_EXIT_AFTER_PRESENT_MS", "0"},
  };
  for (const auto& [name, value] : defaults) {
    if (!SetDefault(name, value, error)) {
      return false;
    }
  }
  return true;
}

}  // namespace runtime
}  // namespace aurora
