#ifndef AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_INTERNAL_H_
#define AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_INTERNAL_H_

#include <cstddef>
#include <cstdint>

namespace aurora {
namespace runtime {
namespace internal {

struct FullscreenStateAccessors {
  std::uintptr_t getter_rva = 0;
  std::uint32_t field_offset = 0;
};

// Verifies the stable semantic instructions of the current setter without
// depending on relocation-specific call displacements.
bool HasExpectedFullscreenSetterContract(const std::uint8_t *code,
                                         std::size_t size);
// Resolves the settings singleton getter and the flag field from the
// Build-ID-scoped setter instructions.
bool ExtractExpectedFullscreenStateAccessors(
    const std::uint8_t *code, std::size_t size, std::uintptr_t setter_rva,
    FullscreenStateAccessors *accessors);
bool HasExpectedFullscreenGetterContract(const std::uint8_t *code,
                                         std::size_t size);

} // namespace internal
} // namespace runtime
} // namespace aurora

#endif // AURORA_RUNTIME_ROBLOX_FULLSCREEN_RUNTIME_BRIDGE_INTERNAL_H_
