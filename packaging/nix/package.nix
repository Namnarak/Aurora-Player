{
  lib,
  stdenv,
  cmake,
  git,
  lld,
  ninja,
  pkg-config,
  vulkan-headers,
  makeWrapper,

  capstone,
  curl,
  fontconfig,
  gamemode,
  glib,
  glib-networking,
  gtk4,
  hicolor-icon-theme,
  libadwaita,
  elfutils,
  libglvnd,
  libplacebo,
  libpng,
  libsoup_3,
  libutf8proc,
  libyaml,
  minizip,
  nlohmann_json,
  openssl,
  sdl3,
  sdl3-ttf,
  vulkan-loader,
  webkitgtk_6_0,
  zlib,
}:
let
  src = ../..;

  lines = lib.splitString "\n" (builtins.readFile (lib.path.append src "CMakeLists.txt"));

  matches = map (builtins.match "[[:space:]]*VERSION[[:space:]]+([0-9][0-9.]*).*") lines;

  match = lib.findFirst (m: m != null) null matches;

  version =
    if match == null then throw "could not find VERSION in CMakeLists.txt" else builtins.elemAt match 0;
in
stdenv.mkDerivation (finalAttrs: {
  pname = "aurora";
  inherit version;
  inherit src;

  nativeBuildInputs = [
    cmake
    git
    lld
    ninja
    pkg-config
    vulkan-headers
    makeWrapper
  ];

  buildInputs = [
    capstone
    curl
    fontconfig
    # Loaded with dlmopen at runtime. Keeping it here also adds libgamemode.so.0
    # to the wrapped binaries' and development shell's LD_LIBRARY_PATH.
    gamemode
    glib
    glib-networking
    gtk4
    hicolor-icon-theme
    libadwaita
    elfutils
    libglvnd
    libplacebo
    libpng
    libsoup_3
    libutf8proc
    libyaml
    minizip
    nlohmann_json
    openssl
    sdl3
    sdl3-ttf
    vulkan-loader
    webkitgtk_6_0
    zlib
  ];

  # nixpkgs installs the header we need under include/libutf8proc
  NIX_CFLAGS_COMPILE = "-I${libutf8proc}/include/libutf8proc";

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DAURORA_DEFAULT_COMPATIBILITY_MANIFEST=${placeholder "out"}/share/aurora/metadata/roblox_compatibility.json"
    "-DAURORA_DEFAULT_SIGNING_TRUST_MANIFEST=${placeholder "out"}/share/aurora/metadata/roblox_signing_certificates.json"
    "-DBUILD_TESTING=OFF"
  ];

  postInstall = ''
    for binary in $out/bin/*; do
    	wrapProgram "$binary" --prefix LD_LIBRARY_PATH : ${lib.makeLibraryPath finalAttrs.buildInputs} --prefix GIO_EXTRA_MODULES : "${glib-networking}/lib/gio/modules"
    done
  '';

  fixupPhase = ''
    install -Dm644 ${finalAttrs.src}/LICENSE "$out/share/licenses/${finalAttrs.pname}/LICENSE"
    install -Dm644 ${finalAttrs.src}/third_party/noto/LICENSES/Noto-Fonts-OFL-1.1.txt "$out/share/licenses/${finalAttrs.pname}/third-party/noto/Noto-Fonts-OFL-1.1.txt"
    install -Dm644 ${finalAttrs.src}/third_party/noto/LICENSES/Noto-CJK-OFL-1.1.txt "$out/share/licenses/${finalAttrs.pname}/third-party/noto/Noto-CJK-OFL-1.1.txt"
  '';

  meta = with lib; {
    description = "Android x86-64 Roblox compatibility runtime for Linux";
    homepage = "https://github.com/Namnarak/Aurora-Player";
    license = with licenses; [ asl20 ofl ];
    platforms = platforms.unix;
    mainProgram = "aurora";
  };
})
