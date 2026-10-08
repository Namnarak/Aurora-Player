#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail

readonly build_dir="${1:?build directory is required}"
readonly source_dir="${2:?source directory is required}"
readonly install_root="$(mktemp -d)"
trap 'rm -rf -- "${install_root}"' EXIT

DESTDIR="${install_root}" cmake --install "${build_dir}" --prefix /usr \
  >/dev/null

readonly binary="${install_root}/usr/bin/aurora"
readonly player_binary="${install_root}/usr/bin/aurora_player"
readonly settings_binary="${install_root}/usr/bin/aurora_settings"
readonly mcp_binary="${install_root}/usr/bin/aurora_mcp"
if [[ -d "${install_root}/usr/lib64/aurora" ]]; then
  readonly runtime="${install_root}/usr/lib64/aurora"
else
  readonly runtime="${install_root}/usr/lib/aurora"
fi
readonly data="${install_root}/usr/share/aurora"
readonly doc="${install_root}/usr/share/doc/aurora-player"
readonly desktop="${install_root}/usr/share/applications/space.bigrat.aurora.desktop"
readonly metainfo="${install_root}/usr/share/metainfo/space.bigrat.aurora.metainfo.xml"

[[ -x "${binary}" && -x "${player_binary}" && -x "${settings_binary}" && -x "${mcp_binary}" ]]
[[ ! -e "${install_root}/usr/bin/aurora_studio" ]]
[[ ! -e "${install_root}/usr/bin/aurora_studio_mcp" ]]
[[ -x "${runtime}/aurora_updater" ]]
[[ -x "${runtime}/aurora_failure_dialog" ]]
[[ -x "${runtime}/aurora_webview_helper" ]]
[[ -f "${runtime}/libOpenSLES.so" ]]
[[ -f "${data}/metadata/roblox_compatibility.json" ]]
[[ -f "${data}/metadata/roblox_host_abi_reference.json" ]]
[[ -f "${data}/metadata/roblox_signing_certificates.json" ]]
[[ -f "${doc}/LICENSE" && -s "${doc}/TERMS.md" && -s "${doc}/PRIVACY.md" ]]
[[ -s "${doc}/PROVENANCE.md" ]]
bash "${source_dir}/tests/third_party_notices_contract.sh" \
  "${source_dir}/third_party" "${doc}/third-party"

readelf -h "${binary}" | grep -Fq 'ELF64'
readelf -h "${player_binary}" | grep -Fq 'ELF64'
readelf -h "${runtime}/aurora_updater" | grep -Fq 'ELF64'
readelf -d "${binary}" | grep -Eq '\$ORIGIN/\.\./(lib|lib64)/aurora'
readelf -d "${runtime}/aurora_failure_dialog" | grep -Fq 'libadwaita-1.so.0'
! readelf -d "${binary}" | grep -Fq 'libadwaita-1.so.0'

[[ -f "${desktop}" ]]
grep -Fq 'Name=Aurora' "${desktop}"
grep -Fxq 'Exec=aurora %u' "${desktop}"
[[ -f "${metainfo}" ]]
grep -Fq '<id>space.bigrat.aurora</id>' "${metainfo}"

if find "${install_root}/usr" -type f \( -name '*.py' -o -name '*.sh' \) \
    -print -quit | grep -q .; then
  echo 'system install contains a first-party Python or shell runtime' >&2
  exit 1
fi

"${binary}" --help | grep -Fq 'Usage:'
"${runtime}/aurora" --help | grep -Fq 'Usage:'
updater_home="${install_root}/updater-home"
mkdir -p "${updater_home}"
HOME="${updater_home}" \
AURORA_DATA_ROOT="${updater_home}/data" \
AURORA_CACHE_ROOT="${updater_home}/cache" \
AURORA_STATE_ROOT="${updater_home}/state" \
  "${runtime}/aurora_updater" status | grep -Fq '"current": null'

printf 'Player system install layout test passed\n'
