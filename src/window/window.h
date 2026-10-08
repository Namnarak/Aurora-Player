#ifndef AURORA_WINDOW_WINDOW_H_
#define AURORA_WINDOW_WINDOW_H_

#include <cstdint>
#include <filesystem>

#include "aurora/platform/display_refresh_capabilities.h"
#include "aurora/status.h"
#include "window/platform_event_observer.h"
#include "window/present_observer.h"
#include "window/window_pointer_capture_owner.h"
#include "window/window_surface_lifecycle.h"
#include "window/window_text_input_owner.h"

namespace aurora {
namespace runtime {
class LivePayloadUpdateMonitor;
}
namespace window {

using PreTextInputPumpCallback = bool (*)(void *context);
using FullscreenStateSyncCallback = bool (*)(void *context, bool fullscreen);
using FullscreenStateQueryCallback = bool (*)(void *context, bool *fullscreen);

// Configures persistent geometry and fullscreen state before Init().
Status ConfigureWindowStatePersistence(const std::filesystem::path &path);

bool Init(int width, int height, const char *title);

// Borrowed for the duration of the main event loop. Set/reset on its thread.
void SetPayloadUpdateMonitor(runtime::LivePayloadUpdateMonitor *monitor);

// Returns false after the user requests quit.
using GameActiveCallback = bool (*)(void *context);
bool PumpEvents(GameActiveCallback game_active = nullptr,
                void *game_active_context = nullptr);

// Paces SDL polling at 240 Hz without smoothing raw input. Returns the
// nanoseconds actually spent in SDL_DelayPrecise (0 if the tick already
// missed the slot or SDL still has events).
uint64_t PaceInputPump();

// True when CachedPresentModePolicy() is unthrottled.
bool UnthrottledPresentationRequested();

// Queues supported GameActivity fullscreen flags for the SDL thread.
bool RequestFullscreenFromAndroidWindowFlags(int flags, int mask);

// Queues a graceful app quit for the SDL event loop. Safe to call from a
// guest/JNI thread.
bool RequestQuit();
// Queues the confirmation prompt used by Roblox's showLeaveAppPrompt().
bool RequestQuitConfirmation();

// Mirrors Roblox fullscreen state changes into the SDL window and back.
bool SetFullscreenStateSyncCallbacks(
    FullscreenStateSyncCallback sync_callback,
    FullscreenStateQueryCallback query_callback, void *context);
void ClearFullscreenStateSyncCallback();

// The initial mapped surface is generation 1 and has no synthetic Created
// event.
bool PollWindowSurfaceEvent(WindowSurfaceEvent *event);
WindowSurfaceSnapshot GetWindowSurfaceSnapshot();

// Records an event only after the GAME/JNI bridge commits it.
Status RecordResizeReadinessSurfaceCommit(const WindowSurfaceEvent &event);

// Completes the optional resize gate after Roblox reaches Stopped.
Status StopResizeReadiness();
Status ResizeReadinessCompletionStatus();

struct WindowViewportSnapshot {
  int logical_width = 0;
  int logical_height = 0;
  int pixel_width = 0;
  int pixel_height = 0;
  float dpi_scale = 1.0f;

  bool valid() const {
    return logical_width > 0 && logical_height > 0 && pixel_width > 0 &&
           pixel_height > 0;
  }
};

// Keep logical input and pixel surface extents separate on high-DPI displays.
WindowViewportSnapshot GetWindowViewportSnapshot();

platform::DisplayRefreshCapabilities GetDisplayRefreshCapabilities();

// Registers the sole consumer of converted input and focus events.
bool SetPlatformEventObserver(PlatformEventObserver observer, void* context);
void ClearPlatformEventObserver();

// Applies MouseBehavior.LockCenter through SDL relative mouse mode.
bool SetMouseLockQueryCallback(MouseLockQueryCallback callback, void* context);
void ClearMouseLockQueryCallback();

// Clear waits for a pre-input callback already in flight.
bool SetPreTextInputPumpCallback(PreTextInputPumpCallback callback,
                                 void* context);
void ClearPreTextInputPumpCallback();

// Guest callbacks enqueue state; PumpEvents applies it on SDL's main thread.
void SetWindowTextInputOwnerEnabled(bool enabled);
bool RequestShowTextInput(uint64_t generation, const TextInputArea& area,
                          const TextInputOptions& options);
bool RequestHideTextInput(uint64_t generation);
// Main-thread cancellation for a specific Roblox focus generation, used when
// the shadow editor loses focus before hideKeyboard reaches the JNI bridge.
bool CancelTextInput(uint64_t generation);

void* GetEGLDisplay();

void* GetEGLSurface();

void* GetEGLContext();

void* GetEGLConfig();

// Raw Android surface returned by ANativeWindow_fromSurface().
void* GetNativeWindow();

// Host-only SDL_Window handle for Vulkan WSI.
void* GetBackendWindow();

bool UsesDirectVulkan();

// Returns nullptr before Init() or without a GL backend.
void* GetGLProcAddress(const char* name);

bool SwapBuffers();

bool HasPresentedFrame();

// Records a real vkQueuePresentKHR; tests must not fabricate readiness.
void NoteVulkanPresent();

// PumpEvents coalesces render-thread out-of-date reports into surface rebinds.
void NoteVulkanSurfaceOutOfDate();

// ClearPresentObserver waits for an in-flight host-present callback.
bool SetPresentObserver(PresentObserver observer, void* context);
void ClearPresentObserver();

class ScopedPresentObserver final {
 public:
  ScopedPresentObserver() = default;
  ~ScopedPresentObserver();

  ScopedPresentObserver(const ScopedPresentObserver&) = delete;
  ScopedPresentObserver& operator=(const ScopedPresentObserver&) = delete;

  bool Register(PresentObserver observer, void* context);
  void Reset();

 private:
  bool registered_ = false;
};

void ShowIfHidden();

// The render thread must bind EGL before GLdispatch initializes its TLS.
bool MakeCurrentOnThread();

bool ReleaseCurrentOnThread();

void Shutdown();

bool IsInitialised();

int GetWidth();
int GetHeight();

}  // namespace window
}  // namespace aurora

#endif  // AURORA_WINDOW_WINDOW_H_
