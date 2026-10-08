#ifndef AURORA_RUNTIME_ROBLOX_TEXT_SURFACE_OVERLAY_H_
#define AURORA_RUNTIME_ROBLOX_TEXT_SURFACE_OVERLAY_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "aurora/graphics/text_overlay_frame.h"
#include "aurora/status.h"
#include "runtime/roblox_text_display_state.h"

namespace aurora {
namespace runtime {

// Thread-safe bridge from Roblox's synchronous TextBox callback to the
// direct-Vulkan present path. The input callback only updates bounded state.
// SDL3_ttf rasterization is deferred to the render thread and the resulting
// RGBA frame is copied into the existing swapchain by libvulkan.so.
class RobloxTextSurfaceOverlay final {
 public:
  RobloxTextSurfaceOverlay() = default;
  ~RobloxTextSurfaceOverlay();

  RobloxTextSurfaceOverlay(const RobloxTextSurfaceOverlay&) = delete;
  RobloxTextSurfaceOverlay& operator=(const RobloxTextSurfaceOverlay&) = delete;

  Status Initialize(RobloxTextOverlayViewport viewport);
  Status UpdateViewport(RobloxTextOverlayViewport viewport);
  Status Shutdown();
  RobloxTextDisplaySink sink();

  bool QueryFrame(AuroraTextOverlayFrameInfo* frame);
  bool CopyFrame(std::uint64_t revision, void* rgba, std::size_t rgba_capacity);

 private:
  static void UpdateCallback(void* context,
                             const RobloxTextDisplayUpdate& update);
  void ApplyUpdate(const RobloxTextDisplayUpdate& update);
  Status RasterizeLocked();
  void ClearFrameLocked();
  void RecordFailureLocked(Status status);

  std::mutex mutex_;
  RobloxTextDisplayState state_;
  RobloxTextOverlayViewport viewport_;
  std::vector<std::uint8_t> rgba_;
  std::uint64_t state_revision_ = 0;
  std::uint64_t raster_revision_ = 0;
  std::uint64_t last_geometry_trace_generation_ = 0;
  RobloxTextOverlayGeometry last_geometry_trace_geometry_;
  RobloxTextDisplayEvent last_geometry_trace_event_ =
      RobloxTextDisplayEvent::kHide;
  bool has_geometry_trace_ = false;
  Status failure_;
  bool initialized_ = false;
  bool failure_logged_ = false;
  bool ready_logged_ = false;
};

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_ROBLOX_TEXT_SURFACE_OVERLAY_H_
