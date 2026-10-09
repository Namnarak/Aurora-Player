#!/usr/bin/env bash
# Copyright 2026 NamKrub
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail
readonly ROOT="$(cd -- "${1:?source root is required}" && pwd)"
fail() { printf 'Player product contract failed: %s\n' "$*" >&2; exit 1; }

grep -Fq 'project(AuroraPlayer' "${ROOT}/CMakeLists.txt" || fail 'CMake project identity is wrong'
grep -Fq 'set(AURORA_PACKAGE_NAME "aurora-player"' "${ROOT}/CMakeLists.txt" || fail 'stable package name is wrong'
grep -Fq 'set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Namnarak/Aurora-Player")' "${ROOT}/CMakeLists.txt" || fail 'CPack homepage is wrong'
grep -Fq 'set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0 AND MIT AND BSD-2-Clause AND BSD-2-Clause-FreeBSD AND BSD-3-Clause AND BSD-4-Clause AND GPL-2.0-only WITH Classpath-exception-2.0")' "${ROOT}/CMakeLists.txt" || fail 'aggregate RPM license metadata is incomplete'
grep -Fq 'License: Apache-2.0.' "${ROOT}/CMakeLists.txt" || fail 'package description omits Apache license'
grep -Fq '<project_license>Apache-2.0</project_license>' "${ROOT}/packaging/space.bigrat.aurora.metainfo.xml" || fail 'desktop metadata license is wrong'
grep -Fq 'https://github.com/Namnarak/Aurora-Player' "${ROOT}/packaging/space.bigrat.aurora.metainfo.xml" || fail 'desktop metadata homepage is wrong'
grep -Fq 'https://github.com/Namnarak/Aurora-Player' "${ROOT}/README.md" || fail 'README does not link the Player repository'
test -f "${ROOT}/LICENSES/PROVENANCE.md" || fail 'provenance document is missing'
test -f "${ROOT}/LICENSE" || fail 'Apache license is missing'
test -s "${ROOT}/third_party/jni/ASSEMBLY_EXCEPTION" || fail 'JNI Assembly Exception source notice is missing'
test ! -e "${ROOT}/tests/system_install_test.sh" || fail 'combined-product system install test remains'
if grep -Fq '${CMAKE_CURRENT_SOURCE_DIR}/system_install_test.sh' "${ROOT}/tests/CMakeLists.txt"; then
  fail 'obsolete combined-product install test is still registered'
fi
grep -Fq 'src/update/apkcombo_provider.cc' "${ROOT}/CMakeLists.txt" || fail 'Player APKCombo provider is not built'
grep -Fq 'AURORA_ENABLE_APKCOMBO_PROVIDER=1' "${ROOT}/CMakeLists.txt" || fail 'Player APKCombo provider is not enabled'
grep -Fq 'SPDX-License-Identifier: Apache-2.0' "${ROOT}/src/update/apkcombo_provider.cc" || fail 'APKCombo provider license attribution is missing'
grep -Fq 'NamKrub-authored local worktree additions' "${ROOT}/LICENSES/PROVENANCE.md" || fail 'local provider provenance is missing'
# Only documented Player distribution workflows may be in the public repo.
if find "${ROOT}/.github/workflows" -maxdepth 1 -type f \
    ! -name ci.yml ! -name distribution.yml ! -name aur-check.yml \
    -print -quit | grep -q .; then
  fail 'unknown workflow remains in the publication candidate'
fi
python3 - "${ROOT}" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for subdir in ('src', 'include', 'tests', 'scripts'):
    directory = root / subdir
    if not directory.exists():
        continue
    for path in directory.rglob('*'):
        if not path.is_file():
            continue
        if path.name == 'player_product_contract_test.sh':
            continue
        try:
            content = path.read_text(encoding='utf-8')
        except (UnicodeDecodeError, OSError):
            continue
        if 'SPDX-License-Identifier: GPL' in content or 'GPL-3.0-only' in content:
            raise SystemExit(f'GPL-only source remains in Player tree: {path.relative_to(root)}')
PY
printf 'Aurora Player product contract passed\n'
