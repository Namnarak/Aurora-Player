#include "runtime/platform_cache_migration.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "runtime/environment.h"
#include "runtime/runtime_paths.h"

namespace aurora {
namespace runtime {
namespace {

class MapEnvironment final : public Environment {
 public:
  explicit MapEnvironment(
      std::unordered_map<std::string, std::string> values = {})
      : values_(std::move(values)) {}

  std::optional<std::string> Get(std::string_view name) const override {
    const auto found = values_.find(std::string(name));
    return found == values_.end() ? std::nullopt : std::optional(found->second);
  }

 private:
  std::unordered_map<std::string, std::string> values_;
};

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    char pattern[] = "/tmp/aurora_platform_cache_XXXXXX";
    char* created = mkdtemp(pattern);
    if (created != nullptr) {
      path_ = created;
    }
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

bool WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
  std::error_code error;
  if (!RuntimePaths::EnsureDirectory(path.parent_path(), &error)) {
    return false;
  }
  std::ofstream output(path);
  output << value.dump();
  return output.good();
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input, nullptr, false, true);
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

mode_t FileMode(const std::filesystem::path& path) {
  struct stat status{};
  return stat(path.c_str(), &status) == 0 ? status.st_mode & 0777 : 0;
}

struct FixturePaths {
  MapEnvironment environment;
  RuntimePaths paths;
  std::filesystem::path app_storage;
};

FixturePaths PathsFor(const TemporaryDirectory& temporary) {
  const std::filesystem::path runtime_root = temporary.path() / "android";
  MapEnvironment environment({
      {"HOME", temporary.path().string()},
      {"AURORA_RUNTIME_ROOT", runtime_root.string()},
  });
  RuntimePaths paths =
      RuntimePaths::FromEnvironment(environment, temporary.path());
  return {std::move(environment), std::move(paths),
          runtime_root / "data/files/appData/LocalStorage/appStorage.json"};
}

const std::string& DesktopProfile() {
  static const std::string profile =
      BuildPlatformProfileRevision("pc-windows-11", false, true, true);
  return profile;
}

const std::string& MobileProfile() {
  static const std::string profile =
      BuildPlatformProfileRevision("mobile-pixel-7", true, false, false);
  return profile;
}

TEST(PlatformCacheMigrationTest, FingerprintIncludesDeviceAndInputProfile) {
  EXPECT_EQ(DesktopProfile(), "device-v1-pc-windows-11-t0-m1-k1");
  EXPECT_EQ(MobileProfile(), "device-v1-mobile-pixel-7-t1-m0-k0");
  EXPECT_EQ(BuildPlatformProfileRevision("console-ps5", false, true, true),
            "device-v1-console-ps5-t0-m1-k1");
}

TEST(PlatformCacheMigrationTest,
     InvalidatesOnlyRefreshablePlatformCachesOnFirstTransition) {
  TemporaryDirectory temporary;
  ASSERT_FALSE(temporary.path().empty());
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json original = {
      {"PlayerHydrationBlob", "signed-blob"},
      {"PlayerHydrationSignature", "signature"},
      {"AppConfiguration", "cached-policy"},
      {"AppInstallationId", "stable-installation"},
      {"_Patch__UniversalAppPatch", "ota-state"},
      {"UnrelatedPreference", "preserve-me"},
  };
  ASSERT_TRUE(WriteJson(fixture.app_storage, original));

  const PlatformCacheMigrationResult result = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  ASSERT_TRUE(result) << result.error;
  EXPECT_TRUE(result.transitioned);
  EXPECT_TRUE(result.app_storage_found);
  EXPECT_TRUE(result.app_storage_updated);

  const nlohmann::json migrated = ReadJson(fixture.app_storage);
  EXPECT_FALSE(migrated.contains("PlayerHydrationBlob"));
  EXPECT_FALSE(migrated.contains("PlayerHydrationSignature"));
  EXPECT_FALSE(migrated.contains("AppConfiguration"));
  EXPECT_EQ(migrated["AppInstallationId"], "stable-installation");
  EXPECT_EQ(migrated["_Patch__UniversalAppPatch"], "ota-state");
  EXPECT_EQ(migrated["UnrelatedPreference"], "preserve-me");
  EXPECT_EQ(FileMode(fixture.app_storage), 0600);
  EXPECT_EQ(FileMode(result.fingerprint_file), 0600);
  EXPECT_EQ(ReadJson(result.fingerprint_file)["profile_revision"],
            DesktopProfile());
}

TEST(PlatformCacheMigrationTest, SameRevisionDoesNotTouchRehydratedCache) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  PlatformCacheMigrationResult first = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  ASSERT_TRUE(first) << first.error;
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"PlayerHydrationBlob", "new-valid-blob"},
                         {"PlayerHydrationSignature", "new-signature"},
                         {"AppConfiguration", "new-desktop-policy"}}));

  const PlatformCacheMigrationResult second = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  ASSERT_TRUE(second) << second.error;
  EXPECT_FALSE(second.transitioned);
  const nlohmann::json current = ReadJson(fixture.app_storage);
  EXPECT_EQ(current["PlayerHydrationBlob"], "new-valid-blob");
  EXPECT_EQ(current["PlayerHydrationSignature"], "new-signature");
  EXPECT_EQ(current["AppConfiguration"], "new-desktop-policy");
}

