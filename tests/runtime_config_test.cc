#include "runtime/runtime_config.h"
#include "runtime/system_proxy.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aurora {
namespace runtime {
namespace {

class MapEnvironment final : public Environment {
 public:
  explicit MapEnvironment(
      std::unordered_map<std::string, std::string> values = {})
      : values_(std::move(values)) {}

  std::optional<std::string> Get(std::string_view name) const override {
    const auto found = values_.find(std::string(name));
    if (found == values_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

 private:
  std::unordered_map<std::string, std::string> values_;
};

TEST(RuntimeConfigTest, UsesSupportedDefaults) {
  const MapEnvironment environment;
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_FALSE(config.headless());
  EXPECT_EQ(config.roblox_library_path(), "rbx_bin/libroblox.so");
  EXPECT_EQ(config.graphics_backend(), GraphicsBackend::kVulkan);
  EXPECT_EQ(config.graphics_backend_name(), "direct-vulkan");
  EXPECT_EQ(config.window().width, 1280);
  EXPECT_EQ(config.window().height, 720);
  EXPECT_EQ(config.window().title, "Roblox");
  EXPECT_FALSE(config.window().high_dpi);
  EXPECT_TRUE(config.window().high_dpi_valid);
  EXPECT_EQ(config.theme_mode(), "roblox");
  EXPECT_FALSE(config.input_capabilities().touch_enabled);
  EXPECT_TRUE(config.input_capabilities().mouse_enabled);
  EXPECT_TRUE(config.input_capabilities().keyboard_enabled);
  EXPECT_EQ(config.device_profile().name, "pc-windows-11");
  EXPECT_EQ(config.device_profile().cache_key, "pc-windows-11");
  EXPECT_EQ(config.device_profile().display_name, "Windows 11 PC");
  EXPECT_TRUE(config.desktop_playability());
  ASSERT_TRUE(config.roblox_http_user_agent().has_value());
  EXPECT_EQ(*config.roblox_http_user_agent(), kRobloxDesktopHttpUserAgent);
  EXPECT_EQ(config.frame_rate().mode, FrameRateLimitMode::kUnmanaged);
  EXPECT_FALSE(config.performance().multithreaded_rendering);
  EXPECT_GT(config.performance().physical_core_count, 0);
  EXPECT_EQ(config.performance().memory_limit_mb, 0U);
  EXPECT_TRUE(config.performance().memory_limit_valid);
  EXPECT_EQ(config.performance().game_mode, GameModePolicy::kAuto);
  EXPECT_TRUE(config.performance().game_mode_valid);
  EXPECT_EQ(config.performance().physics_worker_mode,
            PhysicsWorkerMode::kThroughput);
  EXPECT_TRUE(config.performance().physics_worker_mode_valid);
  EXPECT_EQ(config.audio_output_device(), "default");
  EXPECT_TRUE(config.audio_output_device_valid());
  EXPECT_EQ(config.audio_input_device(), "default");
  EXPECT_TRUE(config.audio_input_device_valid());
  EXPECT_TRUE(config.microphone_enabled());
  EXPECT_FALSE(config.use_system_proxy());
  EXPECT_FALSE(config.fleasion_enabled());
  EXPECT_TRUE(config.fleasion_valid());
  EXPECT_FALSE(config.network_proxy().has_value());
  EXPECT_FALSE(config.discord_rpc().enabled);
  EXPECT_TRUE(config.discord_rpc().show_place_name);
  EXPECT_TRUE(config.discord_rpc().show_elapsed_time);
  EXPECT_TRUE(config.discord_rpc().join_enabled);
  EXPECT_TRUE(config.discord_rpc().public_servers_only);
  EXPECT_EQ(config.discord_rpc().join_button_label, "Join Server");
  EXPECT_EQ(config.discord_rpc().application_id, "1542837604083433633");
  EXPECT_EQ(config.discord_rpc().text.browsing, "Browsing experiences");
  EXPECT_EQ(config.discord_rpc().text.joining, "Joining an experience");
  EXPECT_EQ(config.discord_rpc().text.playing, "{place_name}");
  EXPECT_EQ(config.discord_rpc().text.state, "Playing Roblox");
  EXPECT_EQ(config.discord_rpc().text.unknown_place, "Unknown experience");
  EXPECT_TRUE(config.discord_rpc_valid());
  EXPECT_FALSE(config.has_unsafe_detached_thread_overrides());
  EXPECT_TRUE(config.unsafe_detached_thread_overrides().empty());
  EXPECT_TRUE(config.selected_theme_override().empty());
  EXPECT_TRUE(config.resolved_dark_theme(true));
  EXPECT_FALSE(config.resolved_dark_theme(false));
}

TEST(RuntimeConfigTest, ThemeModesUseRobloxSupportedValues) {
  constexpr std::array<std::pair<std::string_view, std::string_view>, 5>
      test_cases{{{"roblox", ""},
                  {"system", "System"},
                  {"dark", "Dark"},
                  {"light", "Light"},
                  {"classic", "Classic"}}};
  for (const auto& test_case : test_cases) {
    const RuntimeConfig config = RuntimeConfig::FromEnvironment(MapEnvironment(
        {{"AURORA_THEME", std::string(test_case.first)}}));
    ASSERT_TRUE(config.theme_mode_valid());
    EXPECT_EQ(config.selected_theme_override(), test_case.second);
    EXPECT_EQ(config.resolved_dark_theme(false),
              test_case.first == "dark");
      EXPECT_EQ(config.resolved_dark_theme(true),
              test_case.first == "dark" || test_case.first == "system" ||
                  test_case.first == "roblox" ||
                  test_case.first == "classic");
  }
}

TEST(RuntimeConfigTest, RobloxAndClassicPreserveResolvedAccountScheme) {
  const RuntimeConfig roblox = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_THEME", "roblox"}}));
  EXPECT_TRUE(roblox.resolved_dark_theme(false, true));
  EXPECT_FALSE(roblox.resolved_dark_theme(true, false));
  EXPECT_FALSE(roblox.resolved_dark_theme(false));

  const RuntimeConfig classic = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_THEME", "classic"}}));
  EXPECT_EQ(classic.selected_theme_override(), "Classic");
  EXPECT_TRUE(classic.resolved_dark_theme(false, true));
  EXPECT_FALSE(classic.resolved_dark_theme(true, false));
  EXPECT_FALSE(classic.resolved_dark_theme(false));

  const RuntimeConfig system = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_THEME", "system"}}));
  EXPECT_FALSE(system.resolved_dark_theme(false, true));
  EXPECT_TRUE(system.resolved_dark_theme(true, false));
}

TEST(RuntimeConfigTest, ReadsTypedRuntimeValues) {
  const MapEnvironment environment({
      {"AURORA_HEADLESS", "1"},
      {"ROBLOX_LIB_PATH", "/tmp/libroblox.so"},
      {"AURORA_GRAPHICS_BACKEND", "vulkan"},
      {"AURORA_WIN_WIDTH", "1920"},
      {"AURORA_WIN_HEIGHT", "1080"},
      {"AURORA_WIN_TITLE", "Aurora Test"},
      {"AURORA_WIN_HIGH_DPI", "true"},
      {"AURORA_THEME", "system"},
      {"AURORA_TOUCH_MODE", "on"},
      {"AURORA_DESKTOP_PLAYABILITY", "0"},
      {"AURORA_FRAME_RATE_LIMIT", "144"},
      {"AURORA_MULTITHREADED_RENDERING", "true"},
      {"AURORA_MEMORY_LIMIT_MB", "6144"},
      {"AURORA_GAMEMODE", "off"},
      {"AURORA_PHYSICS_WORKER_MODE", "latency"},
      {"AURORA_AUDIO_OUTPUT_DEVICE", "USB Headset"},
      {"AURORA_AUDIO_INPUT_DEVICE", "id:42"},
      {"AURORA_HTTP_PROXY_HOST", "proxy.example.test"},
      {"AURORA_HTTP_PROXY_PORT", "3128"},
      {"AURORA_DISCORD_RPC_ENABLED", "1"},
      {"AURORA_DISCORD_RPC_SHOW_PLACE_NAME", "0"},
      {"AURORA_DISCORD_RPC_SHOW_ELAPSED_TIME", "0"},
      {"AURORA_DISCORD_RPC_JOIN_ENABLED", "0"},
      {"AURORA_DISCORD_RPC_PUBLIC_SERVERS_ONLY", "0"},
      {"AURORA_DISCORD_RPC_JOIN_BUTTON_LABEL", "Play Together"},
      {"AURORA_DISCORD_APPLICATION_ID", "123456789012345678"},
      {"AURORA_DISCORD_RPC_TEXT_PLAYING", "In {place_name}"},
  });
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_TRUE(config.headless());
  EXPECT_EQ(config.roblox_library_path(), "/tmp/libroblox.so");
  EXPECT_EQ(config.graphics_backend(), GraphicsBackend::kVulkan);
  EXPECT_EQ(config.window().width, 1920);
  EXPECT_EQ(config.window().height, 1080);
  EXPECT_EQ(config.window().title, "Aurora Test");
  EXPECT_TRUE(config.window().high_dpi);
  EXPECT_TRUE(config.window().high_dpi_valid);
  EXPECT_EQ(config.theme_mode(), "system");
  EXPECT_TRUE(config.input_capabilities().touch_enabled);
  EXPECT_FALSE(config.desktop_playability());
  EXPECT_FALSE(config.roblox_http_user_agent().has_value());
  EXPECT_EQ(config.frame_rate().fixed_fps, 144);
  EXPECT_TRUE(config.performance().multithreaded_rendering);
  EXPECT_GT(config.performance().physical_core_count, 0);
  EXPECT_EQ(config.performance().memory_limit_mb, 6144U);
  EXPECT_TRUE(config.performance().memory_limit_enabled());
  EXPECT_EQ(config.performance().game_mode, GameModePolicy::kOff);
  EXPECT_TRUE(config.performance().game_mode_valid);
  EXPECT_EQ(config.performance().physics_worker_mode,
            PhysicsWorkerMode::kLatency);
  EXPECT_EQ(config.audio_output_device(), "USB Headset");
  EXPECT_TRUE(config.audio_output_device_valid());
  EXPECT_EQ(config.audio_input_device(), "id:42");
  EXPECT_TRUE(config.audio_input_device_valid());
  ASSERT_TRUE(config.network_proxy().has_value());
  EXPECT_EQ(config.network_proxy()->host, "proxy.example.test");
  EXPECT_EQ(config.network_proxy()->port, 3128);
  EXPECT_EQ(BuildNetworkProxyUrl(*config.network_proxy()),
            "http://proxy.example.test:3128");
  EXPECT_TRUE(config.discord_rpc().enabled);
  EXPECT_FALSE(config.discord_rpc().show_place_name);
  EXPECT_FALSE(config.discord_rpc().show_elapsed_time);
  EXPECT_FALSE(config.discord_rpc().join_enabled);
  EXPECT_FALSE(config.discord_rpc().public_servers_only);
  EXPECT_EQ(config.discord_rpc().join_button_label, "Play Together");
  EXPECT_EQ(config.discord_rpc().application_id, "123456789012345678");
  EXPECT_EQ(config.discord_rpc().text.playing, "In {place_name}");
  EXPECT_TRUE(config.discord_rpc_valid());
}

TEST(RuntimeConfigTest, BuildsBracketedIpv6ProxyUrl) {
  const std::optional<NetworkProxyConfig> proxy =
      ParseNetworkProxyConfig("::1", "8080");
  ASSERT_TRUE(proxy.has_value());
  EXPECT_EQ(BuildNetworkProxyUrl(*proxy), "http://[::1]:8080");
}

TEST(SystemProxyTest, SelectsHttpAndSocksProxies) {
  const SystemProxyResult https =
      SelectSystemProxy({"https://127.0.0.1:7890"});
  ASSERT_TRUE(https) << https.error;
  ASSERT_TRUE(https.proxy.has_value());
  EXPECT_EQ(BuildNetworkProxyUrl(*https.proxy), "http://127.0.0.1:7890");

  const SystemProxyResult socks =
      SelectSystemProxy({"socks://127.0.0.1:1080"});
  ASSERT_TRUE(socks) << socks.error;
  ASSERT_TRUE(socks.proxy.has_value());
  EXPECT_EQ(BuildNetworkProxyUrl(*socks.proxy), "socks5h://127.0.0.1:1080");
}

TEST(RuntimeConfigTest, RejectsMalformedDiscordApplicationId) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_DISCORD_APPLICATION_ID", "not-a-snowflake"}}));

  EXPECT_FALSE(config.discord_rpc_valid());

  const RuntimeConfig invalid_boolean = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_DISCORD_RPC_ENABLED", "sometimes"}}));
  EXPECT_FALSE(invalid_boolean.discord_rpc_valid());
}

TEST(RuntimeConfigTest, RejectsInvalidEnvironmentProxyValues) {
  for (const std::pair<const char*, const char*> invalid : {
           std::pair{"proxy.example.test", "not-a-port"},
           std::pair{"proxy.example.test", "0"},
           std::pair{"proxy.example.test", "65536"},
           std::pair{"https://proxy.example.test", "443"},
           std::pair{"proxy.example.test/path", "443"},
           std::pair{"proxy.example.test\nInjected", "443"},
       }) {
    SCOPED_TRACE(invalid.first);
    const MapEnvironment environment({
        {"AURORA_HTTP_PROXY_HOST", invalid.first},
        {"AURORA_HTTP_PROXY_PORT", invalid.second},
    });
    EXPECT_FALSE(RuntimeConfig::FromEnvironment(environment)
                     .network_proxy()
                     .has_value());
  }
}

TEST(RuntimeConfigTest, KeepsUnknownTouchModeDisabled) {
  const MapEnvironment environment({{"AURORA_TOUCH_MODE", "tablet"}});

  EXPECT_FALSE(RuntimeConfig::FromEnvironment(environment)
                   .input_capabilities()
                   .touch_enabled);
}

TEST(RuntimeConfigTest, PreservesExplicitRobloxHttpUserAgent) {
  const MapEnvironment environment({
      {"AURORA_USER_AGENT", "Roblox/TestDesktop"},
      {"AURORA_DESKTOP_PLAYABILITY", "0"},
      {"AURORA_TOUCH_MODE", "on"},
  });

  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_FALSE(config.desktop_playability());
  ASSERT_TRUE(config.roblox_http_user_agent().has_value());
  EXPECT_EQ(*config.roblox_http_user_agent(), "Roblox/TestDesktop");
}

TEST(RuntimeConfigTest, KeepsLegacyTouchAndPlayabilityOverridesIndependent) {
  const RuntimeConfig touch_desktop = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_TOUCH_MODE", "on"}}));
  EXPECT_TRUE(touch_desktop.input_capabilities().touch_enabled);
  EXPECT_TRUE(touch_desktop.desktop_playability());
  ASSERT_TRUE(touch_desktop.roblox_http_user_agent().has_value());
  EXPECT_EQ(*touch_desktop.roblox_http_user_agent(),
            kRobloxDesktopHttpUserAgent);

  const RuntimeConfig no_touch_mobile = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_DESKTOP_PLAYABILITY", "false"}}));
  EXPECT_FALSE(no_touch_mobile.input_capabilities().touch_enabled);
  EXPECT_FALSE(no_touch_mobile.desktop_playability());
  EXPECT_FALSE(no_touch_mobile.roblox_http_user_agent().has_value());
}

TEST(RuntimeConfigTest, KeepsMobileIdentityWithExplicitDesktopPlayability) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "mobile-pixel-7"},
      {"AURORA_DESKTOP_APP_POLICY", "1"},
      {"AURORA_TOUCH_MODE", "on"},
  }));

  EXPECT_EQ(config.device_profile().device_class, DeviceClass::kMobile);
  EXPECT_FALSE(config.device_profile().pc_hardware);
  EXPECT_TRUE(config.input_capabilities().touch_enabled);
  EXPECT_TRUE(config.desktop_playability());
  EXPECT_FALSE(config.roblox_touch_ui_enabled());
}

