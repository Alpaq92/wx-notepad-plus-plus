#!/usr/bin/env bash
# Build a .deb package from the wxnpp build output. Run from the repo root after
# `cmake --build build --target wxnpp`:
#   installer/linux/build-deb.sh
# Produces build/installer/wxnpp_<version>_amd64.deb
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNotepadPlusPlus VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
PKGDIR="build/deb-pkg"
OUTDIR="build/installer"

rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/DEBIAN" "$PKGDIR/opt/wxnpp" "$PKGDIR/usr/bin" \
         "$PKGDIR/usr/share/applications" "$PKGDIR/usr/share/icons/hicolor/scalable/apps" \
         "$OUTDIR"

# Installs to /opt/wxnpp (exe + resources co-located) with a /usr/bin symlink, rather than the
# traditional FHS split (exe in /usr/bin, resources in /usr/share) - same reasoning as
# build-appimage.sh: the app's resource lookups are all relative to its own executable path, and
# this layout needs no runtime code changes to work, which matters given this project has no
# Linux machine to verify a code change against - only CI.
cp -r build/bin/. "$PKGDIR/opt/wxnpp/"
rm -rf "$PKGDIR/opt/wxnpp/nib/nib_test_plugin.so" "$PKGDIR/opt/wxnpp/plugins"   # dev-only test artifacts
ln -s /opt/wxnpp/wxnpp "$PKGDIR/usr/bin/wxnpp"
cp installer/linux/wxnpp.desktop "$PKGDIR/usr/share/applications/wxnpp.desktop"
cp resources/wxNotepad++.svg "$PKGDIR/usr/share/icons/hicolor/scalable/apps/wxnpp.svg"

cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: wxnpp
Version: ${VERSION}
Section: editors
Priority: optional
Architecture: amd64
Maintainer: wxNote Project <noreply@wx-notepad-plus-plus.invalid>
Description: Experimental cross-platform Notepad++-faithful editor
 wxWidgets-based reimplementation of Notepad++, reusing Scintilla and Lexilla,
 with an original permissive plugin API and optional Notepad++ ABI plugin
 compatibility.
EOF

dpkg-deb --build --root-owner-group "$PKGDIR" "$OUTDIR/wxnpp_${VERSION}_amd64.deb"
echo "Built $OUTDIR/wxnpp_${VERSION}_amd64.deb"
