# Copyright 2026 Aurora Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(AURORA_PLATFORM_GRAPHICS_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

find_package(SDL3 3.4 REQUIRED CONFIG)
find_path(AURORA_EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
find_path(AURORA_GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)

set(AURORA_WINDOW_ICON_PNG
  "${AURORA_PLATFORM_GRAPHICS_ROOT}/packaging/icons/hicolor/48x48/apps/space.bigrat.aurora.png"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${AURORA_WINDOW_ICON_PNG}"
)
file(READ "${AURORA_WINDOW_ICON_PNG}" AURORA_WINDOW_ICON_HEX HEX)
string(LENGTH "${AURORA_WINDOW_ICON_HEX}" AURORA_WINDOW_ICON_HEX_LENGTH)
set(AURORA_WINDOW_ICON_BYTES "")
set(AURORA_WINDOW_ICON_HEX_OFFSET 0)
set(AURORA_WINDOW_ICON_COLUMN 0)
while(AURORA_WINDOW_ICON_HEX_OFFSET LESS AURORA_WINDOW_ICON_HEX_LENGTH)
  string(SUBSTRING "${AURORA_WINDOW_ICON_HEX}"
    ${AURORA_WINDOW_ICON_HEX_OFFSET} 2 AURORA_WINDOW_ICON_BYTE
  )
  string(APPEND AURORA_WINDOW_ICON_BYTES
    "0x${AURORA_WINDOW_ICON_BYTE}, "
  )
  math(EXPR AURORA_WINDOW_ICON_HEX_OFFSET
    "${AURORA_WINDOW_ICON_HEX_OFFSET} + 2"
  )
  math(EXPR AURORA_WINDOW_ICON_COLUMN
    "${AURORA_WINDOW_ICON_COLUMN} + 1"
  )
  if(AURORA_WINDOW_ICON_COLUMN EQUAL 12)
    string(APPEND AURORA_WINDOW_ICON_BYTES "\n    ")
    set(AURORA_WINDOW_ICON_COLUMN 0)
  endif()
endwhile()

set(AURORA_PLATFORM_GENERATED_INCLUDE_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
file(MAKE_DIRECTORY
  "${AURORA_PLATFORM_GENERATED_INCLUDE_DIR}/aurora/platform"
)
configure_file(
  "${AURORA_PLATFORM_GRAPHICS_ROOT}/cmake/templates/sdl_window_icon_data.h.in"
  "${AURORA_PLATFORM_GENERATED_INCLUDE_DIR}/aurora/platform/sdl_window_icon_data.h"
  @ONLY
)

set(AURORA_ANGLE_HEADERS_INCLUDE_DIR
  "${AURORA_PLATFORM_GRAPHICS_ROOT}/third_party/angle_headers/include"
)
if(NOT EXISTS
    "${AURORA_ANGLE_HEADERS_INCLUDE_DIR}/EGL/eglext_angle.h")
  message(FATAL_ERROR "pinned ANGLE EGL extension header is unavailable")
endif()
add_library(aurora_angle_headers INTERFACE)
target_include_directories(aurora_angle_headers SYSTEM INTERFACE
  "${AURORA_ANGLE_HEADERS_INCLUDE_DIR}"
)
add_library(Aurora::AngleHeaders ALIAS aurora_angle_headers)

add_library(aurora_platform_sdl STATIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_application_metadata.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_display_refresh_capabilities.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_event_converter.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_gamepad_manager.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_platform_runtime.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_text_clipboard.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/platform/sdl_window_icon.cc
)
target_include_directories(aurora_platform_sdl PUBLIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/include
)
target_include_directories(aurora_platform_sdl PRIVATE
  ${AURORA_PLATFORM_GENERATED_INCLUDE_DIR}
)
target_link_libraries(aurora_platform_sdl PUBLIC SDL3::SDL3)
target_compile_features(aurora_platform_sdl PUBLIC cxx_std_17)
target_compile_definitions(aurora_platform_sdl PRIVATE
  AURORA_PROJECT_VERSION="${PROJECT_VERSION}"
)
add_library(Aurora::PlatformSdl ALIAS aurora_platform_sdl)

add_library(aurora_graphics_foundation STATIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/graphics_backend.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/angle_probe.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/bionic_egl_bridge.cc
)
target_include_directories(aurora_graphics_foundation
  PUBLIC ${AURORA_PLATFORM_GRAPHICS_ROOT}/include
  PRIVATE ${AURORA_EGL_INCLUDE_DIR}
)
target_link_libraries(aurora_graphics_foundation PRIVATE
  Aurora::AngleHeaders
  ${CMAKE_DL_LIBS}
)
target_compile_features(aurora_graphics_foundation PUBLIC cxx_std_17)
add_library(Aurora::GraphicsFoundation ALIAS aurora_graphics_foundation)

add_library(aurora_gles_text_overlay STATIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/gles_text_overlay_compositor.cc
)
target_include_directories(aurora_gles_text_overlay
  PUBLIC ${AURORA_PLATFORM_GRAPHICS_ROOT}/include
  PRIVATE ${AURORA_GLES3_INCLUDE_DIR}
)
target_link_libraries(aurora_gles_text_overlay PUBLIC SDL3::SDL3)
target_compile_features(aurora_gles_text_overlay PUBLIC cxx_std_17)
aurora_apply_compile_options(aurora_gles_text_overlay)
add_library(Aurora::GlesTextOverlay ALIAS aurora_gles_text_overlay)

add_library(aurora_sdl_vulkan_wsi STATIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/sdl_vulkan_wsi.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/android_vulkan_wsi_adapter.cc
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/src/graphics/present_mode_policy.cc
)
set_target_properties(aurora_sdl_vulkan_wsi PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(aurora_sdl_vulkan_wsi PUBLIC
  ${AURORA_PLATFORM_GRAPHICS_ROOT}/include
)
target_link_libraries(aurora_sdl_vulkan_wsi PUBLIC SDL3::SDL3)
target_link_libraries(aurora_sdl_vulkan_wsi PUBLIC Vulkan::Headers)
target_compile_features(aurora_sdl_vulkan_wsi PUBLIC cxx_std_17)
add_library(Aurora::SdlVulkanWsi ALIAS aurora_sdl_vulkan_wsi)

if(BUILD_TESTING AND TARGET GTest::gtest_main)
  add_executable(gles_text_overlay_compositor_test
    ${AURORA_PLATFORM_GRAPHICS_ROOT}/tests/gles_text_overlay_compositor_test.cc
  )
  target_include_directories(gles_text_overlay_compositor_test PRIVATE
    ${AURORA_GLES3_INCLUDE_DIR}
  )
  target_link_libraries(gles_text_overlay_compositor_test PRIVATE
    Aurora::GlesTextOverlay
    GTest::gtest_main
  )
  add_executable(bionic_egl_bridge_test
    ${AURORA_PLATFORM_GRAPHICS_ROOT}/tests/bionic_egl_bridge_test.cc
  )
  target_link_libraries(bionic_egl_bridge_test PRIVATE
    Aurora::GraphicsFoundation
    GTest::gtest_main
  )
  add_dependencies(bionic_egl_bridge_test stub_egl)

  add_executable(platform_graphics_foundation_test
    ${AURORA_PLATFORM_GRAPHICS_ROOT}/tests/platform_graphics_foundation_test.cc
  )
  add_executable(display_refresh_capabilities_test
    ${AURORA_PLATFORM_GRAPHICS_ROOT}/tests/display_refresh_capabilities_test.cc
  )
  add_executable(present_mode_policy_test
    ${AURORA_PLATFORM_GRAPHICS_ROOT}/tests/present_mode_policy_test.cc
  )
  target_link_libraries(present_mode_policy_test PRIVATE
    Aurora::SdlVulkanWsi
    GTest::gtest_main
  )
  target_link_libraries(display_refresh_capabilities_test PRIVATE
    Aurora::PlatformSdl
    GTest::gtest_main
  )
  target_link_libraries(platform_graphics_foundation_test PRIVATE
    Aurora::PlatformSdl
    Aurora::GraphicsFoundation
    Aurora::SdlVulkanWsi
    GTest::gtest_main
  )
  include(GoogleTest)
  gtest_discover_tests(gles_text_overlay_compositor_test)
  gtest_discover_tests(bionic_egl_bridge_test)
  gtest_discover_tests(platform_graphics_foundation_test)
  gtest_discover_tests(display_refresh_capabilities_test)
  gtest_discover_tests(present_mode_policy_test)
endif()