TEST(PlatformCacheMigrationTest, NewRevisionInvalidatesCacheAgain) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  PlatformCacheMigrationResult first = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, MobileProfile());
  ASSERT_TRUE(first) << first.error;
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"PlayerHydrationBlob", "tablet-blob"},
                         {"PlayerHydrationSignature", "tablet-signature"},
                         {"AppConfiguration", "tablet-policy"},
                         {"Keep", true}}));

  const PlatformCacheMigrationResult second = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  ASSERT_TRUE(second) << second.error;
  EXPECT_TRUE(second.transitioned);
  EXPECT_EQ(second.previous_revision, MobileProfile());
  const nlohmann::json migrated = ReadJson(fixture.app_storage);
  EXPECT_EQ(migrated.size(), 1U);
  EXPECT_EQ(migrated["Keep"], true);
}

TEST(PlatformCacheMigrationTest, MissingAppStorageStillCommitsFingerprint) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const PlatformCacheMigrationResult result = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  ASSERT_TRUE(result) << result.error;
  EXPECT_TRUE(result.transitioned);
  EXPECT_FALSE(result.app_storage_found);
  EXPECT_FALSE(result.app_storage_updated);
  EXPECT_FALSE(std::filesystem::exists(fixture.app_storage));
  EXPECT_EQ(ReadJson(result.fingerprint_file)["profile_revision"],
            DesktopProfile());
}

TEST(PlatformCacheMigrationTest, RejectsSymlinkedAppStorage) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const std::filesystem::path outside = temporary.path() / "outside.json";
  ASSERT_TRUE(WriteJson(outside, {{"AppConfiguration", "do-not-touch"}}));
  ASSERT_TRUE(RuntimePaths::EnsureDirectory(fixture.app_storage.parent_path()));
  ASSERT_EQ(symlink(outside.c_str(), fixture.app_storage.c_str()), 0);

  const PlatformCacheMigrationResult result = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());
  EXPECT_FALSE(result);
  EXPECT_EQ(ReadJson(outside)["AppConfiguration"], "do-not-touch");
  EXPECT_FALSE(std::filesystem::exists(result.fingerprint_file));
}

TEST(PlatformCacheMigrationTest,
     RejectsHardlinkedMigrationLockWithoutMutatingProtectedCookie) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const std::filesystem::path data_root = temporary.path() / "android/data";
  ASSERT_TRUE(RuntimePaths::EnsureDirectory(data_root));
  const std::filesystem::path protected_cookie =
      temporary.path() / "sober-cookie";
  const std::string cookie_contents = ".ROBLOSECURITY=protected";
  std::ofstream(protected_cookie) << cookie_contents;
  ASSERT_EQ(chmod(protected_cookie.c_str(), S_IRWXU), 0);
  const std::filesystem::path lock_path =
      data_root / ".aurora-platform-profile.lock";
  std::error_code filesystem_error;
  std::filesystem::create_hard_link(protected_cookie, lock_path,
                                    filesystem_error);
  ASSERT_FALSE(filesystem_error);

  const PlatformCacheMigrationResult result = MigratePlatformProfileCaches(
      fixture.environment, fixture.paths, DesktopProfile());

  EXPECT_FALSE(result);
  EXPECT_EQ(result.error,
            "another process owns the platform cache migration lock");
  EXPECT_EQ(ReadFile(protected_cookie), cookie_contents);
  EXPECT_EQ(ReadFile(lock_path), cookie_contents);
  EXPECT_EQ(FileMode(protected_cookie), 0700);
  EXPECT_FALSE(std::filesystem::exists(result.fingerprint_file));
}

