#ifndef AURORA_PLATFORM_SDL_PLATFORM_RUNTIME_H_
#define AURORA_PLATFORM_SDL_PLATFORM_RUNTIME_H_

#include <memory>

#include "aurora/platform/platform_runtime.h"

namespace aurora {
namespace platform {

// Creates an SDL3-backed runtime without exposing SDL types to consumers.
std::unique_ptr<PlatformRuntime> CreateSdlPlatformRuntime();

}  // namespace platform
}  // namespace aurora

#endif  // AURORA_PLATFORM_SDL_PLATFORM_RUNTIME_H_
