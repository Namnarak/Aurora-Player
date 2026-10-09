# Copyright 2026 Aurora Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

cmake_minimum_required(VERSION 3.20)
project(AuroraPlayer
  VERSION 1.0.3
  DESCRIPTION "Aurora Player Android x86-64 Roblox runtime for Linux"
  LANGUAGES CXX
)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(AURORA_BUILD_GIT_COMMIT "unknown")
set(AURORA_BUILD_GIT_DIRTY "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE AURORA_GIT_REV_PARSE_RESULT
    OUTPUT_VARIABLE AURORA_GIT_REV_PARSE_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(AURORA_GIT_REV_PARSE_RESULT EQUAL 0 AND
     AURORA_GIT_REV_PARSE_OUTPUT MATCHES "^[0-9a-fA-F]+$")
    string(TOLOWER "${AURORA_GIT_REV_PARSE_OUTPUT}"
      AURORA_BUILD_GIT_COMMIT)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE AURORA_GIT_STATUS_RESULT
      OUTPUT_VARIABLE AURORA_GIT_STATUS_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(AURORA_GIT_STATUS_RESULT EQUAL 0)
      if(AURORA_GIT_STATUS_OUTPUT STREQUAL "")
        set(AURORA_BUILD_GIT_DIRTY "false")
      else()
        set(AURORA_BUILD_GIT_DIRTY "true")
      endif()
    endif()
  endif()
endif()

set(AURORA_DEFAULT_COMPATIBILITY_MANIFEST
  "${CMAKE_SOURCE_DIR}/config/roblox_compatibility.json"
  CACHE FILEPATH
  "Fallback compatibility manifest used outside an installed runtime"
)
set(AURORA_DEFAULT_SIGNING_TRUST_MANIFEST
  "${CMAKE_SOURCE_DIR}/config/roblox_signing_certificates.json"
  CACHE FILEPATH
  "Fallback signing trust manifest used outside an installed runtime"
)
set(AURORA_DISCORD_APPLICATION_ID "1542837604083433633" CACHE STRING
  "Discord application ID baked into Aurora Rich Presence releases"
)
string(LENGTH "${AURORA_DISCORD_APPLICATION_ID}"
  AURORA_DISCORD_APPLICATION_ID_LENGTH)
if(NOT AURORA_DISCORD_APPLICATION_ID STREQUAL "" AND
   (NOT AURORA_DISCORD_APPLICATION_ID MATCHES "^[0-9]+$" OR
    AURORA_DISCORD_APPLICATION_ID_LENGTH LESS 17 OR
    AURORA_DISCORD_APPLICATION_ID_LENGTH GREATER 20))
  message(FATAL_ERROR
    "AURORA_DISCORD_APPLICATION_ID must be empty or a Discord snowflake")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libjnivm uses C++ exceptions, so these cannot be directory-wide.
add_library(aurora_compile_options INTERFACE)
target_compile_options(aurora_compile_options INTERFACE
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wno-unused-parameter;-fno-exceptions;-g>"
)

function(aurora_apply_compile_options target_name)
  target_link_libraries(${target_name} PRIVATE aurora_compile_options)
endfunction()

option(AURORA_ENABLE_UPSTREAM_JNIVM
  "Build upstream libjnivm for the incremental JNI runtime migration" ON)
if(AURORA_ENABLE_UPSTREAM_JNIVM)
  set(JNIVM_ENABLE_TRACE OFF CACHE BOOL
    "Enable tracing inside upstream libjnivm")
  set(JNIVM_ENABLE_GC ON CACHE BOOL
    "Enable local/global reference tracking inside upstream libjnivm")
  set(JNIVM_ENABLE_DEBUG OFF CACHE BOOL
    "Enable upstream libjnivm class-dump/code-generation support")
  set(JNIVM_ENABLE_RETURN_NON_ZERO OFF CACHE BOOL
    "Allow upstream libjnivm to fabricate unresolved object return values")
  set(JNIVM_FAKE_JNI_MINECRAFT_LINUX_COMPAT OFF CACHE BOOL
    "Enable legacy fake-jni static method promotion")
  set(JNIVM_ENABLE_TESTS OFF CACHE BOOL
    "Build upstream libjnivm's own test suite")
  set(JNIVM_BUILD_EXAMPLES OFF CACHE BOOL
    "Build upstream libjnivm examples")

  add_subdirectory(third_party/libjnivm EXCLUDE_FROM_ALL)
  # Work around a missing include in libjnivm.
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jnivm PRIVATE -include string)
  endif()
  target_link_libraries(jnivm PUBLIC Threads::Threads)
  add_library(Aurora::UpstreamJni ALIAS jnivm)
