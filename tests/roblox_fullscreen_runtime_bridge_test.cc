#include "runtime/roblox_fullscreen_runtime_bridge_internal.h"
#include "window/window_fullscreen_state_sync.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace aurora {
namespace runtime {
namespace {

struct TestFullscreenState {
  bool fullscreen = false;
};

bool SetTestFullscreenState(void *context, bool fullscreen) {
  static_cast<TestFullscreenState *>(context)->fullscreen = fullscreen;
  return true;
}

bool QueryTestFullscreenState(void *context, bool *fullscreen) {
  *fullscreen = static_cast<TestFullscreenState *>(context)->fullscreen;
  return true;
}

std::array<std::uint8_t, 96> ValidSetterContract(std::uint8_t field_offset) {
  std::array<std::uint8_t, 96> code = {};
  code[0] = 0x55;
  code[1] = 0x48;
  code[2] = 0x89;
  code[3] = 0xe5;
  code[11] = 0x89;
  code[12] = 0xf3;
  code[27] = 0xe8;
  constexpr std::uintptr_t kSetterRva = 0x1000;
  constexpr std::uintptr_t kGetterRva = 0x2000;
  const std::int32_t getter_displacement =
      static_cast<std::int32_t>(kGetterRva - (kSetterRva + 27 + 5));
  std::memcpy(code.data() + 28, &getter_displacement,
              sizeof(getter_displacement));
  const std::array<std::uint8_t, 6> compare = {0x38, 0x98, field_offset,
                                               0x01, 0x00, 0x00};
  const std::array<std::uint8_t, 6> store = {0x88, 0x98, field_offset,
                                             0x01, 0x00, 0x00};
  for (std::size_t index = 0; index < compare.size(); ++index) {
    code[35 + index] = compare[index];
    code[43 + index] = store[index];
  }
  return code;
}

TEST(RobloxFullscreenRuntimeBridgeTest, AcceptsExpectedSetterSemantics) {
  const auto code = ValidSetterContract(0x59);

  EXPECT_TRUE(
      internal::HasExpectedFullscreenSetterContract(code.data(), code.size()));
}

TEST(RobloxFullscreenRuntimeBridgeTest, AcceptsCurrentSetterSemantics) {
  const auto code = ValidSetterContract(0x61);

  EXPECT_TRUE(
      internal::HasExpectedFullscreenSetterContract(code.data(), code.size()));
}

TEST(RobloxFullscreenRuntimeBridgeTest, RejectsDifferentSettingsField) {
  const auto code = ValidSetterContract(0x65);

  EXPECT_FALSE(
      internal::HasExpectedFullscreenSetterContract(code.data(), code.size()));
}

TEST(RobloxFullscreenRuntimeBridgeTest, RejectsTruncatedFunction) {
  const auto code = ValidSetterContract(0x59);

  EXPECT_FALSE(internal::HasExpectedFullscreenSetterContract(code.data(), 64));
}

TEST(RobloxFullscreenRuntimeBridgeTest,
     ExtractsGetterAndCurrentFullscreenField) {
  const auto code = ValidSetterContract(0x61);
  internal::FullscreenStateAccessors accessors;

  ASSERT_TRUE(internal::ExtractExpectedFullscreenStateAccessors(
      code.data(), code.size(), 0x1000, &accessors));
  EXPECT_EQ(accessors.getter_rva, 0x2000U);
  EXPECT_EQ(accessors.field_offset, 0x161U);
}

TEST(RobloxFullscreenRuntimeBridgeTest, RejectsGetterCallOutsideContract) {
  auto code = ValidSetterContract(0x61);
  code[27] = 0x90;
  internal::FullscreenStateAccessors accessors;

  EXPECT_FALSE(internal::ExtractExpectedFullscreenStateAccessors(
      code.data(), code.size(), 0x1000, &accessors));
}

TEST(RobloxFullscreenRuntimeBridgeTest, AcceptsSettingsSingletonGetter) {
  std::array<std::uint8_t, 96> code = {};
  code[0] = 0x55;
  code[1] = 0x48;
  code[2] = 0x89;
  code[3] = 0xe5;
  code[16] = 0x48;
  code[17] = 0x8d;
  code[18] = 0x05;
  code[30] = 0xc3;

  EXPECT_TRUE(
      internal::HasExpectedFullscreenGetterContract(code.data(), code.size()));
}

TEST(RobloxFullscreenRuntimeBridgeTest, QueriesGuestStateAndSynchronizesIt) {
  window::WindowFullscreenStateSync sync;
  TestFullscreenState state;
  bool fullscreen = true;

  ASSERT_TRUE(sync.Register(&SetTestFullscreenState, &QueryTestFullscreenState,
                            &state));
  ASSERT_TRUE(sync.Query(&fullscreen));
  EXPECT_FALSE(fullscreen);

  ASSERT_TRUE(sync.Notify(true));
  ASSERT_TRUE(sync.Query(&fullscreen));
  EXPECT_TRUE(fullscreen);
  sync.Clear();
  EXPECT_FALSE(sync.Query(&fullscreen));
}

} // namespace
} // namespace runtime
} // namespace aurora