TEST(RuntimeConfigTest, FakeOffKeepsTouchInputButRequestsDesktopRobloxUi) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "android-tablet-hybrid"},
      {"AURORA_DESKTOP_APP_POLICY", "1"},
      {"AURORA_TOUCH_MODE", "on"},
      {"AURORA_MOUSE_MODE", "on"},
      {"AURORA_KEYBOARD_MODE", "on"},
  }));

  EXPECT_TRUE(config.input_capabilities().touch_enabled);
  EXPECT_TRUE(config.input_capabilities().mouse_enabled);
  EXPECT_TRUE(config.input_capabilities().keyboard_enabled);
  EXPECT_TRUE(config.desktop_playability());
  EXPECT_FALSE(config.roblox_touch_ui_enabled());

  const RuntimeConfig mobile = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "mobile-pixel-7"},
      {"AURORA_TOUCH_MODE", "on"},
  }));
  EXPECT_TRUE(mobile.input_capabilities().touch_enabled);
  EXPECT_FALSE(mobile.desktop_playability());
  EXPECT_TRUE(mobile.roblox_touch_ui_enabled());
}

TEST(RuntimeConfigTest, ResolvesOneLineDevicePresetsAndAliases) {
  struct ExpectedProfile {
    const char* configured;
    const char* canonical;
    DeviceClass device_class;
    const char* display_name;
    bool touch;
    bool mouse;
    bool keyboard;
    bool tablet;
    const char* user_agent;
  };
  for (const ExpectedProfile& expected : {
           ExpectedProfile{"pc", "pc-windows-11", DeviceClass::kPc,
                           "Windows 11 PC", false, true, true, false,
                           "Roblox/WinInet"},
           ExpectedProfile{"mobile", "mobile-pixel-7", DeviceClass::kMobile,
                           "Google Pixel 7", true, false, false, false,
                           nullptr},
           ExpectedProfile{"android-tablet-hybrid", "android-tablet-hybrid",
                           DeviceClass::kMobile, "Android Tablet", true, true,
                           true, true, nullptr},
           ExpectedProfile{"console", "console-ps5", DeviceClass::kConsole,
                           "PlayStation 5", false, true, true, false,
                           "Roblox/XboxOne"},
       }) {
    SCOPED_TRACE(expected.configured);
    const RuntimeConfig config = RuntimeConfig::FromEnvironment(
        MapEnvironment({{"AURORA_DEVICE_PROFILE", expected.configured}}));
    ASSERT_TRUE(config.device_profile_valid());
    EXPECT_EQ(config.device_profile().name, expected.canonical);
    EXPECT_EQ(config.device_profile().device_class, expected.device_class);
    EXPECT_EQ(config.device_profile().display_name, expected.display_name);
    EXPECT_EQ(config.input_capabilities().touch_enabled, expected.touch);
    EXPECT_EQ(config.input_capabilities().mouse_enabled, expected.mouse);
    EXPECT_EQ(config.input_capabilities().keyboard_enabled, expected.keyboard);
    EXPECT_EQ(config.device_profile().tablet, expected.tablet);
    if (expected.user_agent == nullptr) {
      EXPECT_FALSE(config.roblox_http_user_agent().has_value());
    } else {
      ASSERT_TRUE(config.roblox_http_user_agent().has_value());
      EXPECT_EQ(*config.roblox_http_user_agent(), expected.user_agent);
    }
  }
}