endif()

add_subdirectory(third_party/mcpelauncher-linker)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(linker PRIVATE -w)
endif()

set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL
  "Build Khronos Vulkan-Headers tests" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL
  "Install vendored Vulkan-Headers" FORCE)
add_subdirectory(third_party/Vulkan-Headers EXCLUDE_FROM_ALL)

include(CTest)
if(BUILD_TESTING)
  # Package builds disable tests and do not fetch Google Test.
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

find_package(OpenSSL REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/AuroraPlatformGraphics.cmake)
foreach(foundation_target IN ITEMS
    aurora_platform_sdl
    aurora_graphics_foundation
    aurora_sdl_vulkan_wsi)
  aurora_apply_compile_options(${foundation_target})
endforeach()
foreach(graphics_test_target IN ITEMS
    bionic_egl_bridge_test
    platform_graphics_foundation_test)
  if(TARGET ${graphics_test_target})
    aurora_apply_compile_options(${graphics_test_target})
  endif()
endforeach()

include(${CMAKE_SOURCE_DIR}/cmake/AuroraAudio.cmake)
foreach(audio_target IN ITEMS
    aurora_audio_core
    aurora_audio_sdl
    aurora_audio_fmod_java_runtime
    aurora_fmod_jni_audio_bridge
    aurora_audio_opensl_adapter
    aurora_opensles)
  aurora_apply_compile_options(${audio_target})
endforeach()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
  # GCC 16 reports a false array-bounds warning in this translation unit.
  target_compile_options(aurora_audio_opensl_adapter PRIVATE
    -Wno-array-bounds
  )
endif()
if(TARGET audio_foundation_test)
  aurora_apply_compile_options(audio_foundation_test)
endif()
if(TARGET opensl_playback_runtime_test)
  aurora_apply_compile_options(opensl_playback_runtime_test)
endif()
if(TARGET fmod_java_audio_runtime_test)
  aurora_apply_compile_options(fmod_java_audio_runtime_test)
endif()
if(TARGET fmod_jni_audio_bridge_test)
  aurora_apply_compile_options(fmod_jni_audio_bridge_test)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/AuroraElfCompatibility.cmake)

