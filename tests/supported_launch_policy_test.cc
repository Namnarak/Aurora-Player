#include "runtime/graphics_launch_policy.h"
#include "runtime/supported_launch_policy.h"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace aurora {
namespace runtime {
namespace {

int RunPolicyProbe(bool interactive, bool explicit_override) {
  for (const char* name :
       {"AURORA_SKIP_LIBROBLOX_CTORS", "AURORA_INIT_CLIENT_SETTINGS",
        "AURORA_GRAPHICS_BACKEND", "AURORA_AUTO_EXIT_AFTER_PRESENT_MS",
        "AURORA_ALLOW_NO_COOKIE_LUA_APP"}) {
    if (unsetenv(name) != 0) {
      return 10;
    }
  }
  if (explicit_override) {
    if (setenv("AURORA_GRAPHICS_BACKEND", "custom-backend", 1) != 0 ||
        setenv("AURORA_ALLOW_NO_COOKIE_LUA_APP", "0", 1) != 0) {
      return 11;
    }
  }
  std::string error;
  if (!ApplySupportedLaunchPolicy(interactive, &error)) {
    return 12;
  }
  if (!interactive) {
    return getenv("AURORA_INIT_CLIENT_SETTINGS") == nullptr &&
                   getenv("AURORA_ALLOW_NO_COOKIE_LUA_APP") == nullptr
               ? 0
               : 13;
  }
  const char* skip_constructors = getenv("AURORA_SKIP_LIBROBLOX_CTORS");
  const char* initialize_settings = getenv("AURORA_INIT_CLIENT_SETTINGS");
  const char* auto_exit = getenv("AURORA_AUTO_EXIT_AFTER_PRESENT_MS");
  const char* allow_guest = getenv("AURORA_ALLOW_NO_COOKIE_LUA_APP");
  if (skip_constructors == nullptr || initialize_settings == nullptr ||
      auto_exit == nullptr || allow_guest == nullptr ||
      std::string(skip_constructors) != "0" ||
      std::string(initialize_settings) != "1" ||
      std::string(auto_exit) != "0" ||
      std::string(allow_guest) != (explicit_override ? "0" : "1")) {
    return 14;
  }
  const char* graphics = getenv("AURORA_GRAPHICS_BACKEND");
  if (explicit_override) {
    return graphics != nullptr && std::string(graphics) == "custom-backend"
               ? 0
               : 15;
  }
  return graphics == nullptr ? 0 : 16;
}

void ExpectPolicyProbe(bool interactive, bool explicit_override) {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::_Exit(RunPolicyProbe(interactive, explicit_override));
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(SupportedLaunchPolicyTest, PublishesInteractiveDefaults) {
  ExpectPolicyProbe(true, false);
}

TEST(SupportedLaunchPolicyTest, PreservesExplicitOverrides) {
  ExpectPolicyProbe(true, true);
}

TEST(SupportedLaunchPolicyTest, LeavesResearchModesUnchanged) {
  ExpectPolicyProbe(false, false);
}

void ExpectPackagedManifestProbe(bool candidate_override) {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    constexpr const char* packaged = "/relocated AppDir/share/aurora/default.json";
    constexpr const char* candidate = "/private-canary/candidate.json";
    if (setenv("AURORA_PACKAGED_COMPATIBILITY_MANIFEST", packaged, 1) != 0 ||
        unsetenv("AURORA_COMPATIBILITY_MANIFEST") != 0) {
      std::_Exit(10);
    }
    if (candidate_override &&
        setenv("AURORA_COMPATIBILITY_MANIFEST", candidate, 1) != 0) {
      std::_Exit(11);
    }
    std::string error;
    if (!ApplySupportedLaunchPolicy(false, &error)) std::_Exit(12);
    const char* manifest = getenv("AURORA_COMPATIBILITY_MANIFEST");
    std::_Exit(manifest != nullptr &&
                       std::string(manifest) == (candidate_override ? candidate : packaged)
                   ? 0 : 13);
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(SupportedLaunchPolicyTest, UsesRelocatedPackagedManifestByDefault) {
  ExpectPackagedManifestProbe(false);
}

TEST(SupportedLaunchPolicyTest, PreservesCandidateManifestOverPackagedDefault) {
  ExpectPackagedManifestProbe(true);
}

int RunGraphicsPolicyProbe(const char* backend) {
  for (const char* name : {
           "AURORA_GRAPHICS_BACKEND",
           "AURORA_PRELOAD_VULKAN_SHIM",
           "AURORA_DISABLE_AUTO_ANGLE_FALLBACK",
           "AURORA_SOFTWARE_WINDOW_FALLBACK",
           "AURORA_REQUIRE_REAL_GRAPHICS",
           "AURORA_CLIENT_SETTINGS_OVERRIDES_JSON",
           "ANV_SYS_MEM_LIMIT",
           "MESA_VK_ENABLE_SUBMIT_THREAD",
       }) {
    if (unsetenv(name) != 0) return 20;
  }
  if (backend != nullptr &&
      setenv("AURORA_GRAPHICS_BACKEND", backend, 1) != 0) {
    return 21;
  }
  const ProcessEnvironment environment;
  const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);
  std::string error;
  if (!ApplyGraphicsLaunchPolicy(config, &error)) return 22;

  const bool open_gl = backend != nullptr && std::string(backend) == "opengl";
  const char* resolved = getenv("AURORA_GRAPHICS_BACKEND");
  const char* preload = getenv("AURORA_PRELOAD_VULKAN_SHIM");
  if (resolved == nullptr || preload == nullptr ||
      std::string(resolved) != (open_gl ? "opengl" : "direct-vulkan") ||
      std::string(preload) != (open_gl ? "0" : "1")) {
    return 23;
  }
  if (open_gl) {
    const char* disable_angle =
        getenv("AURORA_DISABLE_AUTO_ANGLE_FALLBACK");
    const char* software = getenv("AURORA_SOFTWARE_WINDOW_FALLBACK");
    return disable_angle != nullptr && software != nullptr &&
                   std::string(disable_angle) == "1" &&
                   std::string(software) == "0" &&
                   getenv("AURORA_CLIENT_SETTINGS_OVERRIDES_JSON") == nullptr &&
                   getenv("ANV_SYS_MEM_LIMIT") == nullptr &&
                   getenv("MESA_VK_ENABLE_SUBMIT_THREAD") == nullptr
               ? 0
               : 24;
  }
  const char* overrides = getenv("AURORA_CLIENT_SETTINGS_OVERRIDES_JSON");
  const char* anv_memory_limit = getenv("ANV_SYS_MEM_LIMIT");
  const char* submit_thread = getenv("MESA_VK_ENABLE_SUBMIT_THREAD");
  std::string expected_anv_limit = "50";
  {
    std::ifstream input("/proc/meminfo");
    std::string key;
    unsigned long kb = 0;
    std::string unit;
    while (input >> key >> kb >> unit) {
      if (key == "MemTotal:") {
        expected_anv_limit = kb > 4UL * 1024UL * 1024UL ? "75" : "50";
        break;
      }
    }
  }
  return overrides != nullptr && anv_memory_limit != nullptr &&
                 submit_thread != nullptr &&
                 std::string(anv_memory_limit) == expected_anv_limit &&
                 std::string(submit_thread) == "1" &&
                 std::string(overrides).find(
                     "FStringGraphicsTextureManager2DenyPattern2") ==
                     std::string::npos &&
                 std::string(overrides).find(
                     "FStringGraphicsVulkanShaderMTDenyPattern") !=
                     std::string::npos &&
                 std::string(overrides).find(
                     "\"FFlagTextureTranscodeNewRollout\"") ==
                     std::string::npos &&
                 std::string(overrides).find(
                     "\"FStringTextureTranscodeRollout\"") ==
                     std::string::npos
             ? 0
             : 25;
}

void ExpectGraphicsPolicyProbe(const char* backend) {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    std::_Exit(RunGraphicsPolicyProbe(backend));
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(GraphicsLaunchPolicyTest, DefaultsToDirectVulkanAfterConfigResolution) {
  ExpectGraphicsPolicyProbe(nullptr);
}

TEST(GraphicsLaunchPolicyTest, MakesOpenGlStrictAndVulkanIndependent) {
  ExpectGraphicsPolicyProbe("opengl");
}

void ExpectEtc2Policy(const char* drivers, const char* legacy,
                      const char* override_value, const char* expected,
                      const char* cpu_option = nullptr,
                      const char* backend = "direct-vulkan") {
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    for (const char* name : {"VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                             "vk_require_etc2", "AURORA_ADVERTISE_ETC2",
                             "AURORA_SMALL_TEXTURE_UPSCALE",
                             "AURORA_TEXTURE_OVERRIDE_DIR",
                             "AURORA_TEXTURE_DUMP_DIR"}) {
      if (unsetenv(name) != 0) std::_Exit(30);
    }
    if (drivers != nullptr && setenv("VK_DRIVER_FILES", drivers, 1) != 0)
      std::_Exit(30);
    if (legacy != nullptr && setenv("VK_ICD_FILENAMES", legacy, 1) != 0)
      std::_Exit(30);
    if (override_value != nullptr &&
        setenv("vk_require_etc2", override_value, 1) != 0)
      std::_Exit(30);
    if (cpu_option != nullptr && setenv(cpu_option, "0", 1) != 0)
      std::_Exit(30);
    if (setenv("AURORA_GRAPHICS_BACKEND", backend, 1) != 0)
      std::_Exit(30);
    const ProcessEnvironment environment;
    const RuntimeConfig config = RuntimeConfig::FromEnvironment(environment);
    std::string error;
    if (!ApplyGraphicsLaunchPolicy(config, &error)) std::_Exit(31);
    const char* actual = getenv("vk_require_etc2");
    std::_Exit(expected == nullptr ? (actual == nullptr ? 0 : 32)
                                   : (actual != nullptr &&
                                              std::string(actual) == expected
                                          ? 0
                                          : 33));
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(GraphicsLaunchPolicyTest, PrefersRadvEtc2WithEitherIcdVariable) {
  ExpectEtc2Policy("/test/radeon_icd.x86_64.json", nullptr, nullptr, "true");
  ExpectEtc2Policy(nullptr, "/test/radeon_icd.json", nullptr, "true");
}

TEST(GraphicsLaunchPolicyTest, RespectsExplicitDriverAndTextureOverrides) {
  ExpectEtc2Policy("/test/radeon_icd.json", nullptr, "false", "false");
  for (const char* option : {"AURORA_ADVERTISE_ETC2",
                             "AURORA_SMALL_TEXTURE_UPSCALE",
                             "AURORA_TEXTURE_OVERRIDE_DIR",
                             "AURORA_TEXTURE_DUMP_DIR"}) {
    SCOPED_TRACE(option);
    ExpectEtc2Policy("/test/radeon_icd.json", nullptr, nullptr, "false",
                     option);
  }
}

TEST(GraphicsLaunchPolicyTest, LeavesOtherDriversAndBackendsAlone) {
  ExpectEtc2Policy("/test/nvidia_icd.json", nullptr, nullptr, nullptr);
  ExpectEtc2Policy("/test/intel_icd.json", nullptr, nullptr, nullptr);
  ExpectEtc2Policy("/test/nvidia_icd.json", "/test/radeon_icd.json", nullptr,
                   nullptr);
  ExpectEtc2Policy("/test/radeon_icd.json:/test/intel_icd.json", nullptr,
                   nullptr, nullptr);
  ExpectEtc2Policy("/test/radeon_icd.json", nullptr, nullptr, nullptr, nullptr,
                   "opengl");
}

}  // namespace
}  // namespace runtime
}  // namespace aurora