TEST(PlatformCacheMigrationTest, MapsAndroidVirtualFilesDirectoryToXdgRoot) {
  TemporaryDirectory temporary;
  const std::filesystem::path runtime_root = temporary.path() / "android";
  const MapEnvironment environment({
      {"HOME", temporary.path().string()},
      {"AURORA_RUNTIME_ROOT", runtime_root.string()},
      {"AURORA_ANDROID_FILES_DIR", "/data/user/0/com.roblox.client/files"},
  });
  const RuntimePaths paths =
      RuntimePaths::FromEnvironment(environment, temporary.path());
  const PlatformCacheMigrationResult result =
      MigratePlatformProfileCaches(environment, paths, DesktopProfile());
  ASSERT_TRUE(result) << result.error;
  EXPECT_EQ(result.app_storage_file,
            runtime_root / "data/files/appData/LocalStorage/appStorage.json");
}

TEST(PlatformCacheMigrationTest,
     ThemeOverridePreservesUnrelatedAndOtherUsersCache) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json device_themes = {
      {"42", "dark"},
      {"99", "dark"},
  };
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"AuthenticatedTheme", "dark"},
                         {"DeviceLevelTheme", device_themes.dump()},
                         {"DeviceLevelThemeSnapshotTimestamp", "1234"},
                         {"UnrelatedPreference", "keep"}}));

  std::string error;
  ASSERT_TRUE(
      ApplyRobloxThemeCacheOverride(fixture.app_storage, 42, "light", false,
                                    &error))
      << error;

  const nlohmann::json storage = ReadJson(fixture.app_storage);
  EXPECT_EQ(storage["AuthenticatedTheme"], "light");
  const nlohmann::json updated_device_themes = nlohmann::json::parse(
      storage["DeviceLevelTheme"].get_ref<const std::string&>());
  EXPECT_EQ(updated_device_themes["42"], "light");
  EXPECT_EQ(updated_device_themes["99"], "dark");
  EXPECT_EQ(storage["DeviceLevelThemeSnapshotTimestamp"], "1234");
  EXPECT_EQ(storage["UnrelatedPreference"], "keep");
}

TEST(PlatformCacheMigrationTest,
     ReadsAccountThemeWithoutChangingRobloxStorage) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json device_themes = {
      {"42", "light"},
      {"99", "dark"},
  };
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"AuthenticatedTheme", "dark"},
                         {"DeviceLevelTheme", device_themes.dump()},
                         {"UnrelatedPreference", "keep"}}));
  const std::string original = ReadFile(fixture.app_storage);

  const RobloxThemeCacheResult account =
      ReadRobloxThemeCache(fixture.app_storage, 42);
  ASSERT_TRUE(account) << account.error;
  ASSERT_TRUE(account.dark_theme.has_value());
  EXPECT_FALSE(*account.dark_theme);
  EXPECT_EQ(account.selection, std::optional<std::string>("Light"));

  const RobloxThemeCacheResult fallback =
      ReadRobloxThemeCache(fixture.app_storage, 7);
  ASSERT_TRUE(fallback) << fallback.error;
  ASSERT_TRUE(fallback.dark_theme.has_value());
  EXPECT_TRUE(*fallback.dark_theme);
  EXPECT_EQ(fallback.selection, std::optional<std::string>("Dark"));
  EXPECT_EQ(ReadFile(fixture.app_storage), original);
}

