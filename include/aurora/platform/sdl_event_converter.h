#ifndef AURORA_PLATFORM_SDL_EVENT_CONVERTER_H_
#define AURORA_PLATFORM_SDL_EVENT_CONVERTER_H_

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "aurora/platform/platform_runtime.h"

namespace aurora {
namespace platform {

// Converts one SDL event without retaining either argument. The caller remains
// the sole owner of SDL_PollEvent and decides how converted events are routed.
// Returns false for invalid arguments and SDL events outside the platform
// contract.
bool ConvertSdlEvent(SDL_Window* window, const SDL_Event& source,
                     PlatformEvent* destination,
                     bool relative_mouse_mode = false);

}  // namespace platform
}  // namespace aurora

#endif  // AURORA_PLATFORM_SDL_EVENT_CONVERTER_H_
