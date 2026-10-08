#ifndef AURORA_RUNTIME_PLATFORM_CACHE_MIGRATION_H_
#define AURORA_RUNTIME_PLATFORM_CACHE_MIGRATION_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aurora {
namespace runtime {

class Environment;
class RuntimePaths;

// Every coordinate affects Roblox's server-refreshed platform policy. Keeping
// the canonical preset and input capabilities in one cache fingerprint
// prevents one emulated device from reusing another device's hydration data.
std::string BuildPlatformProfileRevision(std::string_view device_profile,
                                         bool touch_enabled, bool mouse_enabled,
                                         bool keyboard_enabled);

struct PlatformCacheMigrationResult {
  bool transitioned = false;
  bool app_storage_found = false;
  bool app_storage_updated = false;
  std::filesystem::path app_storage_file;
  std::filesystem::path fingerprint_file;
  std::string previous_revision;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Invalidates only platform-derived, server-refreshable appStorage entries
// when the platform profile revision changes. Installation identity,
// OTA state, credentials, and unrelated preferences remain untouched.
PlatformCacheMigrationResult MigratePlatformProfileCaches(
    const Environment& environment, const RuntimePaths& paths,
    std::string_view desired_revision);

struct RobloxThemeCacheResult {
  std::optional<bool> dark_theme;
  // Roblox's selected app theme: "Dark", "Light", or "System". The system
  // selection remains distinct from its currently resolved color scheme.
  std::optional<std::string> selection;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Reads the current account's Roblox theme selection without changing
// appStorage. System is returned separately from the resolved color scheme.
RobloxThemeCacheResult ReadRobloxThemeCache(
    const std::filesystem::path& app_storage_file,
    std::int64_t authenticated_user_id);

struct RobloxSubscriptionCacheResult {
  std::optional<bool> has_subscription;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Reads the subscription entitlement cached by Roblox without modifying
// appStorage. Roblox currently serializes this value as the string "true" or
// "false"; JSON booleans are accepted for forward compatibility.
RobloxSubscriptionCacheResult ReadRobloxSubscriptionCache(
    const std::filesystem::path& app_storage_file);

struct RobloxMembershipCacheResult {
  std::optional<int> membership_type;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Reads Roblox's cached numeric Membership value without modifying appStorage.
RobloxMembershipCacheResult ReadRobloxMembershipCache(
    const std::filesystem::path& app_storage_file);

// Synchronizes Roblox's local theme keys with an explicit app theme. The
// resolved dark flag is stored in AuthenticatedTheme; System is preserved in
// DeviceLevelTheme as systemDark/systemLight.
bool ApplyRobloxThemeCacheOverride(
    const std::filesystem::path& app_storage_file,
    std::int64_t authenticated_user_id, std::string_view selection,
    bool dark_theme, std::string* error);

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_PLATFORM_CACHE_MIGRATION_H_
