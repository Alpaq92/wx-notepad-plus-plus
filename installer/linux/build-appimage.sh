#!/usr/bin/env bash
# Build a Linux AppImage from the wxnote build output. Run from the repo root after
# `cmake --build build --target wxnote`:
#   installer/linux/build-appimage.sh
# Produces build/installer/wxNote-<version>-<arch>.AppImage (x86_64 or aarch64, from the build
# host - linuxdeploy publishes a native binary for both).
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNote VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
ARCH="$(uname -m)"   # x86_64 / aarch64 - also read by linuxdeploy's embedded appimagetool
APPDIR="build/AppDir"
OUTDIR="build/installer"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$OUTDIR"

# Co-locate resources with the binary (usr/bin/icons, usr/bin/themes, ...) rather than the
# traditional FHS split (exe in usr/bin, resources in usr/share) - the app resolves every
# resource path relative to its own executable (wxStandardPaths::Get().GetExecutablePath() in
# src/main.cpp), and AppRun launches the real binary from inside usr/bin, so this layout needs
# zero runtime code changes to work, which matters since this project has no Linux machine to
# verify a code change against - only CI.
cp -r build/bin/. "$APPDIR/usr/bin/"
rm -rf "$APPDIR/usr/bin/nib/nib_test_plugin.so" "$APPDIR/usr/bin/nib/example" "$APPDIR/usr/bin/plugins"   # dev-only test artifacts
cp installer/linux/wxnote.desktop "$APPDIR/wxnote.desktop"
cp resources/wxnote.svg "$APPDIR/wxnote.svg"

if [ ! -x linuxdeploy.AppImage ]; then
    curl -fL -o linuxdeploy.AppImage "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
    chmod +x linuxdeploy.AppImage
fi

# --appimage-extract-and-run: GitHub-hosted runners don't have FUSE configured for AppImages to
# mount themselves directly - extracting first is the standard CI workaround.
VERSION="$VERSION" ARCH="$ARCH" ./linuxdeploy.AppImage --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/wxnote" \
    --desktop-file "$APPDIR/wxnote.desktop" \
    --icon-file "$APPDIR/wxnote.svg" \
    --output appimage

# linuxdeploy's exact output filename depends on its desktop-file-derived AppName, which isn't
# worth hardcoding a guess for - find whatever .AppImage it just produced in the cwd.
built=$(find . -maxdepth 1 -name '*.AppImage' ! -name 'linuxdeploy.AppImage' | head -1)
if [ -z "$built" ]; then echo "linuxdeploy did not produce an AppImage" >&2; exit 1; fi
mv "$built" "$OUTDIR/wxNote-${VERSION}-${ARCH}.AppImage"
echo "Built $OUTDIR/wxNote-${VERSION}-${ARCH}.AppImage"