TEST(RuntimeConfigTest, ExplicitPresetWinsOverDeprecatedPlayabilityVariable) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "console-xbox-series-x"},
      {"AURORA_DESKTOP_PLAYABILITY", "1"},
      {"AURORA_TOUCH_MODE", "on"},
  }));

  EXPECT_EQ(config.device_profile().device_class, DeviceClass::kConsole);
  EXPECT_EQ(config.device_profile().name, "console-ps5");
  EXPECT_EQ(config.device_profile().display_name, "PlayStation 5");
  EXPECT_TRUE(config.input_capabilities().touch_enabled);
  EXPECT_FALSE(config.desktop_playability());
  ASSERT_TRUE(config.roblox_http_user_agent().has_value());
  EXPECT_EQ(*config.roblox_http_user_agent(), kRobloxConsoleAdmissionUserAgent);
}

TEST(RuntimeConfigTest, ResolvesDetailedIdentityAndInputOverrides) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "mobile"},
      {"AURORA_DEVICE_PLATFORM_NAME", "Android"},
      {"AURORA_DEVICE_NAME", "Google Pixel 9 Pro"},
      {"AURORA_DEVICE_MANUFACTURER", "Google"},
      {"AURORA_DEVICE_MODEL", "Pixel 9 Pro"},
      {"AURORA_DEVICE_BRAND", "google"},
      {"AURORA_DEVICE_CODE", "komodo"},
      {"AURORA_DEVICE_SKU", "komodo"},
      {"AURORA_DEVICE_SOC_MODEL", "Google Tensor G4"},
      {"AURORA_TOUCH_MODE", "on"},
      {"AURORA_MOUSE_MODE", "on"},
      {"AURORA_KEYBOARD_MODE", "off"},
  }));

  ASSERT_TRUE(config.device_profile_valid());
  EXPECT_EQ(config.device_profile().name, "mobile-pixel-7");
  EXPECT_EQ(config.device_profile().display_name, "Google Pixel 9 Pro");
  EXPECT_EQ(config.device_profile().model, "Pixel 9 Pro");
  EXPECT_EQ(config.device_profile().device_code, "komodo");
  EXPECT_EQ(config.device_profile().soc_model, "Google Tensor G4");
  EXPECT_NE(config.device_profile().cache_key, config.device_profile().name);
  EXPECT_EQ(config.device_profile().cache_key,
            BuildCustomDeviceProfileCacheKey(config.device_profile()));
  EXPECT_TRUE(config.input_capabilities().touch_enabled);
  EXPECT_TRUE(config.input_capabilities().mouse_enabled);
  EXPECT_FALSE(config.input_capabilities().keyboard_enabled);
  EXPECT_FALSE(config.roblox_http_user_agent().has_value());
}

