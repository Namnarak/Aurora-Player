// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#ifndef AURORA_RUNTIME_PROJECT_SAFETY_GUARD_H_
#define AURORA_RUNTIME_PROJECT_SAFETY_GUARD_H_

#include "aurora/status.h"
#include "runtime/command_line.h"
#include "runtime/environment.h"

namespace aurora::runtime {

// Keeps normal Aurora launches on the supported, signed managed-payload path.
// This guard is deliberately about project safety and provenance: it does not
// hide Aurora, spoof Roblox, disable Roblox security features, or interfere
// with platform enforcement. Experimental overrides remain available only in
// explicit developer mode or the updater's isolated canary.
Status ValidateProjectSafetyGuard(const CommandLineOptions& options,
                                  const Environment& environment);

bool ProjectSafetyDeveloperMode(const Environment& environment);

}  // namespace aurora::runtime

#endif  // AURORA_RUNTIME_PROJECT_SAFETY_GUARD_H_
