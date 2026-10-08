#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${AURORA_BUILD_DIR:-${PROJECT_ROOT}/build}"
BINARY="${BUILD_DIR}/aurora"
LIBROBLOX_PATH="${ROBLOX_LIB_PATH:-${PROJECT_ROOT}/rbx_bin/libroblox.so}"

if [[ "${LIBROBLOX_PATH}" != /* ]]; then
  LIBROBLOX_PATH="${PROJECT_ROOT}/${LIBROBLOX_PATH}"
fi

target_user_home() {
  local user="$1"
  local entry
  entry="$(getent passwd "${user}" 2>/dev/null || true)"
  if [[ -z "${entry}" ]]; then
    return 1
  fi
  local name passwd uid gid gecos home shell
  IFS=: read -r name passwd uid gid gecos home shell <<< "${entry}"
  printf '%s\n' "${home}"
}

configure_wayland_env() {
  local uid="$1"

  export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/${uid}}"
  export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
  export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=${XDG_RUNTIME_DIR}/bus}"
  export AURORA_PREFER_WAYLAND="${AURORA_PREFER_WAYLAND:-1}"

  # Leave SDL_VIDEODRIVER unset so the runtime policy can avoid native
  # Wayland Vulkan WSI on NVIDIA. Explicit caller overrides remain untouched.
}

if [[ ! -f "${LIBROBLOX_PATH}" ]]; then
  echo "[error] libroblox.so not found: ${LIBROBLOX_PATH}" >&2
  echo "        Put an x86_64 Roblox lib at rbx_bin/libroblox.so or set ROBLOX_LIB_PATH." >&2
  exit 1
fi

if [[ ! -x "${BINARY}" ]]; then
  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug
  cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

cd "${PROJECT_ROOT}"

export ROBLOX_LIB_PATH="${LIBROBLOX_PATH}"
export AURORA_SOBER_MODE="${AURORA_SOBER_MODE:-1}"
export AURORA_ASSET_ROOT="${AURORA_ASSET_ROOT:-${PROJECT_ROOT}/rbx_bin/assets}"
export AURORA_ASSET_PATH="${AURORA_ASSET_PATH:-rbx_bin/assets/content}"
export AURORA_SOBER_BOOTSTRAP_ONLY="${AURORA_SOBER_BOOTSTRAP_ONLY:-0}"
export AURORA_GRAPHICS_BACKEND="${AURORA_GRAPHICS_BACKEND:-system}"

RUN_AS_USER="${AURORA_RUN_AS_USER:-}"
if [[ -n "${RUN_AS_USER}" && "${AURORA_RUNNER_REEXECED:-0}" == "0" &&
      "$(id -un)" != "${RUN_AS_USER}" ]]; then
  if [[ "$(id -u)" != "0" ]]; then
    echo "[warn] not running as root; cannot switch GUI process to ${RUN_AS_USER}" >&2
  elif ! id "${RUN_AS_USER}" >/dev/null 2>&1; then
    echo "[warn] user not found: ${RUN_AS_USER}; running as $(id -un)" >&2
  else
    RUN_AS_UID="$(id -u "${RUN_AS_USER}")"
    RUN_AS_HOME="$(target_user_home "${RUN_AS_USER}")"
    configure_wayland_env "${RUN_AS_UID}"
    echo "[info] switching GUI process to ${RUN_AS_USER} on ${WAYLAND_DISPLAY}"
    exec runuser -u "${RUN_AS_USER}" -- env \
      HOME="${RUN_AS_HOME}" \
      USER="${RUN_AS_USER}" \
      LOGNAME="${RUN_AS_USER}" \
      XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
      WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
      DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS}" \
      SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-}" \
      AURORA_RUNNER_REEXECED=1 \
      ROBLOX_LIB_PATH="${ROBLOX_LIB_PATH}" \
      AURORA_BUILD_DIR="${BUILD_DIR}" \
      AURORA_SOBER_MODE="${AURORA_SOBER_MODE}" \
      AURORA_ASSET_ROOT="${AURORA_ASSET_ROOT}" \
      AURORA_ASSET_PATH="${AURORA_ASSET_PATH}" \
      AURORA_SOBER_BOOTSTRAP_ONLY="${AURORA_SOBER_BOOTSTRAP_ONLY}" \
      AURORA_GRAPHICS_BACKEND="${AURORA_GRAPHICS_BACKEND}" \
      AURORA_PREFER_WAYLAND="${AURORA_PREFER_WAYLAND}" \
      AURORA_EGL_LIBRARY="${AURORA_EGL_LIBRARY:-}" \
      AURORA_GLES_LIBRARY="${AURORA_GLES_LIBRARY:-}" \
      AURORA_FORCE_X11="${AURORA_FORCE_X11:-0}" \
      AURORA_ANGLE_FORCE_X11="${AURORA_ANGLE_FORCE_X11:-0}" \
      "${BASH}" "${BASH_SOURCE[0]}" "$@"
  fi
fi

if [[ -n "${RUN_AS_USER}" && "$(id -un)" == "${RUN_AS_USER}" ]]; then
  configure_wayland_env "$(id -u)"
fi

if [[ "${AURORA_SOBER_BOOTSTRAP_ONLY}" != "0" ]]; then
  export AURORA_STEP_APP_BRIDGE_APP_START="${AURORA_STEP_APP_BRIDGE_APP_START:-0}"
  export AURORA_STEP_START_LUA_APP_DM="${AURORA_STEP_START_LUA_APP_DM:-0}"
fi

echo "[info] starting Aurora"
echo "[info] SDL video driver: ${SDL_VIDEODRIVER:-auto}"
echo "[info] graphics backend: ${AURORA_GRAPHICS_BACKEND}"
echo "[info] display: DISPLAY=${DISPLAY:-unset} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-unset}"
echo "[info] bootstrap-only: ${AURORA_SOBER_BOOTSTRAP_ONLY}"
echo "[info] press Ctrl+C to stop"

if [[ -n "${AURORA_LOG_PATH:-}" ]]; then
  set +e
  "${BINARY}" "$@" 2>&1 | tee "${AURORA_LOG_PATH}"
  binary_status="${PIPESTATUS[0]}"
  set -e
  exit "${binary_status}"
fi

exec "${BINARY}" "$@"
