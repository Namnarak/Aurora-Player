#!/usr/bin/env bash
# Copyright 2026 NamKrub
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

binary=${1:?aurora_mcp binary is required}
test_root=$(mktemp -d "${TMPDIR:-/tmp}/aurora_mcp_protocol.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

output=$(XDG_CONFIG_HOME="$test_root/config" \
  XDG_STATE_HOME="$test_root/state" \
  AURORA_PLAYER_PATH=/bin/true \
  "$binary" <<'JSON'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"aurora_runtime_status","arguments":{}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"aurora_player_launch","arguments":{"args":[]}}}
JSON
)

grep -q '"id":1' <<<"$output"
grep -q 'aurora_player_launch' <<<"$output"
grep -q 'aurora_runtime_status' <<<"$output"
grep -q '"role":"player"' <<<"$output"
if grep -Eiq 'studio|clients_status' <<<"$output"; then
  echo "Player MCP exposed a Studio-only tool or state" >&2
  exit 1
fi
if grep -q 'notifications/initialized' <<<"$output"; then
  echo "notification unexpectedly produced a response" >&2
  exit 1
fi
