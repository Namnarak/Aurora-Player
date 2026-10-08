#ifndef AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_H_
#define AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_H_

#include <cstdint>

#include "aurora/status.h"
#include "compat/build_profile.h"

namespace aurora {
namespace runtime {

// Mirrors the Roblox UserGameSettings fullscreen flag through an exact
// Build-ID-scoped getter/setter pair and the SDL window.
class RobloxFullscreenRuntimeBridge final {
public:
  RobloxFullscreenRuntimeBridge() = default;
  ~RobloxFullscreenRuntimeBridge();

  RobloxFullscreenRuntimeBridge(const RobloxFullscreenRuntimeBridge &) = delete;
  RobloxFullscreenRuntimeBridge &
  operator=(const RobloxFullscreenRuntimeBridge &) = delete;

  Status Install(const compat::BuildProfile &profile);
  void Shutdown();
  bool installed() const { return installed_; }

private:
  static bool SynchronizeState(void *context, bool fullscreen);
  static bool QueryState(void *context, bool *fullscreen);
  bool ResolveStateAccessors();
  bool ApplyState(bool fullscreen);

  std::uintptr_t setter_rva_ = 0;
  std::uintptr_t getter_rva_ = 0;
  std::uint32_t fullscreen_field_offset_ = 0;
  std::uintptr_t library_base_ = 0;
  bool setter_validated_ = false;
  bool setter_validation_failed_ = false;
  bool installed_ = false;
};

} // namespace runtime
} // namespace aurora

#endif // AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_H_