# Bionic stdio symbols must exist before the linker target.
add_library(aurora_bionic_stdio_runtime STATIC
  src/compat/bionic_stdio_runtime.cc
)
add_library(Aurora::BionicStdio ALIAS aurora_bionic_stdio_runtime)
target_include_directories(aurora_bionic_stdio_runtime PUBLIC include)
aurora_apply_compile_options(aurora_bionic_stdio_runtime)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBYAML REQUIRED IMPORTED_TARGET yaml-0.1)
pkg_check_modules(MINIZIP REQUIRED IMPORTED_TARGET minizip)
pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(LIBADWAITA REQUIRED IMPORTED_TARGET "libadwaita-1>=1.6")
pkg_check_modules(WEBKITGTK6 REQUIRED IMPORTED_TARGET webkitgtk-6.0)
pkg_check_modules(WEBVIEW_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

set(AURORA_UPDATE_PROVIDER_SOURCES)
if(PROJECT_NAME STREQUAL "AuroraPlayer")
  list(APPEND AURORA_UPDATE_PROVIDER_SOURCES
    src/update/apkcombo_provider.cc
  )
endif()

add_library(aurora_update STATIC
  src/update/android_manifest.cc
  src/update/apk_bundle.cc
  src/update/apk_provider.cc
  src/update/apk_signature.cc
  src/update/apkpure_provider.cc
  src/update/candidate_approval.cc
  src/update/compatibility_catalog.cc
  src/update/http_download.cc
  src/update/host_abi_deriver.cc
  src/update/payload_integrity.cc
  src/update/payload_store.cc
  src/update/readiness_canary.cc
  src/update/unsafe_latest_runner.cc
  src/update/update_config.cc
  src/update/update_coordinator.cc
  src/update/zip_archive.cc
  ${AURORA_UPDATE_PROVIDER_SOURCES}
)
add_library(Aurora::Update ALIAS aurora_update)
target_include_directories(aurora_update PUBLIC include)
if(PROJECT_NAME STREQUAL "AuroraPlayer")
  target_compile_definitions(aurora_update PRIVATE
    AURORA_ENABLE_APKCOMBO_PROVIDER=1
  )
endif()
target_link_libraries(aurora_update PUBLIC
  Aurora::Compat
  CURL::libcurl
  OpenSSL::Crypto
  PkgConfig::LIBYAML
  PkgConfig::MINIZIP
  PkgConfig::CAPSTONE
  nlohmann_json::nlohmann_json
  Threads::Threads
)
aurora_apply_compile_options(aurora_update)

add_executable(aurora_updater src/update/main.cc)
target_link_libraries(aurora_updater PRIVATE Aurora::Update)
target_compile_definitions(aurora_updater PRIVATE
  "AURORA_DEFAULT_COMPATIBILITY_MANIFEST=\"${AURORA_DEFAULT_COMPATIBILITY_MANIFEST}\""
  "AURORA_DEFAULT_SIGNING_TRUST_MANIFEST=\"${AURORA_DEFAULT_SIGNING_TRUST_MANIFEST}\""
)
aurora_apply_compile_options(aurora_updater)

add_library(aurora_legacy_jnivm STATIC
  src/jnivm/jnivm.cc
  src/jnivm/jni_references.cc
  src/jnivm/jni_fields.cc
  src/jnivm/jni_strings.cc
)
add_library(Aurora::LegacyJni ALIAS aurora_legacy_jnivm)
target_include_directories(aurora_legacy_jnivm PUBLIC include)
aurora_apply_compile_options(aurora_legacy_jnivm)
# A JDK is not required for these ABI-only headers.
set(AURORA_JNI_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/third_party/jni/include
)
target_include_directories(aurora_legacy_jnivm PUBLIC
  ${AURORA_JNI_INCLUDE_DIRS}
)

add_library(aurora_runtime STATIC
  src/runtime/command_line.cc
  src/runtime/crash_report_policy.cc
  src/runtime/discord_rpc.cc
  src/runtime/device_memory_profile.cc
  src/runtime/device_profile.cc
  src/runtime/environment.cc
  src/runtime/external_launch_broker.cc
  src/runtime/external_client_detector.cc
  src/runtime/frame_rate_policy.cc
  src/runtime/fleasion.cc
  src/runtime/game_mode.cc
  src/runtime/game_session_coordinator.cc
  src/runtime/graphics_launch_policy.cc
  src/runtime/http_client_policy.cc
  src/runtime/memory_limit.cc
  src/runtime/managed_process.cc
  src/runtime/owned_pthread.cc
  src/runtime/performance_policy.cc
  src/runtime/platform_cache_migration.cc
  src/runtime/project_safety_guard.cc
  src/runtime/roblox_desktop_app_policy.cc
  src/runtime/roblox_app_lifecycle.cc
  src/runtime/roblox_browser_service_bridge.cc
  src/runtime/roblox_capability_resolver.cc
  src/runtime/roblox_experience_launch_bridge.cc
  src/runtime/roblox_launch_uri.cc
  src/runtime/roblox_presence_resolver.cc
  src/runtime/roblox_game_session_native_adapter.cc
  src/runtime/roblox_web_view_bridge.cc
  src/runtime/roblox_permissions_bridge.cc
  src/runtime/roblox_message_bus_request_bridge.cc
  src/runtime/roblox_call_protocol_bridge.cc
  src/runtime/payload_update_preflight.cc
  src/runtime/process_launch_policy.cc
  src/runtime/process_diagnostics.cc
  src/runtime/runtime_config.cc
  src/runtime/runtime_config_bootstrap.cc
  src/runtime/runtime_config_file.cc
  src/runtime/runtime_paths.cc
  src/runtime/session_log.cc
  src/runtime/single_instance_lock.cc
  src/runtime/startup_state_machine.cc
  src/runtime/supported_launch_policy.cc
  src/runtime/support_bundle.cc
  src/runtime/system_proxy.cc
  src/runtime/texture_memory_policy.cc
)
add_library(Aurora::Runtime ALIAS aurora_runtime)
target_include_directories(aurora_runtime PUBLIC include)
target_include_directories(aurora_runtime PUBLIC
  ${AURORA_JNI_INCLUDE_DIRS}
)
target_compile_definitions(aurora_runtime PRIVATE
  "AURORA_DISCORD_APPLICATION_ID=\"${AURORA_DISCORD_APPLICATION_ID}\""
)
set_property(SOURCE src/runtime/session_log.cc APPEND PROPERTY
  COMPILE_DEFINITIONS
  "AURORA_PROJECT_VERSION=\"${PROJECT_VERSION}\""
  "AURORA_BUILD_GIT_COMMIT=\"${AURORA_BUILD_GIT_COMMIT}\""
  "AURORA_BUILD_GIT_DIRTY=\"${AURORA_BUILD_GIT_DIRTY}\""
  "AURORA_BUILD_TYPE=\"$<CONFIG>\""
  "AURORA_BUILD_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\""
  "AURORA_BUILD_COMPILER_VERSION=\"${CMAKE_CXX_COMPILER_VERSION}\""
  "AURORA_BUILD_TARGET_SYSTEM=\"${CMAKE_SYSTEM_NAME}\""
  "AURORA_BUILD_TARGET_PROCESSOR=\"${CMAKE_SYSTEM_PROCESSOR}\""
)
aurora_apply_compile_options(aurora_runtime)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
  # GCC 16 reports a false array-bounds warning in nlohmann::json::dump().
  set_property(SOURCE src/runtime/roblox_launch_uri.cc APPEND PROPERTY
    COMPILE_OPTIONS -Wno-array-bounds
  )
endif()
target_link_libraries(aurora_runtime PUBLIC Threads::Threads)
target_link_libraries(aurora_runtime PRIVATE
  CURL::libcurl
  nlohmann_json::nlohmann_json
  PkgConfig::GIO
  PkgConfig::LIBYAML
  ${CMAKE_DL_LIBS}
)

# Keep GTK out of the process that loads Android libraries.
add_library(aurora_failure_dialog_runtime STATIC
  src/runtime/failure_dialog.cc
)
add_library(Aurora::FailureDialog ALIAS aurora_failure_dialog_runtime)
target_include_directories(aurora_failure_dialog_runtime PUBLIC include)
target_compile_definitions(aurora_failure_dialog_runtime PRIVATE
  "AURORA_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\""
)
aurora_apply_compile_options(aurora_failure_dialog_runtime)

add_executable(aurora_failure_dialog
  src/ui/aurora_failure_dialog.cc
)
target_link_libraries(aurora_failure_dialog PRIVATE PkgConfig::LIBADWAITA)
aurora_apply_compile_options(aurora_failure_dialog)

add_executable(aurora_settings src/ui/aurora_settings.cc)
target_link_libraries(aurora_settings PRIVATE
  Aurora::Runtime
  PkgConfig::LIBADWAITA
)
aurora_apply_compile_options(aurora_settings)

add_library(aurora_services STATIC
  src/services/http_client.cc
  src/services/auth_service.cc
  src/services/browser_tracker_service.cc
  src/services/client_settings_service.cc
)
add_library(Aurora::Services ALIAS aurora_services)
target_include_directories(aurora_services PUBLIC include)
target_link_libraries(aurora_services PUBLIC
  aurora_runtime
  CURL::libcurl
  nlohmann_json::nlohmann_json
)
aurora_apply_compile_options(aurora_services)

add_library(aurora_webview_launcher STATIC
  src/runtime/webview_helper_launcher.cc
)
add_library(Aurora::WebViewLauncher ALIAS aurora_webview_launcher)
target_include_directories(aurora_webview_launcher PUBLIC include)
target_link_libraries(aurora_webview_launcher PUBLIC CURL::libcurl)
aurora_apply_compile_options(aurora_webview_launcher)

add_library(aurora_webview_helper_policy STATIC
  src/webview/webview_helper_policy.cc
)
add_library(Aurora::WebViewHelperPolicy ALIAS aurora_webview_helper_policy)
target_include_directories(aurora_webview_helper_policy PUBLIC src)
target_link_libraries(aurora_webview_helper_policy PUBLIC
  Aurora::Runtime
  PkgConfig::WEBKITGTK6
  nlohmann_json::nlohmann_json
)
aurora_apply_compile_options(aurora_webview_helper_policy)

add_executable(aurora_webview_helper
  src/webview/aurora_webview_helper.cc
)
target_link_libraries(aurora_webview_helper PRIVATE
  Aurora::WebViewLauncher
  Aurora::WebViewHelperPolicy
  Aurora::Runtime
  PkgConfig::GTK4
  PkgConfig::WEBKITGTK6
  PkgConfig::WEBVIEW_FONTCONFIG
)
aurora_apply_compile_options(aurora_webview_helper)

add_library(aurora_auth_runtime_composition STATIC
  src/runtime/auth_runtime_composition.cc
  src/runtime/webview_roblox_cookie.cc
)
add_library(Aurora::AuthRuntimeComposition ALIAS
  aurora_auth_runtime_composition)
target_include_directories(aurora_auth_runtime_composition PUBLIC include)
target_link_libraries(aurora_auth_runtime_composition PUBLIC
  aurora_legacy_jnivm
  aurora_runtime
  aurora_services
)
aurora_apply_compile_options(aurora_auth_runtime_composition)

add_library(aurora_linker STATIC
  src/linker/linker.cc
)
target_include_directories(aurora_linker PUBLIC include)
target_link_libraries(aurora_linker PUBLIC
  dl
  linker
  aurora_compat
  aurora_bionic_stdio_runtime
  aurora_bionic_abi_exports
)
aurora_apply_compile_options(aurora_linker)
target_compile_definitions(aurora_linker PUBLIC AURORA_USE_BIONIC_LINKER=1)
# The Bionic linker expects IPLT boundaries that host executables do not have.
target_link_options(aurora_linker INTERFACE
  -Wl,--defsym=__rela_iplt_start=0
  -Wl,--defsym=__rela_iplt_end=0
)

add_library(libc_shim STATIC
  src/libc_shim/libc_shim.cc
)
target_include_directories(libc_shim PUBLIC include)
aurora_apply_compile_options(libc_shim)
target_link_libraries(aurora_linker PUBLIC libc_shim)

add_subdirectory(stubs)
foreach(stub_target IN ITEMS
    stub_sys_m stub_sys_z stub_sys_dl stub_sys_c
    stub_android stub_log stub_mediandk stub_openmax
    stub_egl stub_glesv2 stub_vulkan)
  aurora_apply_compile_options(${stub_target})
endforeach()

add_library(aurora_window STATIC
  src/window/roblox_fullscreen_menu_request_gate.cc
  src/window/video_driver_policy.cc
  src/window/window.cc
  src/window/window_creation_policy.cc
  src/window/window_fullscreen_request_gate.cc
  src/window/window_game_surface_bridge.cc
  src/window/window_pointer_capture_owner.cc
  src/window/window_resize_readiness_gate.cc
  src/window/window_state_store.cc
  src/window/window_surface_lifecycle.cc
  src/window/window_text_input_owner.cc
)
target_include_directories(aurora_window PUBLIC src)
aurora_apply_compile_options(aurora_window)
target_link_libraries(aurora_window PUBLIC
  Aurora::PlatformSdl
  Aurora::SdlVulkanWsi
  SDL3::SDL3
  ${CMAKE_DL_LIBS}
  PRIVATE
    Aurora::Runtime
    Aurora::FailureDialog
    Aurora::GlesTextOverlay
    Aurora::GraphicsFoundation
)

add_library(aurora_roblox_output_device_runtime STATIC
  src/audio/roblox_output_device_bridge.cc
)
target_include_directories(aurora_roblox_output_device_runtime PUBLIC include)
target_include_directories(aurora_roblox_output_device_runtime PRIVATE src)
target_link_libraries(aurora_roblox_output_device_runtime PUBLIC
  aurora_compat
  Aurora::AudioSdl
  PRIVATE aurora_linker
)
aurora_apply_compile_options(aurora_roblox_output_device_runtime)

add_library(aurora_roblox_fullscreen_runtime STATIC
  src/runtime/roblox_fullscreen_runtime_bridge.cc
)
target_include_directories(aurora_roblox_fullscreen_runtime PUBLIC include)
target_include_directories(aurora_roblox_fullscreen_runtime PRIVATE src)
target_link_libraries(aurora_roblox_fullscreen_runtime PUBLIC
  aurora_compat
  PRIVATE
    aurora_linker
    aurora_window
)
aurora_apply_compile_options(aurora_roblox_fullscreen_runtime)

include(${CMAKE_SOURCE_DIR}/cmake/AuroraInput.cmake)

add_library(aurora_experience_runtime STATIC
  src/runtime/jnivm_platform_web_callbacks.cc
  src/runtime/roblox_experience_composition.cc
  src/runtime/roblox_fresh_game_launch_controller.cc
  src/runtime/roblox_platform_web_symbols.cc
)
add_library(Aurora::ExperienceRuntime ALIAS aurora_experience_runtime)
target_include_directories(aurora_experience_runtime PUBLIC include)
target_include_directories(aurora_experience_runtime PRIVATE src)
target_link_libraries(aurora_experience_runtime PUBLIC
  Aurora::Runtime
  Aurora::WebViewLauncher
  PkgConfig::UTF8PROC
  PRIVATE
    Aurora::AuthRuntimeComposition
    Aurora::LegacyJni
    aurora_linker
    aurora_window
    PkgConfig::GIO
)
aurora_apply_compile_options(aurora_experience_runtime)

# Compatibility exports still used by the old startup path.
add_library(aurora_bionic_abi_exports STATIC
  src/compat/bionic_abi_exports.cc
  src/compat/http_client_spin_guard.cc
)
target_include_directories(aurora_bionic_abi_exports
  PUBLIC include
  PRIVATE src
)
aurora_apply_compile_options(aurora_bionic_abi_exports)
# These exported Bionic FORTIFY symbols intentionally overlap glibc's
# __*_chk entry points.  When the bridge is visible to whole-program LTO,
# GCC can fold a fortified host call back into its own compatibility wrapper
# and turn the resulting tail recursion into an infinite loop.  Keep this
# translation unit opaque to interprocedural optimization and make its calls
# to the host C library explicit external calls.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_property(SOURCE src/compat/bionic_abi_exports.cc APPEND PROPERTY
    COMPILE_OPTIONS
      -fno-lto
      -fno-builtin
  )
