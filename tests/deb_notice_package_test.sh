#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail
readonly product="${1:?product name is required}"
readonly build_dir="${2:?build directory is required}"
readonly source_dir="${3:?source directory is required}"
readonly expected_license="${4:?expected package license is required}"
readonly work_dir="$(mktemp -d)"
trap 'rm -rf -- "${work_dir}"' EXIT

cpack --config "${build_dir}/CPackConfig.cmake" -G DEB -B "${work_dir}"
mapfile -t packages < <(find "${work_dir}" -maxdepth 1 -type f -name '*.deb' -print)
[[ "${#packages[@]}" -eq 1 ]] || {
  printf 'expected exactly one DEB, found %s\n' "${#packages[@]}" >&2
  exit 1
}
description="$(dpkg-deb -f "${packages[0]}" Description)"
grep -Fq -- "${expected_license}" <<<"${description}" || {
  printf 'DEB Description does not identify license %s\n' "${expected_license}" >&2
  printf '%s\n' "${description}" >&2
  exit 1
}

readonly extracted="${work_dir}/extracted"
dpkg-deb -x "${packages[0]}" "${extracted}"
readonly doc="${extracted}/usr/share/doc/${product}"
[[ -s "${doc}/LICENSE" ]]
if [[ "${product}" == "aurora-studio" ]]; then
  [[ -s "${doc}/LICENSE-APACHE" ]]
  [[ -s "${doc}/APACHE-FILES.txt" ]]
  [[ -s "${doc}/DISTRIBUTION-LICENSES.md" ]]
fi
bash "${source_dir}/tests/third_party_notices_contract.sh" \
  "${source_dir}/third_party" "${doc}/third-party"
printf '%s DEB notice and license metadata test passed\n' "${product}"
