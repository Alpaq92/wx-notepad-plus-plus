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

# Icons: rasterize the SVG (sips can't read SVG directly) via librsvg, then build a proper
# multi-resolution .iconset for iconutil. librsvg is a fast Homebrew install on GitHub's
# macos-latest runners (bottled, no compile).
brew install --quiet librsvg

# $1 = source .svg, $2 = basename of the .icns to produce in the bundle's Resources.
# Two icons now go through this - the application's and the document type's - and the iconset dance
# (rasterize once at 1024, then every size and its @2x) is identical for both.
make_icns() {
    rsvg-convert -w 1024 -h 1024 "$1" -o "build/$2-src.png"
    iconset="build/$2.iconset"
    rm -rf "$iconset"; mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "build/$2-src.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2))
        sips -z "$double" "$double" "build/$2-src.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$iconset" -o "$APPDIR/Contents/Resources/$2.icns"
}

make_icns resources/wxnote.svg     wxnote
# The document icon Finder draws for files wxNote handles (CFBundleTypeIconFile below). Deliberately
# the canonical resources/wxnote-doc.svg rather than one of the -a/-b/-c variants beside it: that file
# is written by tools/make_doc_icon.py as the exact vector twin of resources/wxnote-doc.ico, so macOS
# and Windows cannot end up shipping two different document icons.
make_icns resources/wxnote-doc.svg wxnote-doc

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
    <!-- Document types. LSHandlerRank is the whole story here: "Alternate" puts wxNote in the Open With
         menu for text and source files without claiming to own them, while public.data at "None" keeps
         it out of that menu for arbitrary binaries yet still lets a user reach it through Open With >
         Other with "All Applications" - and then make it the default. That is what "selectable as the
         default for any file" means on macOS; nothing an installer does can set a default here either.
         Requires WxnApp::MacOpenFiles (src/main.cpp): Finder delivers these as an Apple Event, not argv. -->
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key><string>Text Document</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>CFBundleTypeIconFile</key><string>wxnote-doc.icns</string>
            <key>LSHandlerRank</key><string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.text</string>
                <string>public.plain-text</string>
                <string>public.utf8-plain-text</string>
                <string>public.utf16-plain-text</string>
                <string>public.source-code</string>
                <string>public.script</string>
                <string>public.shell-script</string>
                <string>public.xml</string>
                <string>public.json</string>
                <string>public.yaml</string>
                <string>public.comma-separated-values-text</string>
                <string>public.tab-separated-values-text</string>
                <string>public.log</string>
                <string>public.patch-file</string>
                <string>net.daringfireball.markdown</string>
            </array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key><string>Folder</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSHandlerRank</key><string>None</string>
            <key>LSItemContentTypes</key><array><string>public.folder</string></array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key><string>Any File</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSHandlerRank</key><string>None</string>
            <key>LSItemContentTypes</key><array><string>public.data</string></array>
        </dict>
    </array>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>Apache-2.0 - see LICENSE</string>
</dict>
</plist>
EOF

chmod +x "$APPDIR/Contents/MacOS/wxnote"

# Optional codesigning. The release CI exports MACOS_SIGN_IDENTITY (a "Developer ID Application: ..."
# identity) into the environment when the Apple cert secret is present; otherwise this is skipped and
# the .dmg ships unsigned exactly as before. Signed here - BEFORE the app is packed into the .dmg - so
# the bundle inside the image is what carries the signature. Deep + hardened runtime + a secure
# timestamp are what notarization (done on the .dmg back in the workflow) requires. See docs/SIGNING.md.
if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
    echo "codesigning $APPDIR as: $MACOS_SIGN_IDENTITY"
    codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGN_IDENTITY" "$APPDIR"
    codesign --verify --deep --strict --verbose=2 "$APPDIR"
fi

# Pack into a .dmg with a symlink to /Applications for the standard drag-to-install UX.
DMGROOT="build/dmg-root"
rm -rf "$DMGROOT"; mkdir -p "$DMGROOT"
cp -r "$APPDIR" "$DMGROOT/"
ln -s /Applications "$DMGROOT/Applications"
hdiutil create -volname "wxNote (${ARCH})" -srcfolder "$DMGROOT" -ov -format UDZO "$OUTDIR/wxNote-${VERSION}-${ARCH}.dmg"
echo "Built $OUTDIR/wxNote-${VERSION}-${ARCH}.dmg"
