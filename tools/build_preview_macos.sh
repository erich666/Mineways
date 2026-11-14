#!/usr/bin/env bash
# Build one universal app with pinned static wxWidgets and built-in dependencies.
set -euo pipefail

preview_root=$(cd "$(dirname "$0")/.." && pwd)
preview_work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mineways-preview-wx.XXXXXX")
trap 'rm -rf "$preview_work"' EXIT

curl --fail --location --retry 3 \
    https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.4/wxWidgets-3.3.4.tar.bz2 \
    --output "$preview_work/wx.tar.bz2"
printf '%s  %s\n' 815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98 "$preview_work/wx.tar.bz2" | shasum -a 256 -c -
tar -xjf "$preview_work/wx.tar.bz2" -C "$preview_work"
mkdir "$preview_work/build"
export MACOSX_DEPLOYMENT_TARGET=26.0
(
    cd "$preview_work/build"
    ../wxWidgets-3.3.4/configure --prefix="$preview_work/install" \
        --with-osx_cocoa --enable-universal_binary=x86_64,arm64 --with-macosx-version-min=26.0 \
        --disable-shared --disable-debug --disable-tests --without-subdirs \
        --disable-webview --disable-mediactrl --without-opengl \
        --disable-stc --disable-richtext --disable-aui --disable-ribbon --disable-propgrid \
        --with-libjpeg=builtin --with-libpng=builtin --with-libtiff=builtin \
        --with-zlib=builtin --with-expat=builtin --with-regex=builtin \
        --without-liblzma --without-libjbig --without-libwebp --without-libmspack --without-libcurl
    make -j3
    make install
)
export PATH="$preview_work/install/bin:$PATH"
test "$(wx-config --version)" = 3.3.4
make -C "$preview_root/Mac" clean
make -C "$preview_root/Mac" ARCH="x86_64 arm64" -j3 app
make -C "$preview_root/Mac" ARCH="$(uname -m)" test test-mods test-mod-limits
/usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string 26.0" "$preview_root/Mac/Mineways.app/Contents/Info.plist"
python3 - "$preview_root" <<'PY'
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]) / 'tools'))
from merge_preview_macos import dependency_contract
from package_preview import check_architecture
check_architecture(Path(sys.argv[1]) / 'Mac/Mineways.app', 'macos-universal')
binary = Path(sys.argv[1]) / 'Mac/Mineways.app/Contents/MacOS/mineways'
if dependency_contract(binary):
    raise SystemExit('Static preview unexpectedly depends on bundled dynamic libraries')
PY
codesign --force --deep --sign - "$preview_root/Mac/Mineways.app"
codesign --verify --deep --strict "$preview_root/Mac/Mineways.app"
