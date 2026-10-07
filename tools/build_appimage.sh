#!/usr/bin/env bash
# Builds the Linux / Steam Deck AppImage (run inside Linux or WSL from the
# repository root). Adapted from King Kong Recompiled's script.
#
# Needs the translated game code in ewj/generated/default (made by setup.ps1 on
# Windows, or `rexglue codegen` on Linux, from your own package) and your own
# default.xex in ewj/assets. The code is the same for every platform, so a
# Windows checkout can be built here through /mnt/c.
#
#   tools/build_appimage.sh [version]
#
# Output: dist/EarthwormJimHD-<version>-linux-x86_64.AppImage
set -euo pipefail

VERSION="${1:-dev}"
SDK_VERSION=0.10.0
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Build on the Linux filesystem: much faster than /mnt/c, and file names keep their case.
WORK="${EWJ_WORK:-$HOME/ewj-linux-build}"
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo

echo "== Packages"
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq wget gnupg lsb-release ca-certificates >/dev/null
if ! command -v clang++-20 >/dev/null && ! $SUDO apt-get install -y -qq clang-20 lld-20 >/dev/null; then
  # Older releases: clang 20 from the official LLVM repository.
  CODENAME="$(lsb_release -cs)"
  wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | $SUDO gpg --dearmor --yes -o /usr/share/keyrings/llvm.gpg
  echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/$CODENAME/ llvm-toolchain-$CODENAME-20 main" |
    $SUDO tee /etc/apt/sources.list.d/llvm-20.list
  $SUDO apt-get update -qq
  $SUDO apt-get install -y -qq clang-20 lld-20 >/dev/null
fi
$SUDO apt-get install -y -qq g++ cmake ninja-build pkg-config file unzip rsync desktop-file-utils \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev \
  libxkbcommon-dev libwayland-dev libdecor-0-dev libegl-dev libgl-dev libvulkan-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libgtk-3-dev zenity >/dev/null

echo "== Source"
mkdir -p "$WORK/ewj" "$WORK/third_party" "$WORK/tools"
rsync -a --delete --exclude out --exclude assets --exclude image.bin --exclude '*.log' "$ROOT/ewj/" "$WORK/ewj/"
rsync -a --delete "$ROOT/third_party/trg-launcher/" "$WORK/third_party/trg-launcher/"
cp "$ROOT/tools/patch_setjmp.cmake" "$WORK/tools/"
mkdir -p "$WORK/tools/rexglue" "$WORK/ewj/assets"
# The build checks the translated code against your default.xex (from your own package).
# (ewj/assets is a Windows junction to game/, which /mnt/c may not follow.)
if [ -f "$ROOT/game/default.xex" ]; then cp "$ROOT/game/default.xex" "$WORK/ewj/assets/"; else cp "$ROOT/ewj/assets/default.xex" "$WORK/ewj/assets/"; fi

echo "== ReXGlue SDK $SDK_VERSION (linux-amd64)"
if [ ! -d "$WORK/tools/rexglue/linux-amd64" ]; then
  wget -q -O /tmp/rexglue-linux.zip \
    "https://github.com/rexglue/rexglue-sdk/releases/download/v$SDK_VERSION/rexglue-sdk-$SDK_VERSION-linux-amd64.zip"
  unzip -q -o /tmp/rexglue-linux.zip -d "$WORK/tools/rexglue"
fi

echo "== Build"
cd "$WORK/ewj"
cmake --preset ewj-linux-release
cmake --build --preset ewj-linux-release
BIN="$WORK/ewj/out/build/ewj-linux-release"

echo "== AppDir"
APPDIR="$WORK/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
cp "$BIN/earthworm_jim_hd" "$APPDIR/usr/bin/"
# The runtime, the GPU plugin and anything else the build put beside the binary.
find "$BIN" -maxdepth 1 -name '*.so*' -exec cp -a {} "$APPDIR/usr/bin/" \;
for lib in librexruntime.so librexgpu-xenos.so libTracyClient.so; do
  [ -e "$BIN/$lib" ] || [ ! -e "$WORK/tools/rexglue/linux-amd64/lib/$lib" ] || \
    cp -a "$WORK/tools/rexglue/linux-amd64/lib/$lib" "$APPDIR/usr/bin/"
done
# The SDK needs a GCC 13 C++ runtime (GLIBCXX_3.4.32); bundle it for older
# systems. SteamOS 3 has it already; AppRun only uses the bundled copy when the
# system's is older.
for lib in libstdc++.so.6 libgcc_s.so.1; do
  cp -L "$(g++ -print-file-name=$lib 2>/dev/null || true)" "$APPDIR/usr/lib/" 2>/dev/null ||
    cp -L "/usr/lib/x86_64-linux-gnu/$lib" "$APPDIR/usr/lib/"
done
cp "$ROOT/docs/images/icon.png" "$APPDIR/earthworm_jim_hd.png"
cat > "$APPDIR/earthworm_jim_hd.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Earthworm Jim HD
Comment=Earthworm Jim HD, Xbox 360 version, native PC port
Exec=earthworm_jim_hd
Icon=earthworm_jim_hd
Categories=Game;
Terminal=false
EOF
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
LIBS="$HERE/usr/bin"
# Use the bundled C++ runtime only when the system's is older than the one
# the game needs; a newer system copy is kept for the graphics drivers.
SYS_CXX="$(ldconfig -p 2>/dev/null | awk '/libstdc\+\+\.so\.6 .*x86-64/ {print $NF; exit}')"
if [ -z "$SYS_CXX" ] || ! grep -q GLIBCXX_3.4.32 "$SYS_CXX" 2>/dev/null; then LIBS="$LIBS:$HERE/usr/lib"; fi
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/earthworm_jim_hd" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "== Missing libraries check"
LD_LIBRARY_PATH="$APPDIR/usr/bin" ldd "$APPDIR/usr/bin/earthworm_jim_hd" | grep "not found" || echo "none"

echo "== AppImage"
TOOL="$WORK/appimagetool-x86_64.AppImage"
[ -x "$TOOL" ] || { wget -q -O "$TOOL" \
  https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage && chmod +x "$TOOL"; }
mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/EarthwormJimHD-$VERSION-linux-x86_64.AppImage"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT"
ls -la "$OUT"
