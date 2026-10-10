#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail
readonly source_root="${1:?third-party source directory is required}"
readonly install_root="${2:?installed notice directory is required}"
mapfile -d '' notices < <(
  find "${source_root}" -type f \
    \( -name 'LICENSE*' -o -name 'NOTICE*' -o -name 'COPYING*' \
       -o -path '*/LICENSES/*' \) -print0 | sort -z
)
readonly assembly_exception="${source_root}/jni/ASSEMBLY_EXCEPTION"
[[ -s "${assembly_exception}" ]] || {
  printf 'JNI Assembly Exception notice is missing from the source tree\n' >&2
  exit 1
}
notices+=("${assembly_exception}")
[[ "${#notices[@]}" -eq 16 ]] || {
  printf 'expected 15 third-party notice files plus ASSEMBLY_EXCEPTION, found %s total\n' "${#notices[@]}" >&2
  exit 1
}
for notice in "${notices[@]}"; do
  relative="${notice#"${source_root}"/}"
  installed="${install_root}/${relative}"
  [[ -f "${installed}" ]] || {
    printf 'installed third-party notice is missing: %s\n' "${relative}" >&2
    exit 1
  }
  cmp -s -- "${notice}" "${installed}" || {
    printf 'installed third-party notice differs: %s\n' "${relative}" >&2
    exit 1
  }
done
printf '15 third-party license/notice files and JNI ASSEMBLY_EXCEPTION match byte for byte\n'
