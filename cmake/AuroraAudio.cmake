# Copyright 2026 Aurora Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(AURORA_AUDIO_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

# SDL 3.4 provides the callback needed for OpenSL buffer completion.
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(Threads REQUIRED)

add_library(aurora_audio_core STATIC
  ${AURORA_AUDIO_ROOT}/src/audio/audio_sink.cc
)
target_include_directories(aurora_audio_core PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_compile_features(aurora_audio_core PUBLIC cxx_std_17)
set_target_properties(aurora_audio_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Aurora::AudioCore ALIAS aurora_audio_core)

add_library(aurora_audio_sdl SHARED
  ${AURORA_AUDIO_ROOT}/src/audio/sdl_audio_capture.cc
  ${AURORA_AUDIO_ROOT}/src/audio/sdl_audio_sink.cc
)
target_include_directories(aurora_audio_sdl PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_link_libraries(aurora_audio_sdl PUBLIC
  Aurora::AudioCore
  SDL3::SDL3
)
target_compile_features(aurora_audio_sdl PUBLIC cxx_std_17)
set_target_properties(aurora_audio_sdl PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  BUILD_RPATH_USE_ORIGIN TRUE
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)
add_library(Aurora::AudioSdl ALIAS aurora_audio_sdl)

add_library(aurora_audio_fmod_java_runtime STATIC
  ${AURORA_AUDIO_ROOT}/src/audio/fmod_java_audio_runtime.cc
)
target_include_directories(aurora_audio_fmod_java_runtime PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_link_libraries(aurora_audio_fmod_java_runtime PUBLIC
  Aurora::AudioSdl
  Threads::Threads
)
target_compile_features(aurora_audio_fmod_java_runtime PUBLIC cxx_std_17)
set_target_properties(aurora_audio_fmod_java_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Aurora::FmodJavaAudioRuntime ALIAS
  aurora_audio_fmod_java_runtime
)

add_library(aurora_fmod_jni_audio_bridge STATIC
  ${AURORA_AUDIO_ROOT}/src/audio/fmod_jni_audio_bridge.cc
  ${AURORA_AUDIO_ROOT}/src/audio/webrtc_jni_audio_bridge.cc
)
target_include_directories(aurora_fmod_jni_audio_bridge PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_link_libraries(aurora_fmod_jni_audio_bridge PUBLIC
  Aurora::FmodJavaAudioRuntime
  Aurora::LegacyJni
)
target_compile_features(aurora_fmod_jni_audio_bridge PUBLIC cxx_std_17)
set_target_properties(aurora_fmod_jni_audio_bridge PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Aurora::FmodJniAudioBridge ALIAS
  aurora_fmod_jni_audio_bridge
)

add_library(aurora_audio_opensl_adapter STATIC
  ${AURORA_AUDIO_ROOT}/src/audio/opensl_simple_buffer_queue.cc
)
target_include_directories(aurora_audio_opensl_adapter PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_link_libraries(aurora_audio_opensl_adapter PUBLIC
  Aurora::AudioCore
  Threads::Threads
)
target_compile_features(aurora_audio_opensl_adapter PUBLIC cxx_std_17)
set_target_properties(aurora_audio_opensl_adapter PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
add_library(Aurora::AudioOpenSlAdapter ALIAS
  aurora_audio_opensl_adapter
)

add_library(aurora_audio INTERFACE)
target_link_libraries(aurora_audio INTERFACE
  Aurora::AudioSdl
  Aurora::AudioOpenSlAdapter
)
add_library(Aurora::Audio ALIAS aurora_audio)

# Desktop builds use opensl_abi.h when NDK headers are unavailable.
find_path(AURORA_OPENSL_CORE_INCLUDE_DIR SLES/OpenSLES.h)
find_path(AURORA_OPENSL_ANDROID_INCLUDE_DIR SLES/OpenSLES_Android.h)
if(AURORA_OPENSL_CORE_INCLUDE_DIR AND AURORA_OPENSL_ANDROID_INCLUDE_DIR)
  target_include_directories(aurora_audio_opensl_adapter PUBLIC
    ${AURORA_OPENSL_CORE_INCLUDE_DIR}
    ${AURORA_OPENSL_ANDROID_INCLUDE_DIR}
  )
  target_compile_definitions(aurora_audio_opensl_adapter PUBLIC
    AURORA_USE_SYSTEM_OPENSL_HEADERS=1
  )
  message(STATUS "Aurora audio: using system Android OpenSL headers")
else()
  message(STATUS
    "Aurora audio: Android OpenSL headers unavailable; using minimal queue ABI boundary"
  )
endif()

add_library(aurora_opensles SHARED
  ${AURORA_AUDIO_ROOT}/src/audio/opensl_playback_runtime.cc
)
target_include_directories(aurora_opensles PUBLIC
  ${AURORA_AUDIO_ROOT}/include
)
target_link_libraries(aurora_opensles PRIVATE
  Aurora::Audio
)
target_compile_features(aurora_opensles PRIVATE cxx_std_17)
set_target_properties(aurora_opensles PROPERTIES
  OUTPUT_NAME OpenSLES
  PREFIX "lib"
  SUFFIX ".so"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  BUILD_RPATH_USE_ORIGIN TRUE
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)
target_link_options(aurora_opensles PRIVATE
  "-Wl,-soname,libOpenSLES.so"
  "-Wl,--exclude-libs,ALL"
)

if(BUILD_TESTING AND TARGET GTest::gtest_main)
  add_executable(audio_foundation_test
    ${AURORA_AUDIO_ROOT}/tests/audio_foundation_test.cc
    ${AURORA_AUDIO_ROOT}/stubs/libopensl_stub.cc
  )
  target_link_libraries(audio_foundation_test PRIVATE
    Aurora::Audio
    GTest::gtest_main
  )
  target_compile_features(audio_foundation_test PRIVATE cxx_std_17)
  include(GoogleTest)
  gtest_discover_tests(audio_foundation_test
    PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy"
  )

  add_executable(opensl_playback_runtime_test
    ${AURORA_AUDIO_ROOT}/tests/opensl_playback_runtime_test.cc
  )
  target_link_libraries(opensl_playback_runtime_test PRIVATE
    aurora_opensles
    Aurora::AudioSdl
    GTest::gtest_main
  )
  target_compile_features(opensl_playback_runtime_test PRIVATE cxx_std_17)
  gtest_discover_tests(opensl_playback_runtime_test
    PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy"
  )

  add_executable(fmod_java_audio_runtime_test
    ${AURORA_AUDIO_ROOT}/tests/fmod_java_audio_runtime_test.cc
  )
  target_link_libraries(fmod_java_audio_runtime_test PRIVATE
    Aurora::FmodJavaAudioRuntime
    GTest::gtest_main
    Threads::Threads
  )
  target_compile_features(fmod_java_audio_runtime_test PRIVATE cxx_std_17)
  gtest_discover_tests(fmod_java_audio_runtime_test
    PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy"
  )

  add_executable(fmod_jni_audio_bridge_test
    ${AURORA_AUDIO_ROOT}/tests/fmod_jni_audio_bridge_test.cc
  )
  target_link_libraries(fmod_jni_audio_bridge_test PRIVATE
    Aurora::FmodJniAudioBridge
    GTest::gtest_main
  )
  target_compile_features(fmod_jni_audio_bridge_test PRIVATE cxx_std_17)
  gtest_discover_tests(fmod_jni_audio_bridge_test
    PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy"
  )
endif()