TEST(RuntimeConfigTest, RejectsUnsafeDetailedIdentityFromEnvironment) {
  const RuntimeConfig empty = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "pc"},
      {"AURORA_DEVICE_MODEL", ""},
  }));
  EXPECT_FALSE(empty.device_profile_valid());

  const RuntimeConfig control = RuntimeConfig::FromEnvironment(MapEnvironment({
      {"AURORA_DEVICE_PROFILE", "pc"},
      {"AURORA_DEVICE_MODEL", "Windows\nInjected"},
  }));
  EXPECT_FALSE(control.device_profile_valid());
}

TEST(RuntimeConfigTest, MarksUnknownDeviceProfileInvalid) {
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_DEVICE_PROFILE", "smart-fridge"}}));

  EXPECT_FALSE(config.device_profile_valid());
  EXPECT_EQ(config.device_profile().name, kDefaultDeviceProfileName);
}

TEST(RuntimeConfigTest, RetainsLegacyBooleanSemantics) {
  const MapEnvironment false_text({{"AURORA_HEADLESS", "false"}});
  const MapEnvironment zero({{"AURORA_HEADLESS", "0"}});

  EXPECT_TRUE(RuntimeConfig::FromEnvironment(false_text).headless());
  EXPECT_FALSE(RuntimeConfig::FromEnvironment(zero).headless());
}

