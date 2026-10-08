#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly app_run="${1:?usage: appimage_apprun_test.sh /path/to/AppRun}"
app_dir="$(mktemp -d)"
trap 'rm -rf -- "${app_dir}"' EXIT

mkdir -p -- "${app_dir}/usr/share/aurora-bundle"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'printf "bundle=%s argc=%s first=%s second=%s\\n" "$0" "$#" "$1" "$2"' \
  >"${app_dir}/usr/share/aurora-bundle/run.sh"
chmod 0755 "${app_dir}/usr/share/aurora-bundle/run.sh"
ln -s share/aurora-bundle/run.sh "${app_dir}/usr/aurora"

readonly launch_uri='roblox://experiences/start?placeId=1&gameInstanceId=a%2Bb'
output="$(APPDIR="${app_dir}" "${app_run}" --launch-uri "${launch_uri}")"
grep -Fq "bundle=${app_dir}/usr/share/aurora-bundle/run.sh" <<<"${output}"
grep -Fq 'argc=2' <<<"${output}"
grep -Fq 'first=--launch-uri' <<<"${output}"
grep -Fq "second=${launch_uri}" <<<"${output}"

mkdir -p -- "${app_dir}/bin" \
  "${app_dir}/usr/share/aurora-bundle/aurora/scripts"
printf '%s\n' '#!/bin/sh' 'exec /usr/bin/bash "$@"' \
  >"${app_dir}/bin/bash"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'printf "anylinux=%s bundle=%s first=%s\n" "${AURORA_ANYLINUX_BIN_DIR}" "${AURORA_BUNDLE_ROOT}" "$1"' \
  >"${app_dir}/usr/share/aurora-bundle/aurora/scripts/portable_launcher.sh"
chmod 0755 "${app_dir}/bin/bash" \
  "${app_dir}/usr/share/aurora-bundle/aurora/scripts/portable_launcher.sh"

output="$(APPDIR="${app_dir}" SHARUN_DIR="${app_dir}" \
  "${app_run}" --launch-uri "${launch_uri}")"
grep -Fq "anylinux=${app_dir}/bin" <<<"${output}"
grep -Fq "bundle=${app_dir}/usr/share/aurora-bundle" <<<"${output}"
grep -Fq 'first=--launch-uri' <<<"${output}"

# With a custom AppRun, quick-sharun's generated startup is skipped. Its
# paths and certificate hooks must run on every fresh launch, before the
# helper is spawned, without consuming application arguments.
cat >"${app_dir}/bin/01-check-ca-certs.hook" <<'EOF'
export AURORA_TEST_CA_HOOK=ready
EOF
cat >"${app_dir}/bin/01-path-mapping-hardcoded.hook" <<'EOF'
[ "${AURORA_TEST_CA_HOOK}" = ready ]
ln -s "${APPDIR}/bin" "${APPDIR}/mapped-bin"
set -- hook-private-argument
EOF
cat >"${app_dir}/usr/share/aurora-bundle/aurora/scripts/portable_launcher.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "${AURORA_TEST_CA_HOOK}" == ready ]]
[[ -x "${APPDIR}/mapped-bin/bash" ]]
[[ "$#" == 2 && "$1" == --launch-uri ]]
printf '%s\n' "$2"
EOF
output="$(APPDIR="${app_dir}" SHARUN_DIR="${app_dir}" \
  "${app_run}" --launch-uri "${launch_uri}")"
[[ "${output}" == "${launch_uri}" ]]
unlink "${app_dir}/mapped-bin"
output="$(APPDIR="${app_dir}" SHARUN_DIR="${app_dir}" \
  "${app_run}" --launch-uri "${launch_uri}")"
[[ "${output}" == "${launch_uri}" ]]

printf 'AppImage AppRun test passed\n'
