#ifndef AURORA_PLATFORM_SDL_APPLICATION_METADATA_H_
#define AURORA_PLATFORM_SDL_APPLICATION_METADATA_H_

#include "aurora/status.h"

namespace aurora {
namespace platform {

inline constexpr char kAuroraApplicationName[] = "Aurora Player";
inline constexpr char kAuroraApplicationIdentifier[] =
    "space.bigrat.aurora";

// Configures the stable compositor identity before SDL initializes. The
// identifier matches the installed desktop file and icon name so Wayland and
// X11 compositors can group the runtime window with its launcher.
Status ConfigureSdlApplicationMetadata();

}  // namespace platform
}  // namespace aurora

#endif  // AURORA_PLATFORM_SDL_APPLICATION_METADATA_H_