TEST(RuntimeConfigTest, RejectsUnsafeWindowSizes) {
  const MapEnvironment environment({
      {"AURORA_WIN_WIDTH", "-1"},
      {"AURORA_WIN_HEIGHT", "999999999999999999999"},
  });
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_EQ(config.window().width, 1280);
  EXPECT_EQ(config.window().height, 720);
}

TEST(RuntimeConfigTest, RejectsUnsafeAudioOutputDevice) {
  const RuntimeConfig empty = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_AUDIO_OUTPUT_DEVICE", ""}}));
  EXPECT_TRUE(empty.audio_output_device_valid());
  EXPECT_EQ(empty.audio_output_device(), "default");

  const RuntimeConfig control = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_AUDIO_OUTPUT_DEVICE", "Speaker\nInjected"}}));
  EXPECT_FALSE(control.audio_output_device_valid());
}

TEST(RuntimeConfigTest, ValidatesAudioInputDeviceAndMicrophonePolicy) {
  const RuntimeConfig empty = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_AUDIO_INPUT_DEVICE", ""}}));
  EXPECT_TRUE(empty.audio_input_device_valid());
  EXPECT_EQ(empty.audio_input_device(), "default");
  EXPECT_TRUE(empty.microphone_enabled());

  const RuntimeConfig disabled = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_AUDIO_INPUT_DEVICE", "disabled"}}));
  EXPECT_TRUE(disabled.audio_input_device_valid());
  EXPECT_FALSE(disabled.microphone_enabled());

  const RuntimeConfig control = RuntimeConfig::FromEnvironment(
      MapEnvironment({{"AURORA_AUDIO_INPUT_DEVICE", "Mic\nInjected"}}));
  EXPECT_FALSE(control.audio_input_device_valid());
}

