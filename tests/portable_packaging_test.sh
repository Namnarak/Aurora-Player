#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

set -euo pipefail

ROOT="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGER="${ROOT}/scripts/package_portable.sh"
BUILD_DIR="${AURORA_PORTABLE_TEST_BUILD_DIR:-${ROOT}/build}"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/aurora-portable-test.XXXXXX")"
trap 'rm -rf -- "${TEMP_DIR}"' EXIT

if ! command -v apksigner >/dev/null 2>&1; then
  echo "apksigner not available, skipping portable packaging test"
  exit 0
fi

dry_run="$(${PACKAGER} --build-dir "${BUILD_DIR}" --libc glibc \
  --mode standalone --dry-run)"
grep -q '^mode=standalone$' <<<"${dry_run}"
grep -q '^libc=glibc$' <<<"${dry_run}"
grep -q '^artifact_name=aurora-linux-x86_64-glibc-standalone$' \
  <<<"${dry_run}"
grep -q '^interpreter=.*/ld-linux-x86-64\.so\.2$' <<<"${dry_run}"
grep -q '^android_tools_libc=glibc$' <<<"${dry_run}"
grep -q '^project_artifacts=17$' <<<"${dry_run}"
grep -q 'host=libc\.so\.6 reason=glibc' <<<"${dry_run}"
grep -q 'host=libEGL\.so\.1 reason=gpu-driver-stack' <<<"${dry_run}"
! grep -q 'host=libwebkitgtk-6\.0\.so\.4' <<<"${dry_run}"
standalone_bundled_count="$(sed -n 's/^bundled_dependency_count=//p' \
  <<<"${dry_run}")"
[[ "${standalone_bundled_count}" =~ ^[0-9]+$ ]]
(( standalone_bundled_count > 0 ))
! grep -q 'libgamemode' <<<"${dry_run}"

mkdir -p "${TEMP_DIR}/no-ldd-bin"
printf '%s\n' '#!/usr/bin/env bash' \
  'touch "${AURORA_LDD_CALLED_MARKER}"' 'exit 99' \
  >"${TEMP_DIR}/no-ldd-bin/ldd"
chmod 0755 "${TEMP_DIR}/no-ldd-bin/ldd"
thin_dry_run="$(env PATH="${TEMP_DIR}/no-ldd-bin:${PATH}" \
  AURORA_LDD_CALLED_MARKER="${TEMP_DIR}/ldd-called" \
  "${PACKAGER}" --build-dir "${BUILD_DIR}" --libc glibc \
    --mode thin --dry-run)"
[[ ! -e "${TEMP_DIR}/ldd-called" ]]
grep -q '^mode=thin$' <<<"${thin_dry_run}"
grep -q '^artifact_name=aurora-linux-x86_64-glibc-thin$' \
  <<<"${thin_dry_run}"
grep -q '^bundled_dependency_count=0$' <<<"${thin_dry_run}"
grep -q 'reason=thin-mode' <<<"${thin_dry_run}"

for alias in full static; do
  alias_output="$(${PACKAGER} --build-dir "${BUILD_DIR}" --libc glibc \
    --mode "${alias}" --dry-run)"
  grep -q '^mode=standalone$' <<<"${alias_output}"
  grep -q '^artifact_name=aurora-linux-x86_64-glibc-standalone$' \
    <<<"${alias_output}"
done
for alias in dynamic minimal; do
  alias_output="$(${PACKAGER} --build-dir "${BUILD_DIR}" --libc glibc \
    --mode "${alias}" --dry-run)"
  grep -q '^mode=thin$' <<<"${alias_output}"
  grep -q '^artifact_name=aurora-linux-x86_64-glibc-thin$' \
    <<<"${alias_output}"
done

set +e
"${PACKAGER}" --build-dir "${BUILD_DIR}" --libc musl --mode standalone \
  --dry-run >"${TEMP_DIR}/musl.stdout" 2>"${TEMP_DIR}/musl.stderr"
musl_status=$?
set -e
[[ "${musl_status}" -ne 0 ]]
grep -Fq 'build artifact aurora libc ABI mismatch: expected musl, detected glibc' \
  "${TEMP_DIR}/musl.stderr"