TEST(PlatformCacheMigrationTest,
     ReadsRobloxSubscriptionWithoutChangingRobloxStorage) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"HasRobloxSubscription", "true"},
                         {"UnrelatedPreference", "keep"}}));
  const std::string original = ReadFile(fixture.app_storage);

  const RobloxSubscriptionCacheResult result =
      ReadRobloxSubscriptionCache(fixture.app_storage);
  ASSERT_TRUE(result) << result.error;
  EXPECT_EQ(result.has_subscription, std::optional<bool>(true));
  EXPECT_EQ(ReadFile(fixture.app_storage), original);
}

TEST(PlatformCacheMigrationTest,
     ReadsFalseRobloxSubscriptionAndBooleanEncoding) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  for (const nlohmann::json& entitlement :
       {nlohmann::json("false"), nlohmann::json(false)}) {
    SCOPED_TRACE(entitlement.dump());
    ASSERT_TRUE(WriteJson(fixture.app_storage,
                          {{"HasRobloxSubscription", entitlement}}));

    const RobloxSubscriptionCacheResult result =
        ReadRobloxSubscriptionCache(fixture.app_storage);
    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.has_subscription, std::optional<bool>(false));
  }
}

TEST(PlatformCacheMigrationTest, MissingOrUnknownRobloxSubscriptionIsUnset) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json unknown_values[] = {
      nlohmann::json::object(),
      nlohmann::json{{"HasRobloxSubscription", "unknown"}},
      nlohmann::json{{"HasRobloxSubscription", nullptr}},
      nlohmann::json{{"HasRobloxSubscription", 1}}};
  for (const nlohmann::json& storage : unknown_values) {
    SCOPED_TRACE(storage.dump());
    ASSERT_TRUE(WriteJson(fixture.app_storage, storage));

    const RobloxSubscriptionCacheResult result =
        ReadRobloxSubscriptionCache(fixture.app_storage);
    ASSERT_TRUE(result) << result.error;
    EXPECT_FALSE(result.has_subscription.has_value());
  }
}

TEST(PlatformCacheMigrationTest, ReadsRobloxMembershipTypeWithoutWritingCache) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"Membership", "4"},
                         {"HasRobloxSubscription", "true"}}));
  const std::string original = ReadFile(fixture.app_storage);

  const RobloxMembershipCacheResult result =
      ReadRobloxMembershipCache(fixture.app_storage);
  ASSERT_TRUE(result) << result.error;
  EXPECT_EQ(result.membership_type, std::optional<int>(4));
  EXPECT_EQ(ReadFile(fixture.app_storage), original);
}

TEST(PlatformCacheMigrationTest, RejectsInvalidRobloxMembershipType) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json invalid_values[] = {
      {{"Membership", "4tail"}}, {{"Membership", "-1"}},
      {{"Membership", ""}},     {{"Membership", nullptr}},
      {{"Membership", 4.5}},     {{"Membership", 1ULL << 40}}};
  for (const nlohmann::json& storage : invalid_values) {
    SCOPED_TRACE(storage.dump());
    ASSERT_TRUE(WriteJson(fixture.app_storage, storage));

    const RobloxMembershipCacheResult result =
        ReadRobloxMembershipCache(fixture.app_storage);
    ASSERT_TRUE(result) << result.error;
    EXPECT_FALSE(result.membership_type.has_value());
  }
}

TEST(PlatformCacheMigrationTest, PreservesSystemThemeSelection) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"AuthenticatedTheme", "dark"},
                         {"DeviceLevelTheme",
                          nlohmann::json{{"42", "systemDark"}}.dump()}}));

  const RobloxThemeCacheResult result =
      ReadRobloxThemeCache(fixture.app_storage, 42);
  ASSERT_TRUE(result) << result.error;
  EXPECT_EQ(result.dark_theme, std::optional<bool>(true));
  EXPECT_EQ(result.selection, std::optional<std::string>("System"));
}