endif()
target_link_libraries(aurora_bionic_abi_exports PRIVATE
  aurora_bionic_stdio_runtime
  aurora_window
  Threads::Threads
)

include(CheckCXXSourceCompiles)
include(CheckIncludeFileCXX)
include(CMakePushCheckState)

check_include_file_cxx(execinfo.h AURORA_HAVE_EXECINFO_H)
if(NOT AURORA_HAVE_EXECINFO_H)
  message(FATAL_ERROR
    "execinfo.h is required for crash diagnostics; on musl install "
    "libexecinfo-devel"
  )
endif()

set(AURORA_EXECINFO_LINK_LIBRARY "")
set(_aurora_execinfo_link_probe [=[
  #include <execinfo.h>
  int main() {
    void* frames[1] = {};
    const int count = backtrace(frames, 1);
    return backtrace_symbols(frames, count) == nullptr;
  }
]=])
check_cxx_source_compiles(
  "${_aurora_execinfo_link_probe}"
  AURORA_EXECINFO_IN_HOST_LIBC
)
if(NOT AURORA_EXECINFO_IN_HOST_LIBC)
  find_library(AURORA_EXECINFO_LIBRARY NAMES execinfo REQUIRED)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES "${AURORA_EXECINFO_LIBRARY}")
  check_cxx_source_compiles(
    "${_aurora_execinfo_link_probe}"
    AURORA_EXECINFO_IN_EXTERNAL_LIBRARY
  )
  cmake_pop_check_state()
  if(NOT AURORA_EXECINFO_IN_EXTERNAL_LIBRARY)
    message(FATAL_ERROR
      "execinfo.h is available, but backtrace and backtrace_symbols cannot "
      "be linked"
    )
  endif()
  set(AURORA_EXECINFO_LINK_LIBRARY "${AURORA_EXECINFO_LIBRARY}")
