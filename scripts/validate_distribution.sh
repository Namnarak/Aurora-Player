#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
for pkg in aurora aurora-bin aurora-git; do
  bash -n "packaging/aur/$pkg/PKGBUILD"
  test -s "packaging/aur/$pkg/.SRCINFO"
  grep -q "^pkgname=$pkg$" "packaging/aur/$pkg/PKGBUILD"
done
python3 -m json.tool packaging/flatpak/space.bigrat.aurora.json >/dev/null
python3 - <<'PYCODE'
from pathlib import Path
import re
src=Path('CMakeLists.txt').read_text()
m=re.search(r'project\(AuroraPlayer\s+VERSION\s+(\d+(?:\.\d+)+)',src)
assert m,'Missing version'
manifest=Path('packaging/aur/aurora-bin/PKGBUILD').read_text()
assert 'pkgver='+m.group(1) in manifest
assert 'Namnarak/Aurora-Player' in manifest
assert '.AppImage' in manifest and 'sha256sums' in manifest
assert 'aurora.bigrat.space' not in Path('scripts/assemble_native_repositories.sh').read_text()
print('Aurora distribution metadata okay',m.group(1))
PYCODE
