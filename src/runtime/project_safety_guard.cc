// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/project_safety_guard.h"

#include <array>
#include <string>
#include <string_view>

namespace aurora::runtime {
namespace {

bool Enabled(const Environment& environment, std::string_view name) {
  const auto value = environment.Get(name);
  return value.has_value() && !value->empty() && *value != "0" &&
         *value != "false" && *value != "off";
}

constexpr std::array<std::string_view, 4> kExternalPayloadOverrides = {
    "ROBLOX_LIB_PATH",
    "AURORA_ALLOW_CANDIDATE_HOST_ABI",
    "AURORA_HOST_ABI_PROFILE_FILE",
    "AURORA_HOST_ABI_APPROVAL_RECEIPT",
};

Status Blocked(std::string detail) {
  return Status::Error(
      StatusCode::kFailedPrecondition,
      "Aurora Project Safety Guard blocked an unsupported production path: " +
          std::move(detail) +
          ". Use the verified managed Roblox payload. Developer experiments "
          "must be explicitly isolated and must not be used to conceal Aurora "
          "or bypass Roblox security.");
}

}  // namespace

bool ProjectSafetyDeveloperMode(const Environment& environment) {
  return Enabled(environment, "AURORA_DEVELOPER_MODE");
}

Status ValidateProjectSafetyGuard(const CommandLineOptions& options,
                                  const Environment& environment) {
  if (options.mode != CommandMode::kRun ||
      Enabled(environment, "AURORA_ISOLATED_CANARY")) {
    return Status::Ok();
  }

  if (ProjectSafetyDeveloperMode(environment)) {
    return Status::Ok();
  }

  if (options.force_run_latest) {
    return Blocked("--force-run-latest");
  }
  if (options.allow_unverified_build) {
    return Blocked("--allow-unverified-build");
  }
  if (!options.roblox_library_path.empty()) {
    return Blocked("--roblox-lib");
  }

  for (const std::string_view name : kExternalPayloadOverrides) {
    if (environment.HasNonEmpty(name)) {
      return Blocked(std::string(name));
    }
  }
  return Status::Ok();
}

}  // namespace aurora::runtime