endif()
unset(_aurora_execinfo_link_probe)

add_library(aurora_legacy_runtime STATIC
  src/legacy/legacy_runtime.cc
  src/runtime/roblox_text_input_jni_bridge.cc
)
target_include_directories(aurora_legacy_runtime PRIVATE include src)
aurora_apply_compile_options(aurora_legacy_runtime)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Diagnostics from signal handlers cannot handle a failed write.
  set_property(SOURCE src/legacy/legacy_runtime.cc APPEND PROPERTY
    COMPILE_OPTIONS -Wno-unused-result
  )
endif()
target_link_libraries(aurora_legacy_runtime PRIVATE
  aurora_legacy_jnivm
  aurora_compat
  aurora_runtime
  aurora_input_runtime
  aurora_experience_runtime
  aurora_services
  aurora_linker
  aurora_bionic_abi_exports
  libc_shim
  aurora_window
  aurora_graphics_foundation
  ${AURORA_EXECINFO_LINK_LIBRARY}
)
target_compile_definitions(aurora_legacy_runtime PRIVATE
  "AURORA_DEFAULT_COMPATIBILITY_MANIFEST=\"${AURORA_DEFAULT_COMPATIBILITY_MANIFEST}\""
)

add_executable(aurora src/main.cc)
target_include_directories(aurora PRIVATE include src)
aurora_apply_compile_options(aurora)
target_compile_definitions(aurora PRIVATE
  "AURORA_DEFAULT_COMPATIBILITY_MANIFEST=\"${AURORA_DEFAULT_COMPATIBILITY_MANIFEST}\""
)
target_link_libraries(aurora PRIVATE
  aurora_legacy_runtime
  aurora_auth_runtime_composition
  aurora_failure_dialog_runtime
  aurora_fmod_jni_audio_bridge
  aurora_roblox_output_device_runtime
  aurora_roblox_fullscreen_runtime
  aurora_window
  libc_shim
)

