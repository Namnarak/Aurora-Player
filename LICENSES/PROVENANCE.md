# Source and license provenance

Aurora Player continues the Apache-2.0 Mocktail project. The source tree was
synchronized with Mocktail commits
[`2451264696d83bdb91d1517a7107273213178aca`](https://github.com/komaruworld/mocktail/commit/2451264696d83bdb91d1517a7107273213178aca)
and [`a9a96c6861a99187c1489e8a06942d4af97d4fdc`](https://github.com/komaruworld/mocktail/commit/a9a96c6861a99187c1489e8a06942d4af97d4fdc).
The Apache-2.0 text in `LICENSE` is preserved from that project.

NamKrub-authored first-party additions in the pre-split source history include:

| Files | Source history | License |
| --- | --- | --- |
| `src/player/aurora_player.cc`, `src/mcp/aurora_mcp.cc`, `src/ui/aurora_settings.cc` | `3b18378ecf3a8549a137648fb1672142ca8b4010` | Apache-2.0 |
| `include/runtime/managed_process.h`, `src/runtime/managed_process.cc`, `tests/managed_process_test.cc` | `3b18378ecf3a8549a137648fb1672142ca8b4010` | Apache-2.0 |
| `include/runtime/external_client_detector.h`, `tests/external_client_detector_test.cc` | `3b18378ecf3a8549a137648fb1672142ca8b4010` | Apache-2.0 |
| `src/runtime/external_client_detector.cc` | `91b7a5ba57bed27be574852a0e21e378b1446018` | Apache-2.0 |
| `include/runtime/project_safety_guard.h`, `src/runtime/project_safety_guard.cc`, `tests/project_safety_guard_test.cc` | `08f6ef77bd754c912bd8b0c880f4b2d5671c713c` | Apache-2.0 |
| `include/update/apkcombo_provider.h`, `src/update/apkcombo_provider.cc` | NamKrub-authored local worktree additions; these files had no upstream source commit | Apache-2.0 |

The APKCombo provider was reviewed as first-party implementation: it uses
Aurora's update interfaces and the C++ standard library, with no copied
third-party implementation or license text. It is built only in Aurora Player.

Aurora bundles script-aware fallback fonts under `third_party/noto/fonts/`:
Regular faces from the Arch Linux `noto-fonts 1:2026.10.01-1` package and the
Noto Sans CJK collection from `noto-fonts-cjk 20240730-1`. Their upstream
projects are [Noto](https://github.com/notofonts/notofonts.github.io) and
[Noto CJK](https://github.com/notofonts/noto-cjk). Noto fonts are distributed
under the SIL Open Font License 1.1; the respective license texts are kept in
`third_party/noto/LICENSES/`. `third_party/noto/SHA256SUMS.txt` records the
hashes of every bundled font file. Aurora preserves the Roblox-selected font
as the primary face and registers these bundled fonts as fallbacks for missing
script glyphs.

This publication snapshot intentionally has no Git history. The full local
source hashes above refer to the pre-split checkout and are included as
provenance evidence; they are not commits in this repository. Third-party
components retain their own Apache-2.0, MIT, BSD-2-Clause, BSD-2-Clause-FreeBSD,
BSD-3-Clause, BSD-4-Clause, SIL OFL 1.1, and GPL-2.0-only with Classpath
exception notices.
The package installs those notices and the JNI `ASSEMBLY_EXCEPTION` under
`share/doc/aurora-player/third-party/`.
