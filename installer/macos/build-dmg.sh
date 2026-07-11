#!/usr/bin/env bash
# Build a macOS .app bundle + .dmg from the wxnote build output. Run from the repo root after
# `cmake --build build --target wxnote`:
#   installer/macos/build-dmg.sh [arch]
# `arch` (arm64 or x86_64) is only used to name the output file - it must match whatever
# CMAKE_OSX_ARCHITECTURES the build itself was actually configured with (see build.yml), since this
# script has no way to inspect the already-built binary's arch itself. Defaults to `uname -m` for a
# local, non-CI build on a single-arch machine.
# Produces build/installer/wxNote-<version>-<arch>.dmg
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

ARCH="${1:-$(uname -m)}"

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNote VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
APPDIR="build/wxNote.app"
OUTDIR="build/installer"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/Contents/MacOS" "$APPDIR/Contents/Resources" "$OUTDIR"

# Co-locate resources with the binary in Contents/MacOS (rather than the conventional
# Contents/Resources split) - the app resolves every resource path relative to its own executable
# (wxStandardPaths::Get().GetExecutablePath() in src/main.cpp), so this layout needs zero runtime
# code changes to work. Same reasoning as installer/linux/build-appimage.sh and build-deb.sh: this
# project has no macOS machine to verify a resource-path code change against - only CI.
cp -r build/bin/. "$APPDIR/Contents/MacOS/"
rm -rf "$APPDIR/Contents/MacOS/nib/nib_test_plugin."* "$APPDIR/Contents/MacOS/plugins"

# App icon: rasterize the SVG (sips can't read SVG directly) via librsvg, then build a proper
# multi-resolution .iconset for iconutil. librsvg is a fast Homebrew install on GitHub's
# macos-latest runners (bottled, no compile).
brew install --quiet librsvg
rsvg-convert -w 1024 -h 1024 resources/wxnote.svg -o build/icon-src.png
ICONSET="build/wxnote.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" build/icon-src.png --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" build/icon-src.png --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APPDIR/Contents/Resources/wxnote.icns"

cat > "$APPDIR/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>wxNote</string>
    <key>CFBundleDisplayName</key><string>wxNote</string>
    <key>CFBundleIdentifier</key><string>com.wxnote.app</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleExecutable</key><string>wxnote</string>
    <key>CFBundleIconFile</key><string>wxnote.icns</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>GPL v3 - see LICENSE</string>
</dict>
</plist>
EOF

chmod +x "$APPDIR/Contents/MacOS/wxnote"

# Pack into a .dmg with a symlink to /Applications for the standard drag-to-install UX.
DMGROOT="build/dmg-root"
rm -rf "$DMGROOT"; mkdir -p "$DMGROOT"
cp -r "$APPDIR" "$DMGROOT/"
ln -s /Applications "$DMGROOT/Applications"
hdiutil create -volname "wxNote (${ARCH})" -srcfolder "$DMGROOT" -ov -format UDZO "$OUTDIR/wxNote-${VERSION}-${ARCH}.dmg"
echo "Built $OUTDIR/wxNote-${VERSION}-${ARCH}.dmg"
