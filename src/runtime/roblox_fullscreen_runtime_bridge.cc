#include "runtime/roblox_fullscreen_runtime_bridge.h"
#include "runtime/roblox_fullscreen_runtime_bridge_internal.h"

#include <elf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "linker/linker.h"
#include "window/window.h"

namespace aurora {
namespace runtime {
namespace {

constexpr std::size_t kSetterContractBytes = 96;

bool ContainsBytes(const std::uint8_t *code, std::size_t size,
                   const std::uint8_t *expected, std::size_t expected_size) {
  if (code == nullptr || expected == nullptr || expected_size == 0 ||
      expected_size > size) {
    return false;
  }
  for (std::size_t index = 0; index + expected_size <= size; ++index) {
    if (std::memcmp(code + index, expected, expected_size) == 0) {
      return true;
    }
  }
  return false;
}

bool IsExecutableImageRange(std::uintptr_t base, std::uintptr_t rva,
                            std::size_t size) {
  if (base == 0 || rva > std::numeric_limits<std::uintptr_t>::max() - size) {
    return false;
  }
  const auto *header = reinterpret_cast<const Elf64_Ehdr *>(base);
  if (std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
      header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_phoff == 0 ||
      header->e_phnum == 0) {
    return false;
  }
  const auto *program_headers = reinterpret_cast<const Elf64_Phdr *>(
      base + static_cast<std::uintptr_t>(header->e_phoff));
  const std::uintptr_t range_end = rva + size;
  for (std::size_t index = 0; index < header->e_phnum; ++index) {
    const Elf64_Phdr &segment = program_headers[index];
    if (segment.p_type != PT_LOAD || (segment.p_flags & PF_X) == 0 ||
        segment.p_vaddr >
            std::numeric_limits<std::uintptr_t>::max() - segment.p_memsz) {
      continue;
    }
    const std::uintptr_t segment_end =
        static_cast<std::uintptr_t>(segment.p_vaddr + segment.p_memsz);
    if (rva >= segment.p_vaddr && range_end <= segment_end) {
      return true;
    }
  }
  return false;
}

} // namespace

namespace internal {

bool HasExpectedFullscreenSetterContract(const std::uint8_t *code,
                                         std::size_t size) {
  if (code == nullptr || size < kSetterContractBytes) {
    return false;
  }
  constexpr std::array<std::uint8_t, 4> kFramePrologue = {0x55, 0x48, 0x89,
                                                          0xe5};
  constexpr std::array<std::uint8_t, 2> kCaptureBoolean = {0x89, 0xf3};
  constexpr std::array<std::uint8_t, 6> kCompareLegacyFullscreen = {
      0x38, 0x98, 0x59, 0x01, 0x00, 0x00};
  constexpr std::array<std::uint8_t, 6> kStoreLegacyFullscreen = {
      0x88, 0x98, 0x59, 0x01, 0x00, 0x00};
  constexpr std::array<std::uint8_t, 6> kCompareCurrentFullscreen = {
      0x38, 0x98, 0x61, 0x01, 0x00, 0x00};
  constexpr std::array<std::uint8_t, 6> kStoreCurrentFullscreen = {
      0x88, 0x98, 0x61, 0x01, 0x00, 0x00};
  const bool has_legacy_fullscreen_field =
      ContainsBytes(code, size, kCompareLegacyFullscreen.data(),
                    kCompareLegacyFullscreen.size()) &&
      ContainsBytes(code, size, kStoreLegacyFullscreen.data(),
                    kStoreLegacyFullscreen.size());
  const bool has_current_fullscreen_field =
      ContainsBytes(code, size, kCompareCurrentFullscreen.data(),
                    kCompareCurrentFullscreen.size()) &&
      ContainsBytes(code, size, kStoreCurrentFullscreen.data(),
                    kStoreCurrentFullscreen.size());
  return std::memcmp(code, kFramePrologue.data(), kFramePrologue.size()) == 0 &&
         ContainsBytes(code, 24, kCaptureBoolean.data(),
                       kCaptureBoolean.size()) &&
         (has_legacy_fullscreen_field || has_current_fullscreen_field);
}

bool HasExpectedFullscreenGetterContract(const std::uint8_t *code,
                                         std::size_t size) {
  if (code == nullptr || size < 32) {
    return false;
  }
  constexpr std::array<std::uint8_t, 4> kFramePrologue = {0x55, 0x48, 0x89,
                                                          0xe5};
  constexpr std::array<std::uint8_t, 3> kLoadSingletonAddress = {0x48, 0x8d,
                                                                 0x05};
  constexpr std::array<std::uint8_t, 1> kReturn = {0xc3};
  return std::memcmp(code, kFramePrologue.data(), kFramePrologue.size()) == 0 &&
         ContainsBytes(code, std::min<std::size_t>(size, 64),
                       kLoadSingletonAddress.data(),
                       kLoadSingletonAddress.size()) &&
         ContainsBytes(code, std::min<std::size_t>(size, 96), kReturn.data(),
                       kReturn.size());
}

bool ExtractExpectedFullscreenStateAccessors(
    const std::uint8_t *code, std::size_t size, std::uintptr_t setter_rva,
    FullscreenStateAccessors *accessors) {
  if (accessors == nullptr || setter_rva == 0 ||
      !HasExpectedFullscreenSetterContract(code, size)) {
    return false;
  }

  std::size_t compare_offset = size;
  std::uint32_t field_offset = 0;
  for (std::size_t index = 0; index + 6 <= size; ++index) {
    if (code[index] != 0x38 || code[index + 1] != 0x98) {
      continue;
    }
    const std::uint32_t candidate =
        static_cast<std::uint32_t>(code[index + 2]) |
        (static_cast<std::uint32_t>(code[index + 3]) << 8) |
        (static_cast<std::uint32_t>(code[index + 4]) << 16) |
        (static_cast<std::uint32_t>(code[index + 5]) << 24);
    if (candidate != 0x159U && candidate != 0x161U) {
      continue;
    }
    const std::array<std::uint8_t, 6> store = {
        0x88,           0x98, code[index + 2], code[index + 3], code[index + 4],
        code[index + 5]};
    if (!ContainsBytes(code, size, store.data(), store.size())) {
      continue;
    }
    compare_offset = index;
    field_offset = candidate;
    break;
  }
  if (compare_offset == size) {
    return false;
  }

  std::size_t getter_call_offset = size;
  for (std::size_t index = 0; index + 5 <= compare_offset; ++index) {
    if (code[index] == 0xe8 && compare_offset - (index + 5) <= 8) {
      getter_call_offset = index;
    }
  }
  if (getter_call_offset == size ||
      setter_rva >
          std::numeric_limits<std::uintptr_t>::max() - getter_call_offset - 5) {
    return false;
  }

  std::int32_t relative_target = 0;
  std::memcpy(&relative_target, code + getter_call_offset + 1,
              sizeof(relative_target));
  const std::uintptr_t return_rva = setter_rva + getter_call_offset + 5;
  std::uintptr_t getter_rva = 0;
  if (relative_target >= 0) {
    const auto displacement = static_cast<std::uintptr_t>(relative_target);
    if (return_rva >
        std::numeric_limits<std::uintptr_t>::max() - displacement) {
      return false;
    }
    getter_rva = return_rva + displacement;
  } else {
    const auto displacement = static_cast<std::uintptr_t>(
        -static_cast<std::int64_t>(relative_target));
    if (return_rva < displacement) {
      return false;
    }
    getter_rva = return_rva - displacement;
  }

  accessors->getter_rva = getter_rva;
  accessors->field_offset = field_offset;
  return getter_rva != 0;
}

} // namespace internal

RobloxFullscreenRuntimeBridge::~RobloxFullscreenRuntimeBridge() { Shutdown(); }

Status
RobloxFullscreenRuntimeBridge::Install(const compat::BuildProfile &profile) {
  if (installed_) {
    return Status::Error(StatusCode::kFailedPrecondition,
                         "fullscreen runtime bridge is already installed");
  }
  if (!profile.user_game_settings_fullscreen_setter_rva.has_value()) {
    return Status::Ok();
  }
  setter_rva_ = *profile.user_game_settings_fullscreen_setter_rva;
  if (!window::SetFullscreenStateSyncCallbacks(&SynchronizeState, &QueryState,
                                               this)) {
    setter_rva_ = 0;
    return Status::Error(StatusCode::kFailedPrecondition,
                         "fullscreen state synchronizer already has an owner");
  }
  installed_ = true;
  std::fprintf(stderr,
               "  [fullscreen] Roblox menu bridge armed for Build ID %s\n",
               profile.elf_build_id.c_str());
  return Status::Ok();
}

void RobloxFullscreenRuntimeBridge::Shutdown() {
  if (!installed_) {
    return;
  }
  window::ClearFullscreenStateSyncCallback();
  installed_ = false;
  setter_validated_ = false;
  setter_validation_failed_ = false;
  library_base_ = 0;
  setter_rva_ = 0;
  getter_rva_ = 0;
  fullscreen_field_offset_ = 0;
}

bool RobloxFullscreenRuntimeBridge::SynchronizeState(void *context,
                                                     bool fullscreen) {
  auto *bridge = static_cast<RobloxFullscreenRuntimeBridge *>(context);
  return bridge != nullptr && bridge->ApplyState(fullscreen);
}

bool RobloxFullscreenRuntimeBridge::QueryState(void *context,
                                               bool *fullscreen) {
  auto *bridge = static_cast<RobloxFullscreenRuntimeBridge *>(context);
  if (bridge == nullptr || fullscreen == nullptr ||
      !bridge->ResolveStateAccessors()) {
    return false;
  }

  using FullscreenGetter = void *(*)();
  auto getter = reinterpret_cast<FullscreenGetter>(bridge->library_base_ +
                                                   bridge->getter_rva_);
  void *settings = getter();
  if (settings == nullptr) {
    return false;
  }
  const auto *fullscreen_field = reinterpret_cast<const std::uint8_t *>(
      static_cast<const std::uint8_t *>(settings) +
      bridge->fullscreen_field_offset_);
  *fullscreen = __atomic_load_n(fullscreen_field, __ATOMIC_ACQUIRE) != 0;
  return true;
}

bool RobloxFullscreenRuntimeBridge::ResolveStateAccessors() {
  if (setter_rva_ == 0 || setter_validation_failed_) {
    return false;
  }
  if (library_base_ == 0) {
    library_base_ = linker::FindLoadedAndroidLibraryBase("libroblox");
  }
  if (library_base_ == 0) {
    return false;
  }
  if (!IsExecutableImageRange(library_base_, setter_rva_,
                              kSetterContractBytes)) {
    std::fprintf(stderr,
                 "  [fullscreen] libroblox setter is not in an executable "
                 "image range\n");
    setter_validation_failed_ = true;
    return false;
  }
  if (!setter_validated_) {
    const auto *setter_code =
        reinterpret_cast<const std::uint8_t *>(library_base_ + setter_rva_);
    internal::FullscreenStateAccessors accessors;
    if (!internal::ExtractExpectedFullscreenStateAccessors(
            setter_code, kSetterContractBytes, setter_rva_, &accessors)) {
      std::fprintf(stderr, "  [fullscreen] Build-ID setter contract validation "
                           "failed\n");
      setter_validation_failed_ = true;
      return false;
    }
    if (!IsExecutableImageRange(library_base_, accessors.getter_rva, 96) ||
        !internal::HasExpectedFullscreenGetterContract(
            reinterpret_cast<const std::uint8_t *>(library_base_ +
                                                   accessors.getter_rva),
            96)) {
      std::fprintf(stderr, "  [fullscreen] Build-ID settings getter contract "
                           "validation failed\n");
      setter_validation_failed_ = true;
      return false;
    }
    getter_rva_ = accessors.getter_rva;
    fullscreen_field_offset_ = accessors.field_offset;
    setter_validated_ = true;
    std::fprintf(stderr,
                 "  [fullscreen] settings state accessor validated "
                 "getter_rva=0x%llx field_offset=0x%x\n",
                 static_cast<unsigned long long>(getter_rva_),
                 fullscreen_field_offset_);
  }
  return true;
}

bool RobloxFullscreenRuntimeBridge::ApplyState(bool fullscreen) {
  if (!ResolveStateAccessors()) {
    return false;
  }

  using FullscreenSetter = void (*)(void *unused, int fullscreen);
  auto setter = reinterpret_cast<FullscreenSetter>(library_base_ + setter_rva_);
  setter(nullptr, fullscreen ? 1 : 0);
  std::fprintf(stderr, "  [fullscreen] Roblox state synchronized=%s\n",
               fullscreen ? "fullscreen" : "windowed");
  return true;
}

} // namespace runtime
} // namespace aurora
