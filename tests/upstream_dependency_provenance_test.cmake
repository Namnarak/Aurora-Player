# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED AURORA_SOURCE_DIR)
  message(FATAL_ERROR "AURORA_SOURCE_DIR is required")
endif()

find_package(Git REQUIRED)
set(manifest "${AURORA_SOURCE_DIR}/config/upstream_dependencies.json")
file(READ "${manifest}" manifest_json)
string(JSON schema_version GET "${manifest_json}" schema_version)
if(NOT schema_version EQUAL 1)
  message(FATAL_ERROR "unsupported upstream dependency manifest schema")
endif()

function(read_dependency_field output dependency field)
  string(JSON value GET "${manifest_json}" dependencies "${dependency}" "${field}")
  set("${output}" "${value}" PARENT_SCOPE)
endfunction()

function(require_git_object_id field_name value)
  string(LENGTH "${value}" value_length)
  if(NOT value_length EQUAL 40 OR NOT value MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "${field_name} must be a full lowercase Git object ID")
  endif()
endfunction()

function(require_sha256 field_name value)
  string(LENGTH "${value}" value_length)
  if(NOT value_length EQUAL 64 OR NOT value MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "${field_name} must be a lowercase SHA-256 digest")
  endif()
endfunction()

read_dependency_field(libjnivm_mode libjnivm mode)
read_dependency_field(libjnivm_url libjnivm url)
read_dependency_field(libjnivm_commit libjnivm commit)
read_dependency_field(libjnivm_tree libjnivm tree)
read_dependency_field(vulkan_mode Vulkan-Headers mode)
read_dependency_field(vulkan_url Vulkan-Headers url)
read_dependency_field(vulkan_commit Vulkan-Headers commit)
read_dependency_field(vulkan_tree Vulkan-Headers tree)
read_dependency_field(linker_mode mcpelauncher-linker mode)
read_dependency_field(linker_url mcpelauncher-linker url)
read_dependency_field(linker_upstream_commit mcpelauncher-linker upstream_commit)
read_dependency_field(linker_tree mcpelauncher-linker vendor_tree)
read_dependency_field(linker_patch mcpelauncher-linker patch)
string(JSON linker_bionic_url GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_bionic url)
string(JSON linker_bionic_commit GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_bionic upstream_commit)
string(JSON linker_bionic_tree GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_bionic vendor_tree)
string(JSON linker_core_mode GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_core mode)
string(JSON linker_core_url GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_core url)
string(JSON linker_core_commit GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_core upstream_commit)
string(JSON linker_core_upstream_tree GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_core upstream_tree)
string(JSON linker_core_tree GET "${manifest_json}" dependencies
       mcpelauncher-linker nested_sources android_core vendor_tree)
foreach(core_subtree IN ITEMS base libcutils liblog libziparchive)
  string(JSON linker_core_${core_subtree}_tree GET "${manifest_json}"
         dependencies mcpelauncher-linker nested_sources android_core
         vendored_subtrees "${core_subtree}")
endforeach()

foreach(pair IN ITEMS
    "libjnivm|${libjnivm_commit}|${libjnivm_tree}"
    "Vulkan-Headers|${vulkan_commit}|${vulkan_tree}"
    "mcpelauncher-linker|${linker_upstream_commit}|${linker_tree}"
    "android_bionic|${linker_bionic_commit}|${linker_bionic_tree}"
    "android_core|${linker_core_commit}|${linker_core_tree}"
    "android_core upstream tree|${linker_core_commit}|${linker_core_upstream_tree}")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 label)
  list(GET fields 1 commit)
  list(GET fields 2 tree)
  require_git_object_id("${label} upstream commit" "${commit}")
  require_git_object_id("${label} snapshot tree" "${tree}")
endforeach()

if(NOT libjnivm_mode STREQUAL "vendored-unmodified-source-snapshot" OR
   NOT vulkan_mode STREQUAL "vendored-unmodified-source-snapshot" OR
   NOT linker_mode STREQUAL "vendored-patched-snapshot" OR
   NOT linker_core_mode STREQUAL "vendored-unmodified-subtree-snapshot")
  message(FATAL_ERROR "dependency ownership modes do not match the flattened source layout")