if "${PACKAGER}" --build-dir "${BUILD_DIR}" --libc bionic --dry-run \
    >/dev/null 2>&1; then
  printf 'packager accepted an unsupported libc ABI\n' >&2
  exit 1
fi
bundle="${TEMP_DIR}/first/aurora-linux-x86_64-glibc-thin"
mkdir -p "${TEMP_DIR}/thin-host-bin"
cat >"${TEMP_DIR}/thin-host-bin/pacman" <<'EOF'
#!/bin/sh
[ "${1:-}" = -Qq ]
EOF
chmod 0755 "${TEMP_DIR}/thin-host-bin/pacman"
printf 'ID=arch\n' >"${TEMP_DIR}/thin-os-release"
thin_host_path="${TEMP_DIR}/thin-host-bin:${PATH}"
PATH="${thin_host_path}" \
  AURORA_OS_RELEASE_FILE="${TEMP_DIR}/thin-os-release" \
  "${PACKAGER}" --build-dir "${BUILD_DIR}" --libc glibc --mode minimal \
    --output "${bundle}"
runtime="${bundle}/aurora"
metadata="${runtime}/metadata"

RunThinLauncher() {
  env PATH="${thin_host_path}" \
    AURORA_OS_RELEASE_FILE="${TEMP_DIR}/thin-os-release" "$@"
}

RefreshBundleChecksums() {
  (
    cd -- "${bundle}"
    find . -type f \
      ! -path './aurora/metadata/SHA256SUMS.txt' -print0 |
      LC_ALL=C sort -z |
      while IFS= read -r -d '' path; do
        sha256sum -- "${path}"
      done
  ) >"${metadata}/SHA256SUMS.txt"
}

for path in \
    run.sh \
    aurora/bin/aurora \
    aurora/bin/aurora_updater \
    aurora/bin/aurora_failure_dialog \
    aurora/bin/aurora_webview_helper \
    aurora/bin/aurora_freebsd_socket_helper \
    aurora/bin/libEGL.so \
    aurora/bin/libvulkan.so \
    aurora/runtime \
    aurora/scripts/portable_launcher.sh \
    aurora/scripts/install_thin_dependencies.sh \
    aurora/scripts/collect_support_bundle.sh \
    aurora/runtime/android-tools/bin/aapt \
    aurora/runtime/android-tools/bin/apksigner \
    aurora/runtime/android-tools/lib/apksigner.jar \
    aurora/runtime/android-tools/licenses/build-tools.txt \
    aurora/metadata/ABI.txt \
    aurora/metadata/DEPENDENCIES.txt \
    aurora/metadata/SHA256SUMS.txt \
    aurora/metadata/roblox_compatibility.json \
    aurora/metadata/roblox_bootstrap_sources.json \
    aurora/metadata/roblox_host_abi_reference.json \
    aurora/metadata/roblox_signing_certificates.json \
    aurora/metadata/.aurora-portable-bundle; do
  [[ -e "${bundle}/${path}" ]] || {
    printf 'portable bundle is missing %s\n' "${path}" >&2
    exit 1
  }
done
[[ ! -e "${runtime}/scripts/aurora_login_webview.py" ]]
! find "${bundle}" -type f -name '*.py' -print -quit | grep -q .

root_entries="$(find "${bundle}" -mindepth 1 -maxdepth 1 \
  -printf '%f\n' | LC_ALL=C sort)"
[[ "${root_entries}" == $'aurora\nrun.sh' ]]
runtime_entries="$(find "${runtime}" -mindepth 1 -maxdepth 1 \
  -printf '%f\n' | LC_ALL=C sort)"
[[ "${runtime_entries}" == \
   $'bin\nlib\nmetadata\nruntime\nscripts' ]]
[[ ! -e "${runtime}/tools" ]]
[[ ! -e "${bundle}/README.txt" ]]
! find "${bundle}" -type d -name config -print -quit | grep -q .
! find "${bundle}" -type f \
  \( -name aurora.example.yaml -o -name roblox_payload.json \) \
  -print -quit | grep -q .
