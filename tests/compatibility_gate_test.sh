#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# Licensed under the Apache License, Version 2.0.

set -Eeuo pipefail

if (( $# != 3 )); then
  printf 'usage: %s AURORA UNKNOWN_ELF COMPATIBILITY_MANIFEST\n' "$0" >&2
  exit 2
fi

aurora_binary="$1"
unknown_elf="$2"
compatibility_manifest="$3"

temporary_root="$(mktemp -d)"
trap 'rm -rf "${temporary_root}"' EXIT

set +e
mkdir -p \
  "${temporary_root}/run" \
  "${temporary_root}/assets" \
  "${temporary_root}/config" \
  "${temporary_root}/data" \
  "${temporary_root}/cache" \
  "${temporary_root}/state" \
  "${temporary_root}/auth"
output="$(
  env -i \
    PATH="${PATH}" \
    HOME="${HOME}" \
    XDG_RUNTIME_DIR="${temporary_root}/run" \
    AURORA_ISOLATED_CANARY=1 \
    AURORA_ALLOW_NO_COOKIE_LUA_APP=1 \
    AURORA_CONFIG_ROOT="${temporary_root}/config" \
    AURORA_DATA_ROOT="${temporary_root}/data" \
    AURORA_CACHE_ROOT="${temporary_root}/cache" \
    AURORA_STATE_ROOT="${temporary_root}/state" \
    AURORA_AUTH_ROOT="${temporary_root}/auth" \
    AURORA_COOKIE_FILE="${temporary_root}" \
    AURORA_ASSET_PATH="${temporary_root}/assets" \
    AURORA_SKIP_UPDATE_CHECK=1 \
    AURORA_AUTO_EXIT_AFTER_PRESENT_MS=5000 \
    ROBLOX_LIB_PATH="${unknown_elf}" \
    AURORA_COMPATIBILITY_MANIFEST="${compatibility_manifest}" \
    "${aurora_binary}" 2>&1
)"
status=$?
set -e

if (( status == 0 )); then
  printf 'unknown payload unexpectedly succeeded\n%s\n' "${output}" >&2
  exit 1
fi
if ! grep -Fq 'Unsupported Roblox Build ID' <<<"${output}"; then
  printf 'missing fail-closed Build-ID diagnostic\n%s\n' "${output}" >&2
  exit 1
fi
if grep -Fq 'unknown Roblox fixture reached native loading' <<<"${output}"; then
  printf 'unknown payload reached native loading\n%s\n' "${output}" >&2
  exit 1
fi

printf '%s\n' "${output}"