endif()
if(NOT libjnivm_url STREQUAL "https://github.com/ChristopherHX/libjnivm.git" OR
   NOT vulkan_url STREQUAL "https://github.com/KhronosGroup/Vulkan-Headers.git" OR
   NOT linker_url STREQUAL "https://github.com/minecraft-linux/mcpelauncher-linker.git" OR
   NOT linker_bionic_url STREQUAL "https://github.com/minecraft-linux/android_bionic.git" OR
   NOT linker_core_url STREQUAL "https://github.com/minecraft-linux/android_core")
  message(FATAL_ERROR "upstream dependency URL differs from the supported lock")
endif()

if(EXISTS "${AURORA_SOURCE_DIR}/.gitmodules" OR
   EXISTS "${AURORA_SOURCE_DIR}/third_party/libjnivm/.git" OR
   EXISTS "${AURORA_SOURCE_DIR}/third_party/Vulkan-Headers/.git" OR
   EXISTS "${AURORA_SOURCE_DIR}/third_party/mcpelauncher-linker/.git" OR
   EXISTS "${AURORA_SOURCE_DIR}/third_party/mcpelauncher-linker/core/.git")
  message(FATAL_ERROR "flattened upstream snapshots must not contain local Git metadata")
endif()

foreach(jni_file IN ITEMS
    third_party/jni/include/jni.h
    third_party/jni/include/jni_md.h
    third_party/jni/LICENSE
    third_party/jni/ASSEMBLY_EXCEPTION)
  string(JSON expected_sha256 GET "${manifest_json}" dependencies
         OpenJDK-JNI-Headers files "${jni_file}" sha256)
  require_sha256("OpenJDK JNI ${jni_file}" "${expected_sha256}")
  set(jni_path "${AURORA_SOURCE_DIR}/${jni_file}")
  if(NOT EXISTS "${jni_path}")
    message(FATAL_ERROR "standalone JNI snapshot is missing ${jni_file}")
  endif()
  file(SHA256 "${jni_path}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "standalone JNI snapshot differs: ${jni_file}")
  endif()
endforeach()

string(JSON jni_headers_mode GET "${manifest_json}" dependencies OpenJDK-JNI-Headers mode)
string(JSON jni_headers_url GET "${manifest_json}" dependencies OpenJDK-JNI-Headers url)
string(JSON jni_headers_tag GET "${manifest_json}" dependencies OpenJDK-JNI-Headers tag)
string(JSON jni_headers_commit GET "${manifest_json}" dependencies OpenJDK-JNI-Headers upstream_commit)
require_git_object_id("OpenJDK JNI headers upstream commit" "${jni_headers_commit}")
if(NOT jni_headers_mode STREQUAL "vendored-unmodified-header-snapshot" OR
   NOT jni_headers_url STREQUAL "https://github.com/openjdk/jdk17u.git" OR
   NOT jni_headers_tag STREQUAL "jdk-17.0.20+8")
  message(FATAL_ERROR "standalone JNI header provenance differs")
endif()

string(JSON angle_mode GET "${manifest_json}" dependencies ANGLE-Headers mode)
string(JSON angle_url GET "${manifest_json}" dependencies ANGLE-Headers url)
string(JSON angle_commit GET "${manifest_json}" dependencies ANGLE-Headers upstream_commit)
string(JSON angle_tree GET "${manifest_json}" dependencies ANGLE-Headers upstream_tree)
string(JSON angle_header_blob GET "${manifest_json}" dependencies ANGLE-Headers files
       include/EGL/eglext_angle.h git_blob)
string(JSON angle_header_sha256 GET "${manifest_json}" dependencies ANGLE-Headers files
       include/EGL/eglext_angle.h sha256)
string(JSON angle_license_blob GET "${manifest_json}" dependencies ANGLE-Headers files
       LICENSE git_blob)
string(JSON angle_license_sha256 GET "${manifest_json}" dependencies ANGLE-Headers files
       LICENSE sha256)
require_git_object_id("ANGLE-Headers upstream commit" "${angle_commit}")
require_git_object_id("ANGLE-Headers upstream tree" "${angle_tree}")
require_git_object_id("ANGLE header blob" "${angle_header_blob}")
require_git_object_id("ANGLE license blob" "${angle_license_blob}")
if(NOT angle_mode STREQUAL "vendored-unmodified-header-snapshot" OR
   NOT angle_url STREQUAL "https://chromium.googlesource.com/angle/angle.git")
  message(FATAL_ERROR "ANGLE-Headers canonical upstream provenance differs")
endif()
set(angle_root "${AURORA_SOURCE_DIR}/third_party/angle_headers")
foreach(relative_file IN ITEMS include/EGL/eglext_angle.h LICENSE)
  if(NOT EXISTS "${angle_root}/${relative_file}")
    message(FATAL_ERROR "vendored ANGLE snapshot is missing ${relative_file}")
  endif()
endforeach()
file(SHA256 "${angle_root}/include/EGL/eglext_angle.h" checkout_angle_header_sha256)
file(SHA256 "${angle_root}/LICENSE" checkout_angle_license_sha256)
if(NOT checkout_angle_header_sha256 STREQUAL angle_header_sha256 OR
   NOT checkout_angle_license_sha256 STREQUAL angle_license_sha256)
  message(FATAL_ERROR "vendored ANGLE header snapshot differs from provenance")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" hash-object "${angle_root}/include/EGL/eglext_angle.h"
  RESULT_VARIABLE angle_header_blob_result OUTPUT_VARIABLE checkout_angle_header_blob
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${GIT_EXECUTABLE}" hash-object "${angle_root}/LICENSE"
  RESULT_VARIABLE angle_license_blob_result OUTPUT_VARIABLE checkout_angle_license_blob
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT angle_header_blob_result EQUAL 0 OR
   NOT checkout_angle_header_blob STREQUAL angle_header_blob OR
   NOT angle_license_blob_result EQUAL 0 OR
   NOT checkout_angle_license_blob STREQUAL angle_license_blob)
  message(FATAL_ERROR "vendored ANGLE Git blobs differ from provenance")
