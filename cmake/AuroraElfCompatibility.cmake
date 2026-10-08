# Copyright 2026 Aurora Project Authors
# Licensed under the Apache License, Version 2.0.

include_guard(GLOBAL)

get_filename_component(AURORA_ELF_COMPAT_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBELF REQUIRED IMPORTED_TARGET libelf)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(aurora_compat STATIC
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_atfork_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_dns_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_host_libc_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_large_file_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_prctl_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_rwlock_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_signal_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_socket_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_semaphore_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_create_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_pthread_key_runtime.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/bionic_sysconf.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/elf_build_id.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/build_profile.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/payload_compatibility.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/host_allocator_bridge.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/host_abi_experiment.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/host_abi_profile.cc
  ${AURORA_ELF_COMPAT_ROOT}/src/compat/host_abi_profile_loader.cc
)
add_library(Aurora::Compat ALIAS aurora_compat)
target_include_directories(aurora_compat PUBLIC
  ${AURORA_ELF_COMPAT_ROOT}/include
)
target_compile_definitions(aurora_compat PRIVATE
  "AURORA_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\""
)
target_link_libraries(aurora_compat PUBLIC
  PkgConfig::LIBELF
  nlohmann_json::nlohmann_json
  Threads::Threads
)
target_link_libraries(aurora_compat PRIVATE OpenSSL::Crypto)
target_compile_features(aurora_compat PUBLIC cxx_std_17)

if(COMMAND aurora_apply_compile_options)
  aurora_apply_compile_options(aurora_compat)
endif()