# Libraries loaded with dlopen() need the stub symbols.
target_link_options(aurora PRIVATE
  -Wl,--export-dynamic
)

# Aurora Player is the native Linux Roblox runtime and player-side control
# plane. Studio code lives in the separate Aurora Studio repository.
add_executable(aurora_player src/player/aurora_player.cc)
aurora_apply_compile_options(aurora_player)

add_executable(aurora_mcp src/mcp/aurora_mcp.cc)
target_link_libraries(aurora_mcp PRIVATE
  Aurora::Runtime
  nlohmann_json::nlohmann_json
)
aurora_apply_compile_options(aurora_mcp)

# Native FreeBSD helper for Linuxulator's unsupported DF socket option.
# The Linuxulator helper contains x86_64 assembly and cannot build on AArch64.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(AURORA_FREEBSD_SOCKET_HELPER_DEFAULT ON)
else()
  set(AURORA_FREEBSD_SOCKET_HELPER_DEFAULT OFF)
endif()
option(AURORA_BUILD_FREEBSD_SOCKET_HELPER
  "Build the x86_64-only FreeBSD Linuxulator socket helper"
  ${AURORA_FREEBSD_SOCKET_HELPER_DEFAULT})
if(AURORA_BUILD_FREEBSD_SOCKET_HELPER AND
   NOT AURORA_FREEBSD_SOCKET_HELPER_DEFAULT)
  message(FATAL_ERROR
    "The FreeBSD Linuxulator socket helper requires an x86_64 build target")