! find "${bundle}" -type f -name 'libgamemode*.so*' -print -quit | grep -q .

(cd -- "${bundle}" && sha256sum --quiet -c \
  aurora/metadata/SHA256SUMS.txt)
"${runtime}/runtime/android-tools/bin/aapt" version >/dev/null
"${runtime}/runtime/android-tools/bin/apksigner" version >/dev/null
LC_ALL=C readelf -h "${runtime}/bin/aurora_freebsd_socket_helper" |
  grep -Fq 'UNIX - FreeBSD'
[[ -z "$(LC_ALL=C readelf -l \
  "${runtime}/bin/aurora_freebsd_socket_helper" |
  sed -n 's/.*Requesting program interpreter: \([^]]*\).*/\1/p')" ]]
[[ "$(stat -c '%a' "${bundle}")" == 755 ]]
packaged_root_entries="$(find "${bundle}" -mindepth 1 -maxdepth 1 \
  -printf '%f\n' | LC_ALL=C sort)"
[[ "${packaged_root_entries}" == $'aurora\nrun.sh' ]]
RunThinLauncher AURORA_SKIP_HOST_CHECK=1 \
  "${bundle}/run.sh" --help >/dev/null
(cd -- "${bundle}" && sha256sum --quiet -c \
  aurora/metadata/SHA256SUMS.txt)

# Binutils localizes readelf field labels. The public launcher must force the
# C locale only for ABI probes while preserving the user's locale for Roblox.
mkdir -p "${TEMP_DIR}/localized-bin"
cat >"${TEMP_DIR}/localized-bin/readelf" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${AURORA_TEST_REAL_READELF:?}"
if [[ "${LC_ALL:-}" == C ]]; then
  exec "${AURORA_TEST_REAL_READELF}" "$@"
fi
LC_ALL=C "${AURORA_TEST_REAL_READELF}" "$@" |
  sed -e 's/Requesting program interpreter/Localized interpreter/' \
      -e 's/Shared library/Localized shared library/'
EOF
chmod 0755 "${TEMP_DIR}/localized-bin/readelf"
real_readelf="$(command -v readelf)"
RunThinLauncher PATH="${TEMP_DIR}/localized-bin:${thin_host_path}" \
  AURORA_TEST_REAL_READELF="${real_readelf}" \
  AURORA_SKIP_HOST_CHECK=1 \
  "${bundle}/run.sh" --help >/dev/null

grep -Fxq 'schema=2' "${metadata}/ABI.txt"
grep -Fxq 'architecture=x86_64' "${metadata}/ABI.txt"
grep -Fxq 'libc=glibc' "${metadata}/ABI.txt"
grep -Fxq 'mode=thin' "${metadata}/ABI.txt"
grep -Fxq 'interpreter=/lib64/ld-linux-x86-64.so.2' \
  "${metadata}/ABI.txt"
grep -Fxq 'android_tools_libc=glibc' "${metadata}/ABI.txt"
cp -- "${metadata}/ABI.txt" "${TEMP_DIR}/ABI.txt"
sed -i 's/^libc=glibc$/libc=musl/' "${metadata}/ABI.txt"
RefreshBundleChecksums
set +e
RunThinLauncher AURORA_SKIP_HOST_CHECK=1 "${bundle}/run.sh" --help \
  >"${TEMP_DIR}/abi.stdout" 2>"${TEMP_DIR}/abi.stderr"
abi_status=$?
set -e
[[ "${abi_status}" -ne 0 ]]
grep -Fq 'aurora targets glibc, but ABI.txt requires musl' \
  "${TEMP_DIR}/abi.stderr"
mv -- "${TEMP_DIR}/ABI.txt" "${metadata}/ABI.txt"
RefreshBundleChecksums

cp -- "${metadata}/ABI.txt" "${TEMP_DIR}/ABI.txt"
sed -i 's/^mode=thin$/mode=standalone/' "${metadata}/ABI.txt"
RefreshBundleChecksums
set +e
RunThinLauncher AURORA_SKIP_HOST_CHECK=1 "${bundle}/run.sh" --help \
  >"${TEMP_DIR}/mode.stdout" 2>"${TEMP_DIR}/mode.stderr"
mode_status=$?
set -e
[[ "${mode_status}" -ne 0 ]]
grep -Fq 'ABI mode standalone does not match DEPENDENCIES.txt mode thin' \
  "${TEMP_DIR}/mode.stderr"
mv -- "${TEMP_DIR}/ABI.txt" "${metadata}/ABI.txt"
RefreshBundleChecksums

touch "${runtime}/lib/unexpected.so"
set +e
RunThinLauncher AURORA_SKIP_HOST_CHECK=1 "${bundle}/run.sh" --help \
  >"${TEMP_DIR}/thin-lib.stdout" 2>"${TEMP_DIR}/thin-lib.stderr"
thin_lib_status=$?
set -e
[[ "${thin_lib_status}" -ne 0 ]]
grep -Fq 'thin bundle unexpectedly contains bundled dependency libraries' \
  "${TEMP_DIR}/thin-lib.stderr"
rm -f -- "${runtime}/lib/unexpected.so"

# When a musl compiler is available, prove that the launcher accepts native
# musl Aurora ELFs while Android metadata tools retain their Java contract.
if command -v musl-gcc >/dev/null 2>&1; then
  musl_bundle="${TEMP_DIR}/synthetic-musl"
  musl_runtime="${musl_bundle}/aurora"
  musl_metadata="${musl_runtime}/metadata"
  mkdir -p "${musl_runtime}/bin" \
    "${musl_runtime}/lib" \
    "${musl_runtime}/runtime" \
    "${musl_runtime}/scripts" \
    "${musl_runtime}/runtime/android-tools/bin" \
    "${musl_metadata}" \
    "${TEMP_DIR}/musl-fake-bin"
  musl-gcc -x c -o "${musl_runtime}/bin/aurora" - <<'EOF'
#include <stdio.h>
int main(void) {
  puts("native-musl-aurora");
  return 0;
}
EOF
  cp -- "${musl_runtime}/bin/aurora" \
    "${musl_runtime}/bin/aurora_failure_dialog"
  cp -- "${musl_runtime}/bin/aurora" \
    "${musl_runtime}/bin/aurora_updater"
  cp -- "${musl_runtime}/bin/aurora" \
    "${musl_runtime}/bin/aurora_webview_helper"
  for tool in aapt apkanalyzer apksigner; do
    cat >"${musl_runtime}/runtime/android-tools/bin/${tool}" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
    chmod 0755 "${musl_runtime}/runtime/android-tools/bin/${tool}"
  done
  printf '{}\n' >"${musl_metadata}/roblox_compatibility.json"
  printf '{}\n' >"${musl_metadata}/roblox_bootstrap_sources.json"
  printf '{}\n' >"${musl_metadata}/roblox_host_abi_reference.json"
  printf '{}\n' >"${musl_metadata}/roblox_signing_certificates.json"
  touch "${musl_metadata}/.aurora-portable-bundle"
  musl_interpreter="$(LC_ALL=C readelf -l "${musl_runtime}/bin/aurora" |
    sed -n 's/.*Requesting program interpreter: \([^]]*\).*/\1/p')"
  cat >"${musl_metadata}/ABI.txt" <<EOF
