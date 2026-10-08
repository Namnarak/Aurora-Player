#include "aurora/audio/roblox_output_device_bridge.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "audio/roblox_output_device_bridge_internal.h"
#include "compat/fmod_output_device_contract.h"

namespace aurora::audio {
namespace {

compat::FmodOutputDeviceBridgeProfile TestProfile() {
  return compat::FmodOutputDeviceBridgeProfile{0x1000, 0x2000, 0x3000,
                                               0x4000, 0x5000, 0x6000};
}

TEST(RobloxOutputDeviceBridgeTest, ValidatesExactGuestStringAbiContract) {
  constexpr std::array<std::uint8_t, 24> kExpected = {
      0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48,
      0x89, 0xd3, 0x49, 0x89, 0xf6, 0x49, 0x89, 0xff, 0x48, 0x83, 0xfa, 0x16,
  };
  EXPECT_TRUE(internal::HasExpectedFmodStringConstructorContract(
      kExpected.data(), kExpected.size()));

  auto changed = kExpected;
  changed.back() = 0x17;
  EXPECT_FALSE(internal::HasExpectedFmodStringConstructorContract(
      changed.data(), changed.size()));
  EXPECT_FALSE(internal::HasExpectedFmodStringConstructorContract(
      kExpected.data(), kExpected.size() - 1));
}

TEST(RobloxOutputDeviceBridgeTest, ValidatesNativeSinkDestroyAbiContract) {
  constexpr std::array<std::uint8_t, 63> kExpected = {
      0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x8b, 0x5f, 0x08, 0x48,
      0x85, 0xdb, 0x74, 0x29, 0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff,
      0xf0, 0x48, 0x0f, 0xc1, 0x43, 0x08, 0x48, 0x85, 0xc0, 0x74, 0x11,
      0x48, 0x83, 0xc4, 0x08, 0x5b, 0x5d, 0xc3, 0x48, 0x8b, 0x03, 0x48,
      0x89, 0xdf, 0xff, 0x50, 0x10, 0x48, 0x89, 0xdf, 0x48, 0x83, 0xc4,
      0x08, 0x5b, 0x5d, 0xe9, 0x11, 0x22, 0x33, 0x44};
  EXPECT_TRUE(internal::HasExpectedFmodNativeSinkDestroyContract(
      kExpected.data(), kExpected.size()));

  auto changed = kExpected;
  changed[47] ^= 1;
  EXPECT_FALSE(internal::HasExpectedFmodNativeSinkDestroyContract(
      changed.data(), changed.size()));
  EXPECT_FALSE(internal::HasExpectedFmodNativeSinkDestroyContract(
      kExpected.data(), kExpected.size() - 1));
}

TEST(RobloxOutputDeviceBridgeTest, SelectsNativeCaptureForExactSupportedBuild) {
  compat::BuildProfile profile;
  profile.elf_build_id = "913ea9839b6084470a25e4be8feddc88214a26a3";
  profile.fmod_output_device_bridge = compat::FmodOutputDeviceBridgeProfile{
      0x6e6a588, 0x1ddb51e, 0x3253718, 0x32537b8, 0x3253768, 0x325301a, 2};
  profile.fmod_output_device_bridge->input_method_rvas =
      {0x3253d98, 0x3253da6, 0x32541f4, 0x3252dba};

  const auto capture = internal::FindNativeInputCaptureProfile(profile);
  ASSERT_TRUE(capture.has_value());
  EXPECT_EQ(capture->capture_method_rvas,
            (std::array<std::uintptr_t, 6>{0x32536d2, 0x3252d98, 0x3254556,
                                           0x3254bde, 0x3252aa0, 0x3254eb4}));
  EXPECT_EQ(capture->sink_vtable_rva, 0x6e6ada8);
  EXPECT_EQ(capture->deliver_pcm_rva, 0x3259cc0);
  EXPECT_EQ(capture->sink_destroy_rva, 0x1e24f60);

  profile.elf_build_id = "0000000000000000000000000000000000000000";
  EXPECT_FALSE(internal::FindNativeInputCaptureProfile(profile).has_value());
  profile.elf_build_id = "913ea9839b6084470a25e4be8feddc88214a26a3";
  profile.fmod_output_device_bridge->vtable_rva++;
  EXPECT_FALSE(internal::FindNativeInputCaptureProfile(profile).has_value());
}

TEST(RobloxOutputDeviceBridgeTest, ValidatesEveryInterposedVtableSlot) {
  constexpr std::uintptr_t kImageBase = 0x10000000;
  const compat::FmodOutputDeviceBridgeProfile profile = TestProfile();
  std::array<std::uintptr_t, 18> vtable{};
  vtable[5] = kImageBase + profile.count_method_rva;
  vtable[6] = kImageBase + profile.info_method_rva;
  vtable[7] = kImageBase + profile.current_method_rva;
  vtable[17] = kImageBase + profile.select_method_rva;
  EXPECT_TRUE(internal::HasExpectedFmodOutputDeviceVtable(vtable.data(),
                                                          kImageBase, profile));

  ++vtable[17];
  EXPECT_FALSE(internal::HasExpectedFmodOutputDeviceVtable(
      vtable.data(), kImageBase, profile));
}

TEST(RobloxOutputDeviceBridgeTest, BuildsStableDistinctHostGuids) {
  const std::string first = internal::MakeOutputDeviceGuid(17, "USB Headset");
  EXPECT_EQ(first, internal::MakeOutputDeviceGuid(17, "USB Headset"));
  EXPECT_NE(first, internal::MakeOutputDeviceGuid(18, "USB Headset"));
  EXPECT_NE(first, internal::MakeOutputDeviceGuid(17, "HDMI Output"));
  EXPECT_EQ(internal::MakeOutputDeviceGuid(0, "ignored"), "aurora:default");
}

TEST(RobloxOutputDeviceBridgeTest, UsesDeviceListSlotsWithoutReusingLegacySlots) {
  constexpr std::uintptr_t kImageBase = 0x10000000;
  auto profile = TestProfile();
  profile.vtable_layout_version = 2;
  std::array<std::uintptr_t, 20> vtable{};
  vtable[5] = kImageBase + profile.count_method_rva;
  vtable[6] = kImageBase + profile.info_method_rva;
  vtable[8] = kImageBase + profile.current_method_rva;
  vtable[19] = kImageBase + profile.select_method_rva;
  EXPECT_TRUE(internal::HasExpectedFmodOutputDeviceVtable(
      vtable.data(), kImageBase, profile));
  profile.vtable_layout_version = 1;
  EXPECT_FALSE(internal::HasExpectedFmodOutputDeviceVtable(
      vtable.data(), kImageBase, profile));
  profile.vtable_layout_version = 2;
  ++vtable[19];
  EXPECT_FALSE(internal::HasExpectedFmodOutputDeviceVtable(
      vtable.data(), kImageBase, profile));
}

TEST(RobloxOutputDeviceBridgeTest, RejectsAnUnknownLayoutBeforeInstalling) {
  compat::BuildProfile profile;
  profile.allow_host_abi_bridges = true;
  profile.fmod_output_device_bridge = TestProfile();
  profile.fmod_output_device_bridge->vtable_layout_version = 3;
  RobloxOutputDeviceBridge bridge;
  EXPECT_EQ(bridge.Install(profile).code(), StatusCode::kFailedPrecondition);
}

TEST(RobloxOutputDeviceBridgeTest, DeviceListSelectorKeepsArgumentsAndCountSlotExact) {
  std::string relocated(compat::kFmodDeviceListsSelectContract,
                        compat::kFmodDeviceListsSelectContractSize);
  for (const std::size_t start : {24, 38, 55, 78, 96, 116}) {
    relocated.replace(start, 4, 4, '\x17');
  }
  EXPECT_TRUE(compat::HasFmodDeviceListsSelectContract(relocated));
  // Device index register, branch into legacy path, count slot, system offset.
  for (const std::size_t index : {17, 44, 90, 112}) {
    auto changed = relocated;
    changed[index] ^= 1;
    EXPECT_FALSE(compat::HasFmodDeviceListsSelectContract(changed));
  }
  relocated.pop_back();
  EXPECT_FALSE(compat::HasFmodDeviceListsSelectContract(relocated));
}

TEST(RobloxOutputDeviceBridgeTest, ValidatesInputSlotsForBothLayouts) {
  constexpr std::uintptr_t base = 0x10000000;
  for (int layout : {1, 2}) {
    auto profile = TestProfile();
    profile.vtable_layout_version = layout;
    profile.input_method_rvas = {0x7000, 0x8000, 0x9000, 0xa000};
    std::array<std::uintptr_t, 20> vtable{};
    vtable[5] = base + profile.count_method_rva;
    vtable[6] = base + profile.info_method_rva;
    vtable[profile.current_vtable_index()] = base + profile.current_method_rva;
    vtable[profile.select_vtable_index()] = base + profile.select_method_rva;
    const auto indexes = profile.input_vtable_indexes();
    for (std::size_t i = 0; i < 4; ++i)
      vtable[indexes[i]] = base + profile.input_method_rvas[i];
    ASSERT_TRUE(internal::HasExpectedFmodOutputDeviceVtable(vtable.data(), base,
                                                            profile));
    for (const auto index : indexes) {
      ++vtable[index];
      EXPECT_FALSE(internal::HasExpectedFmodOutputDeviceVtable(vtable.data(),
                                                               base, profile));
      --vtable[index];
    }
  }
}

TEST(RobloxOutputDeviceBridgeTest, EnforcesSingleProcessOwner) {
  compat::BuildProfile profile;
  profile.elf_build_id = "d0cb1fa0deb3d9161b4cd77530cbcd2e50de3a21";
  profile.allow_host_abi_bridges = true;
  profile.fmod_output_device_bridge = TestProfile();

  RobloxOutputDeviceBridge first;
  RobloxOutputDeviceBridge second;
  ASSERT_TRUE(first.Install(profile).ok());
  EXPECT_EQ(second.Install(profile).code(), StatusCode::kFailedPrecondition);
  first.Shutdown();
  EXPECT_TRUE(second.Install(profile).ok());
  second.Shutdown();
}

}  // namespace
}  // namespace aurora::audio
