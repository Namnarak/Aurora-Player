# Aurora Player Linux Distribution

The .github/workflows/distribution.yml pipeline creates Linux x86_64 builds
for AppImage, Ubuntu 26.04 DEB, Fedora 44 RPM and Flatpak. The recipes for
aurora, aurora-bin, aurora-git are in packaging/aur/ but not submitted to AUR.

GitHub Releases: https://github.com/Namnarak/Aurora-Player/releases

Channels:
- On main: run nightly; publish the mutable continuous prerelease only if
  all four format builds pass.
- On tag vX.Y.Z or X.Y.Z matching CMake: publish stable after successful builds.
- workflow_dispatch from main: rerun nightly.
- Roblox APK is never bundled in any published artifact.

Installation of a downloaded binary:
- AppImage: chmod +x Aurora-x86_64-glibc-standalone.AppImage
  then ./Aurora-x86_64-glibc-standalone.AppImage
- Ubuntu: sudo apt install ./aurora-player_*.deb
- Fedora: sudo dnf install ./aurora-player-*.rpm
- Flatpak: flatpak install --user ./Aurora-x86_64.flatpak

Limitations:
- AUR registration/SSH maintainer access is required to publish recipes to
  aur.archlinux.org. aur-check workflow validates only, does NOT publish.
- Flathub publication requires separate review and ownership of App ID.
  The space.bigrat.aurora naming comes from upstream; do not claim that
  upstream namespace without authorization. Migrate ID before submission.
- To publish signed APT/RPM repositories, provide dedicated HTTPS hosting,
  GPG keys, and set AURORA_REPO_BASE_URL to your actual repository URL.
- AArch64 is experimental; not part of binary distribution yet.
- Automated builds do not test Roblox login, GPU compatibility, gameplay, VC
  or in-game purchases. Perform real smoke tests before recommending stable.