schema=2
architecture=x86_64
libc=musl
mode=thin
interpreter=${musl_interpreter}
android_tools_libc=java
EOF
  printf '%s\n' 'Aurora portable dependency contract' 'mode=thin' \
    >"${musl_metadata}/DEPENDENCIES.txt"
  cp -- "${ROOT}/packaging/run.sh" "${musl_bundle}/run.sh"
  cp -- "${ROOT}/packaging/aurora-launcher.sh" \
    "${musl_runtime}/scripts/portable_launcher.sh"
  cp -- "${ROOT}/scripts/install_thin_dependencies.sh" \
    "${musl_runtime}/scripts/install_thin_dependencies.sh"
  chmod 0755 "${musl_bundle}/run.sh"
  chmod 0755 "${musl_runtime}/scripts/portable_launcher.sh" \
    "${musl_runtime}/scripts/install_thin_dependencies.sh"
  (
    cd -- "${musl_bundle}"
    find . -type f \
      ! -path './aurora/metadata/SHA256SUMS.txt' -print0 |
      LC_ALL=C sort -z |
      xargs -0 sha256sum --
  ) >"${musl_metadata}/SHA256SUMS.txt"
  cat >"${TEMP_DIR}/musl-fake-bin/getconf" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
  cat >"${TEMP_DIR}/musl-fake-bin/ldd" <<'EOF'