endif()

set(snapshot_root "${CMAKE_CURRENT_BINARY_DIR}/upstream-snapshot-provenance")
file(REMOVE_RECURSE "${snapshot_root}")
file(MAKE_DIRECTORY "${snapshot_root}")
file(COPY "${AURORA_SOURCE_DIR}/third_party" DESTINATION "${snapshot_root}")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${snapshot_root}" init --quiet
  RESULT_VARIABLE init_result)
if(NOT init_result EQUAL 0)
  message(FATAL_ERROR "cannot initialize isolated snapshot verifier")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${snapshot_root}" add -- third_party
  RESULT_VARIABLE add_result)
if(NOT add_result EQUAL 0)
  message(FATAL_ERROR "cannot stage flattened third-party snapshots for verification")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${snapshot_root}" write-tree
  RESULT_VARIABLE write_tree_result OUTPUT_VARIABLE snapshot_root_tree
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT write_tree_result EQUAL 0 OR snapshot_root_tree STREQUAL "")
  message(FATAL_ERROR "cannot materialise flattened third-party snapshot tree")
endif()

function(require_snapshot_tree dependency_path expected_tree)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${snapshot_root}" rev-parse
            "${snapshot_root_tree}:${dependency_path}"
    RESULT_VARIABLE result OUTPUT_VARIABLE actual_tree
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT result EQUAL 0 OR NOT actual_tree STREQUAL expected_tree)
    message(FATAL_ERROR "flattened snapshot tree differs: ${dependency_path}")
  endif()
endfunction()

require_snapshot_tree(third_party/libjnivm "${libjnivm_tree}")
require_snapshot_tree(third_party/Vulkan-Headers "${vulkan_tree}")
require_snapshot_tree(third_party/mcpelauncher-linker "${linker_tree}")
require_snapshot_tree(third_party/mcpelauncher-linker/bionic "${linker_bionic_tree}")
require_snapshot_tree(third_party/mcpelauncher-linker/core "${linker_core_tree}")
foreach(core_subtree IN ITEMS base libcutils liblog libziparchive)
  require_snapshot_tree(
    "third_party/mcpelauncher-linker/core/${core_subtree}"
    "${linker_core_${core_subtree}_tree}")
endforeach()

if(NOT EXISTS "${AURORA_SOURCE_DIR}/${linker_patch}")
  message(FATAL_ERROR "declared mcpelauncher-linker patch is missing")
endif()
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${AURORA_SOURCE_DIR}" apply --reverse --check
          --unidiff-zero --directory=third_party/mcpelauncher-linker
          "${AURORA_SOURCE_DIR}/${linker_patch}"
  RESULT_VARIABLE linker_patch_result OUTPUT_QUIET ERROR_QUIET)
if(NOT linker_patch_result EQUAL 0)
  message(FATAL_ERROR "vendored mcpelauncher-linker does not contain the declared patch")
endif()

message(STATUS "flattened upstream dependency snapshots match the lock manifest")
