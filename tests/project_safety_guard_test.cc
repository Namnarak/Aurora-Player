// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/project_safety_guard.h"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace aurora::runtime {
namespace {

class MapEnvironment final : public Environment {
 public:
  explicit MapEnvironment(std::map<std::string, std::string> values = {})
      : values_(std::move(values)) {}

  std::optional<std::string> Get(std::string_view name) const override {
    const auto found = values_.find(std::string(name));
    if (found == values_.end()) return std::nullopt;
    return found->second;
  }

 private:
  std::map<std::string, std::string> values_;
};

TEST(ProjectSafetyGuardTest, NormalManagedLaunchIsAllowed) {
  CommandLineOptions options;
  const MapEnvironment environment;
  EXPECT_TRUE(ValidateProjectSafetyGuard(options, environment).ok());
}

TEST(ProjectSafetyGuardTest, BlocksUnverifiedBuildInProduction) {
  CommandLineOptions options;
  options.allow_unverified_build = true;
  const MapEnvironment environment;
  const Status status = ValidateProjectSafetyGuard(options, environment);
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("--allow-unverified-build"),
            std::string::npos);
}

TEST(ProjectSafetyGuardTest, BlocksExplicitExternalRobloxLibraryInProduction) {
  CommandLineOptions options;
  options.roblox_library_path = "/tmp/libroblox.so";
  const MapEnvironment environment;
  const Status status = ValidateProjectSafetyGuard(options, environment);
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("--roblox-lib"), std::string::npos);
}

TEST(ProjectSafetyGuardTest, BlocksEnvironmentPayloadOverrideInProduction) {
  CommandLineOptions options;
  const MapEnvironment environment({{"ROBLOX_LIB_PATH", "/tmp/libroblox.so"}});
  const Status status = ValidateProjectSafetyGuard(options, environment);
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("ROBLOX_LIB_PATH"), std::string::npos);
}

TEST(ProjectSafetyGuardTest, IsolatedCanaryKeepsUpdaterProbationWorking) {
  CommandLineOptions options;
  options.allow_unverified_build = true;
  const MapEnvironment environment({
      {"AURORA_ISOLATED_CANARY", "1"},
      {"AURORA_HOST_ABI_PROFILE_FILE", "/tmp/profile.json"},
  });
  EXPECT_TRUE(ValidateProjectSafetyGuard(options, environment).ok());
}

TEST(ProjectSafetyGuardTest, ExplicitDeveloperModeAllowsDevelopmentOverrides) {
  CommandLineOptions options;
  options.allow_unverified_build = true;
  options.roblox_library_path = "/tmp/libroblox.so";
  const MapEnvironment environment({{"AURORA_DEVELOPER_MODE", "1"}});
  EXPECT_TRUE(ValidateProjectSafetyGuard(options, environment).ok());
  EXPECT_TRUE(ProjectSafetyDeveloperMode(environment));
}

}  // namespace
}  // namespace aurora::runtime