#!/usr/bin/env bash
printf 'musl libc (x86_64)\n'
EOF
  chmod 0755 "${TEMP_DIR}/musl-fake-bin/getconf" \
    "${TEMP_DIR}/musl-fake-bin/ldd"
  musl_help="$(PATH="${TEMP_DIR}/musl-fake-bin:${thin_host_path}" \
    AURORA_OS_RELEASE_FILE="${TEMP_DIR}/thin-os-release" \
    AURORA_SKIP_HOST_CHECK=1 "${musl_bundle}/run.sh" --help)"
  grep -Fxq 'native-musl-aurora' <<<"${musl_help}"

  set +e
  PATH="${TEMP_DIR}/musl-fake-bin:${thin_host_path}" \
    AURORA_OS_RELEASE_FILE="${TEMP_DIR}/thin-os-release" \
    VK_ICD_FILENAMES="${TEMP_DIR}/synthetic-icd.json" \
    "${musl_bundle}/run.sh" --check-system \
    >"${TEMP_DIR}/musl-check.stdout" 2>"${TEMP_DIR}/musl-check.stderr"
  musl_check_status=$?
  set -e
  [[ "${musl_check_status}" -ne 0 ]]
  grep -Fq 'missing host Vulkan loader: libvulkan.so.1' \
    "${TEMP_DIR}/musl-check.stderr"
fi

main_runpath="$(LC_ALL=C readelf -d "${runtime}/bin/aurora" |
  sed -n 's/.*\(RPATH\|RUNPATH\).*\[\(.*\)\].*/\2/p')"
[[ "${main_runpath}" == '$ORIGIN' ]]
! LC_ALL=C readelf -d "${runtime}/bin/aurora_webview_helper" |
  grep -Eq '\((RPATH|RUNPATH)\)'
! LC_ALL=C readelf -d "${runtime}/bin/aurora" |
  grep -Fq "${ROOT}/build"

[[ ! -e "${runtime}/lib/libc.so.6" ]]
[[ ! -e "${runtime}/lib/libvulkan.so.1" ]]
[[ ! -e "${runtime}/lib/libwebkitgtk-6.0.so.4" ]]
! find "${runtime}/lib" -mindepth 1 -maxdepth 1 -print -quit | grep -q .
grep -q '^mode=thin$' "${metadata}/DEPENDENCIES.txt"
grep -q '^libc=glibc$' "${metadata}/DEPENDENCIES.txt"
grep -q '^android_tools_libc=glibc$' "${metadata}/DEPENDENCIES.txt"
grep -q 'physical GPU driver, Vulkan ICD, and GL/display driver stack are host-provided' \
  "${metadata}/DEPENDENCIES.txt"

relocated="${TEMP_DIR}/relocated/bundle"
mkdir -p "${TEMP_DIR}/relocated"
mv -- "${bundle}" "${relocated}"
RunThinLauncher AURORA_SKIP_HOST_CHECK=1 \
  "${relocated}/run.sh" --help >/dev/null

# The updater must keep all first-run scratch data outside a read-only
# portable/AppImage root. A missing APK should therefore fail at
# input validation, without trying to create rbx_bin beside the launcher.
chmod -R a-w -- "${relocated}"
set +e
"${relocated}/aurora/bin/aurora_updater" prepare-apks \
  "${TEMP_DIR}/readonly-store/payload" \
  "${TEMP_DIR}/missing-base.apk" \
  >"${TEMP_DIR}/readonly.stdout" 2>"${TEMP_DIR}/readonly.stderr"
readonly_status=$?
set -e
chmod -R u+w -- "${relocated}"
[[ "${readonly_status}" -ne 0 ]]
grep -Fq 'ZIP archive is not a regular file' "${TEMP_DIR}/readonly.stderr"
[[ ! -e "${relocated}/rbx_bin" ]]