TEST(RuntimeConfigTest, PreservesUnknownBackendName) {
  const MapEnvironment environment(
      {{"AURORA_GRAPHICS_BACKEND", "future-backend"}});
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_EQ(config.graphics_backend(), GraphicsBackend::kUnknown);
  EXPECT_EQ(config.graphics_backend_name(), "future-backend");
}

TEST(RuntimeConfigTest, RejectsEveryUnsafeDetachedThreadOverride) {
  const std::vector<std::string> unsafe_overrides = {
      "AURORA_APP_BRIDGE_APP_START_THREAD",
      "AURORA_CALL_REAL_APP_BRIDGE_INIT_THREAD",
      "AURORA_START_LUA_APP_DM_THREAD",
      "AURORA_CALL_REAL_APP_BRIDGE_UPDATE_SURFACE_THREAD",
      "AURORA_CALL_REAL_APP_BRIDGE_START_THREAD",
      "AURORA_SEND_APP_READY_THREAD",
      "AURORA_SEND_GAME_LOADED_THREAD",
  };

  for (const std::string& name : unsafe_overrides) {
    SCOPED_TRACE(name);
    const MapEnvironment environment({{name, "enabled"}});
    const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

    ASSERT_TRUE(config.has_unsafe_detached_thread_overrides());
    EXPECT_EQ(config.unsafe_detached_thread_overrides(),
              std::vector<std::string>({name}));
  }
}