endif()
if(AURORA_BUILD_FREEBSD_SOCKET_HELPER)
find_program(AURORA_LLD_EXECUTABLE NAMES ld.lld REQUIRED)
find_program(AURORA_ELFEDIT_EXECUTABLE NAMES elfedit REQUIRED)
get_filename_component(AURORA_LLD_DIRECTORY
  "${AURORA_LLD_EXECUTABLE}" DIRECTORY)
set(AURORA_FREEBSD_SOCKET_HELPER
  "${CMAKE_BINARY_DIR}/aurora_freebsd_socket_helper")
add_custom_command(
  OUTPUT "${AURORA_FREEBSD_SOCKET_HELPER}"
  COMMAND "${CMAKE_CXX_COMPILER}"
    -x assembler-with-cpp
    -nostdlib
    -static
    -no-pie
    "-B${AURORA_LLD_DIRECTORY}/"
    -fuse-ld=lld
    -Wl,-e,_start,--build-id=none,-z,noexecstack
    -o "${AURORA_FREEBSD_SOCKET_HELPER}"
    "${CMAKE_SOURCE_DIR}/src/compat/freebsd_socket_helper.S"
  COMMAND "${AURORA_ELFEDIT_EXECUTABLE}"
    --output-osabi FreeBSD "${AURORA_FREEBSD_SOCKET_HELPER}"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/compat/freebsd_socket_helper.S"
  COMMENT "Building native FreeBSD Linuxulator socket helper"
  VERBATIM
)
add_custom_target(aurora_freebsd_socket_helper_build ALL
  DEPENDS "${AURORA_FREEBSD_SOCKET_HELPER}"
)
add_dependencies(aurora aurora_freebsd_socket_helper_build)
endif()

set_target_properties(aurora PROPERTIES
  BUILD_RPATH   "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/aurora"
)

set(AURORA_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_LIBDIR}/aurora")
set(AURORA_INSTALL_DATA_DIR "${CMAKE_INSTALL_DATADIR}/aurora")