# AppImage bundle payloads never ship Cargo sources or an Android SDK
# development tree. The native updater owns the direct HTTPS provider.
[[ ! -e "${relocated}/aurora/tools" ]]
[[ ! -e "${relocated}/aurora/runtime/android-tools/sdk" ]]
[[ ! -e "${relocated}/aurora/scripts/apk_providers" ]]

standalone_bundle="${TEMP_DIR}/standalone/aurora-linux-x86_64-glibc-standalone"
"${PACKAGER}" --build-dir "${BUILD_DIR}" --libc glibc --mode full \
  --output "${standalone_bundle}"
standalone_runtime="${standalone_bundle}/aurora"
standalone_metadata="${standalone_runtime}/metadata"
! find "${standalone_bundle}" -type f -name 'libgamemode*.so*' \
  -print -quit | grep -q .

standalone_root_entries="$(find "${standalone_bundle}" -mindepth 1 \
  -maxdepth 1 -printf '%f\n' | LC_ALL=C sort)"
[[ "${standalone_root_entries}" == $'aurora\nrun.sh' ]]
standalone_runtime_entries="$(find "${standalone_runtime}" -mindepth 1 \
  -maxdepth 1 -printf '%f\n' | LC_ALL=C sort)"
[[ "${standalone_runtime_entries}" == \
   $'bin\nlib\nlibexec\nmetadata\nnamespace\nruntime\nscripts\nshare\nwebkit.env' ]]
[[ ! -e "${standalone_runtime}/tools" ]]
for path in runtime libexec share; do
  [[ -d "${standalone_runtime}/${path}" &&
     ! -L "${standalone_runtime}/${path}" ]]
done
[[ -x "${standalone_runtime}/runtime/bin/bash" ]]
[[ ! -e "${standalone_runtime}/runtime/bin/python3" ]]
[[ ! -e "${standalone_runtime}/runtime/python" ]]
! find "${standalone_runtime}/runtime/jre" -name 'libjsound*.so' \
  -print -quit | grep -q .
[[ ! -e "${standalone_runtime}/lib/plugins/glycin-loaders/2+/glycin-heif" ]]
[[ ! -e "${standalone_runtime}/share/glycin-loaders/2+/conf.d/glycin-heif.conf" ]]
[[ -s "${standalone_runtime}/webkit.env" ]]
[[ ! -e "${standalone_runtime}/scripts/aurora_login_webview.py" ]]
[[ ! -e "${standalone_runtime}/libexec/webkit2gtk-4.1" ]]
[[ ! -e "${standalone_runtime}/lib/girepository-1.0" ]]
[[ ! -e "${standalone_runtime}/runtime/share/girepository-1.0" ]]
[[ -f "${standalone_runtime}/lib/libadwaita-1.so.0" ]]
[[ -n "$(find "${standalone_runtime}/lib" -mindepth 1 -type f \
  -print -quit)" ]]
[[ -n "$(find "${standalone_runtime}/libexec" -mindepth 1 -type f \
  -print -quit)" ]]
[[ -n "$(find "${standalone_runtime}/share" -mindepth 1 \
  -print -quit)" ]]
grep -Fxq 'mode=standalone' "${standalone_metadata}/ABI.txt"
grep -Fxq 'mode=standalone' "${standalone_metadata}/DEPENDENCIES.txt"
(cd -- "${standalone_bundle}" && sha256sum --quiet -c \
  aurora/metadata/SHA256SUMS.txt)
AURORA_SKIP_HOST_CHECK=1 "${standalone_bundle}/run.sh" --help >/dev/null

unsafe_output="${TEMP_DIR}/unmarked"
mkdir -p "${unsafe_output}"
touch "${unsafe_output}/user-file"
if "${PACKAGER}" --build-dir "${BUILD_DIR}" --libc glibc --mode thin \
    --output "${unsafe_output}" >/dev/null 2>&1; then
  printf 'packager replaced an unmarked output directory\n' >&2
  exit 1
fi
[[ -f "${unsafe_output}/user-file" ]]

printf 'portable packaging test passed\n'