TEST(RuntimeConfigTest, AllowsOnlyEmptyOrZeroDetachedThreadOverrides) {
  const MapEnvironment environment({
      {"AURORA_APP_BRIDGE_APP_START_THREAD", ""},
      {"AURORA_CALL_REAL_APP_BRIDGE_INIT_THREAD", "0"},
      {"AURORA_START_LUA_APP_DM_THREAD", "0"},
      {"AURORA_CALL_REAL_APP_BRIDGE_UPDATE_SURFACE_THREAD", ""},
      {"AURORA_CALL_REAL_APP_BRIDGE_START_THREAD", "0"},
      {"AURORA_SEND_APP_READY_THREAD", ""},
      {"AURORA_SEND_GAME_LOADED_THREAD", "0"},
  });
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_FALSE(config.has_unsafe_detached_thread_overrides());
  EXPECT_TRUE(config.unsafe_detached_thread_overrides().empty());
}

TEST(RuntimeConfigTest, ReportsUnsafeOverridesInStablePolicyOrder) {
  const MapEnvironment environment({
      {"AURORA_SEND_GAME_LOADED_THREAD", "1"},
      {"AURORA_APP_BRIDGE_APP_START_THREAD", "false"},
      {"AURORA_CALL_REAL_APP_BRIDGE_START_THREAD", "yes"},
  });
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);

  EXPECT_EQ(config.unsafe_detached_thread_overrides(),
            std::vector<std::string>({
                "AURORA_APP_BRIDGE_APP_START_THREAD",
                "AURORA_CALL_REAL_APP_BRIDGE_START_THREAD",
                "AURORA_SEND_GAME_LOADED_THREAD",
            }));
}

}  // namespace
}  // namespace runtime
}  // namespace aurora
