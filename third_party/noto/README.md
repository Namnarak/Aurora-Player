# Bundled Noto fonts

Aurora bundles the Regular faces from the Noto font collection and the Noto
Sans CJK collection. The font files were taken from the Arch Linux packages
`noto-fonts 1:2026.10.01-1` and `noto-fonts-cjk 20240730-1`; see
[`SHA256SUMS.txt`](SHA256SUMS.txt) for the exact bundled file hashes.

The Noto collection covers a broad range of living and historical scripts.
The CJK collection is provided as a TrueType Collection, with separate faces
for Japanese, Korean, Simplified Chinese, Traditional Chinese, and Hong Kong
Chinese. Aurora keeps Roblox's selected font as the primary face and uses
these files as script-aware fallbacks when the primary font has no glyph.

The collection does not claim complete coverage of every Unicode character or
language. Noto itself is a family of script-specific fonts rather than one
universal font. The bundled files use the SIL Open Font License 1.1; the
applicable license texts are in [`LICENSES/`](LICENSES/).

Upstream projects:

- [Noto fonts](https://github.com/notofonts/notofonts.github.io)
- [Noto CJK](https://github.com/notofonts/noto-cjk)
- [Noto documentation](https://notofonts.github.io/)