TEST(PlatformCacheMigrationTest, SystemOverrideStoresResolvedColorAndMode) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  ASSERT_TRUE(WriteJson(fixture.app_storage,
                        {{"AuthenticatedTheme", "light"},
                         {"DeviceLevelTheme",
                          nlohmann::json{{"42", "light"},
                                         {"99", "dark"}}
                              .dump()},
                         {"UnrelatedPreference", "keep"}}));

  std::string error;
  ASSERT_TRUE(ApplyRobloxThemeCacheOverride(fixture.app_storage, 42, "system",
                                           true, &error))
      << error;

  const nlohmann::json storage = ReadJson(fixture.app_storage);
  EXPECT_EQ(storage["AuthenticatedTheme"], "dark");
  const nlohmann::json themes = nlohmann::json::parse(
      storage["DeviceLevelTheme"].get_ref<const std::string&>());
  EXPECT_EQ(themes["42"], "systemDark");
  EXPECT_EQ(themes["99"], "dark");
  EXPECT_EQ(storage["UnrelatedPreference"], "keep");
}

TEST(PlatformCacheMigrationTest, MissingRobloxThemeUsesCallerDefault) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);

  const RobloxThemeCacheResult result =
      ReadRobloxThemeCache(fixture.app_storage, -1);
  ASSERT_TRUE(result) << result.error;
  EXPECT_FALSE(result.dark_theme.has_value());
  EXPECT_FALSE(std::filesystem::exists(fixture.app_storage));
}

TEST(PlatformCacheMigrationTest, UnknownAccountThemeUsesAuthenticatedTheme) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json invalid_themes[] = {
      "system", "", nullptr, false, 42, nlohmann::json::object()};
  for (const auto& theme : invalid_themes) {
    SCOPED_TRACE(theme.dump());
    const nlohmann::json device_themes = {{"42", theme}, {"99", "dark"}};
    ASSERT_TRUE(WriteJson(fixture.app_storage,
                         {{"DeviceLevelTheme", device_themes.dump()},
                          {"AuthenticatedTheme", "light"},
                          {"UnrelatedPreference", "keep"}}));
    const std::string original = ReadFile(fixture.app_storage);

    const auto result = ReadRobloxThemeCache(fixture.app_storage, 42);
    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.dark_theme, std::optional<bool>(false));
    EXPECT_EQ(result.selection, std::optional<std::string>("Light"));
    EXPECT_EQ(ReadFile(fixture.app_storage), original);
  }
}

TEST(PlatformCacheMigrationTest, MalformedDeviceThemesUseAuthenticatedTheme) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json invalid_caches[] = {
      "{broken", "[]", "null", 42, nullptr, nlohmann::json::object()};
  for (const auto& cache : invalid_caches) {
    SCOPED_TRACE(cache.dump());
    ASSERT_TRUE(WriteJson(fixture.app_storage,
                         {{"DeviceLevelTheme", cache},
                          {"AuthenticatedTheme", "dark"}}));
    const std::string original = ReadFile(fixture.app_storage);

    const auto result = ReadRobloxThemeCache(fixture.app_storage, 42);
    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.dark_theme, std::optional<bool>(true));
    EXPECT_EQ(result.selection, std::optional<std::string>("Dark"));
    EXPECT_EQ(ReadFile(fixture.app_storage), original);
  }
}

TEST(PlatformCacheMigrationTest, UnknownThemesUseCallerDefault) {
  TemporaryDirectory temporary;
  FixturePaths fixture = PathsFor(temporary);
  const nlohmann::json device_themes = {{"42", "system"}};
  const nlohmann::json invalid_themes[] = {
      "future-theme", "", nullptr, true, 42, nlohmann::json::array()};
  for (const auto& theme : invalid_themes) {
    SCOPED_TRACE(theme.dump());
    ASSERT_TRUE(WriteJson(fixture.app_storage,
                         {{"DeviceLevelTheme", device_themes.dump()},
                          {"AuthenticatedTheme", theme}}));
    const std::string original = ReadFile(fixture.app_storage);

    const auto result = ReadRobloxThemeCache(fixture.app_storage, 42);
    ASSERT_TRUE(result) << result.error;
    EXPECT_FALSE(result.dark_theme.has_value());
    EXPECT_EQ(ReadFile(fixture.app_storage), original);
  }
}

}  // namespace
}  // namespace runtime
}  // namespace aurora
