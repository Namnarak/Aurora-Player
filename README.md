# Aurora Player

[![CI](https://github.com/Namnarak/Aurora-Player/actions/workflows/ci.yml/badge.svg)](https://github.com/Namnarak/Aurora-Player/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Aurora Player runs Roblox on Linux using the Android `x86_64` client and a
native Linux graphics, audio, and input layer. The verified client payload is
downloaded when needed and is not included in this source repository.

Aurora Player is an independent community project. It is not affiliated with
Roblox Corporation and does not distribute the Roblox client.

## Features

- Native Vulkan and OpenGL rendering with ETC2 texture handling
- Roblox account, experience, and player runtime support
- Audio output, voice input integration, gamepad input, and Linux windowing
- Player settings, support logs, and safe runtime update handling
- A player-side MCP server (`aurora_mcp`) for status, launch, stop, and log-tail
  tools

Aurora Studio is developed separately in
[Namnarak/Aurora-Studio](https://github.com/Namnarak/Aurora-Studio).

Read the [Terms of Use](TERMS.md) and [Privacy Policy](PRIVACY.md) before use.
Packages install third-party license and notice files under
`/usr/share/doc/aurora-player/third-party/`.

## Downloads and Linux packages

Download tested build artifacts from [GitHub Releases](https://github.com/Namnarak/Aurora-Player/releases).

- **AppImage:** standalone x86_64 portable build
- **DEB:** Ubuntu 26.04
- **RPM:** Fedora 44
- **Flatpak bundle:** install from downloaded file
- **AUR recipes:** aurora, aurora-git, aurora-bin (manual publication is separate)

After the distribution pipeline passes, the rolling development prerelease is
available at [continuous](https://github.com/Namnarak/Aurora-Player/releases/tag/continuous).
The Flathub listing and signed APT/DNF repositories are **not yet published**.
See [Distribution Guide](docs/DISTRIBUTION.md) for installation and release details.

## Build

The build uses CMake, Ninja, a C++17 compiler, pkg-config, and the native
SDL3, Vulkan, OpenSSL, GTK/libadwaita, WebKitGTK, curl, libyaml, minizip, and
capstone development packages for your distribution.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
cmake --install build --prefix /usr/local
```

To build and run the project tests, configure with `-DBUILD_TESTING=ON`, then
run `ctest --test-dir build --output-on-failure`.

## Player MCP

Configure an MCP client to start Aurora Player's local control server:

```json
{
  "mcpServers": {
    "Aurora_Player": {
      "command": "aurora_mcp"
    }
  }
}
```

The Player MCP exposes Player status, launch, stop, and bounded log-tail tools.
Studio launch and Studio Quick Connect tools are provided by the separate
Aurora Studio project.

## License

Aurora Player source is licensed under Apache-2.0. See [LICENSE](LICENSE).
Third-party components retain their own licenses and notices under
[`third_party/`](third_party/). First-party files moved from the pre-split
Aurora tree are listed with their author and license provenance in
[`LICENSES/PROVENANCE.md`](LICENSES/PROVENANCE.md).