install(TARGETS aurora aurora_player aurora_mcp aurora_settings
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(TARGETS
    aurora
    aurora_updater
    aurora_failure_dialog
    aurora_webview_helper
    aurora_audio_sdl
    aurora_opensles
    stub_sys_m
    stub_sys_z
    stub_sys_dl
    stub_sys_c
    stub_android
    stub_log
    stub_mediandk
    stub_openmax
    stub_egl
    stub_glesv2
    stub_vulkan
  RUNTIME DESTINATION "${AURORA_INSTALL_RUNTIME_DIR}"
  LIBRARY DESTINATION "${AURORA_INSTALL_RUNTIME_DIR}"
)
if(AURORA_BUILD_FREEBSD_SOCKET_HELPER)
install(PROGRAMS "${AURORA_FREEBSD_SOCKET_HELPER}"
  DESTINATION "${AURORA_INSTALL_RUNTIME_DIR}"
)
endif()
install(FILES
    config/roblox_compatibility.json
    config/roblox_bootstrap_sources.json
    config/roblox_host_abi_reference.json
    config/roblox_signing_certificates.json
  DESTINATION "${AURORA_INSTALL_DATA_DIR}/metadata"
)
install(FILES TERMS.md PRIVACY.md LICENSE LICENSES/PROVENANCE.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/aurora-player"
)
file(GLOB_RECURSE AURORA_THIRD_PARTY_NOTICE_FILES
  LIST_DIRECTORIES false
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/third_party/LICENSE*"
  "${CMAKE_SOURCE_DIR}/third_party/NOTICE*"
  "${CMAKE_SOURCE_DIR}/third_party/COPYING*"
  "${CMAKE_SOURCE_DIR}/third_party/*/LICENSES/*"
)
foreach(notice_file IN LISTS AURORA_THIRD_PARTY_NOTICE_FILES)
  file(RELATIVE_PATH notice_relative
    "${CMAKE_SOURCE_DIR}/third_party" "${notice_file}")
  get_filename_component(notice_directory "${notice_relative}" DIRECTORY)
  install(FILES "${notice_file}"
    DESTINATION
      "${CMAKE_INSTALL_DATADIR}/doc/aurora-player/third-party/${notice_directory}"
  )
endforeach()
install(FILES third_party/jni/ASSEMBLY_EXCEPTION
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/aurora-player/third-party/jni"
)
install(FILES packaging/space.bigrat.aurora.desktop
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES packaging/space.bigrat.aurora.metainfo.xml
  DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
)
install(FILES packaging/space.bigrat.aurora.svg
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)
set(AURORA_HICOLOR_ICON_SIZES
  16 22 24 32 36 48 64 72 96 128 192 256 512
)
# Generated icons are committed, so ImageMagick is not a build dependency.
foreach(icon_name IN ITEMS space.bigrat.aurora)
  foreach(icon_size IN LISTS AURORA_HICOLOR_ICON_SIZES)
    install(FILES
        "packaging/icons/hicolor/${icon_size}x${icon_size}/apps/${icon_name}.png"
      DESTINATION
        "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps"
    )
  endforeach()
endforeach()
install(CODE "
  if(NOT DEFINED ENV{DESTDIR} OR \"\$ENV{DESTDIR}\" STREQUAL \"\")
    find_program(AURORA_GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
    if(AURORA_GTK_UPDATE_ICON_CACHE)
      execute_process(
        COMMAND \"\${AURORA_GTK_UPDATE_ICON_CACHE}\" --force
                --ignore-theme-index
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor\"
        RESULT_VARIABLE AURORA_ICON_CACHE_RESULT
      )
      if(NOT AURORA_ICON_CACHE_RESULT EQUAL 0)
        message(WARNING \"Failed to refresh the hicolor icon cache\")
      endif()
    endif()
    find_program(AURORA_UPDATE_DESKTOP_DATABASE update-desktop-database)
    if(AURORA_UPDATE_DESKTOP_DATABASE)
      execute_process(
        COMMAND \"\${AURORA_UPDATE_DESKTOP_DATABASE}\" -q
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications\"
        RESULT_VARIABLE AURORA_DESKTOP_DATABASE_RESULT
      )
      if(NOT AURORA_DESKTOP_DATABASE_RESULT EQUAL 0)
        message(WARNING \"Failed to refresh the desktop MIME cache\")
      endif()
    endif()
  endif()
")

set(AURORA_PACKAGE_NAME "aurora-player" CACHE STRING
  "Native package name"
)
set(AURORA_PACKAGE_VERSION_SUFFIX "" CACHE STRING
  "Native package version suffix"
)
if(NOT AURORA_PACKAGE_NAME STREQUAL "aurora-player" AND
   NOT AURORA_PACKAGE_NAME STREQUAL "aurora-player-nightly")
  message(FATAL_ERROR
    "AURORA_PACKAGE_NAME must be aurora-player or aurora-player-nightly")
endif()

set(CPACK_PACKAGE_NAME "${AURORA_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}${AURORA_PACKAGE_VERSION_SUFFIX}")
set(CPACK_PACKAGE_VENDOR "Aurora Player Project")
set(CPACK_PACKAGE_CONTACT "Namnarak")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Namnarak/Aurora-Player")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION "Runs the Android Roblox client on Linux.\n\nLicense: Apache-2.0.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES TRUE)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Namnarak")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Runs the Android Roblox client on Linux.\n\nLicense: Apache-2.0.")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0 AND MIT AND BSD-2-Clause AND BSD-2-Clause-FreeBSD AND BSD-3-Clause AND BSD-4-Clause AND GPL-2.0-only WITH Classpath-exception-2.0")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

if(AURORA_PACKAGE_NAME STREQUAL "aurora-player-nightly")
  set(CPACK_DEBIAN_PACKAGE_CONFLICTS "aurora-player")
  set(CPACK_DEBIAN_PACKAGE_REPLACES "aurora-player")
  set(CPACK_RPM_PACKAGE_CONFLICTS "aurora-player")
else()
  set(CPACK_DEBIAN_PACKAGE_CONFLICTS "aurora-player-nightly")
  set(CPACK_DEBIAN_PACKAGE_REPLACES "aurora-player-nightly")
  set(CPACK_RPM_PACKAGE_CONFLICTS "aurora-player-nightly")
endif()
include(CPack)

add_dependencies(aurora all_stubs)
add_dependencies(aurora aurora_failure_dialog)
add_dependencies(aurora aurora_webview_helper)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
