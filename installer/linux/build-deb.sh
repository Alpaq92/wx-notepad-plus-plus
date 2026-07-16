#!/usr/bin/env bash
# Build a .deb package from the wxnote build output. Run from the repo root after
# `cmake --build build --target wxnote`:
#   installer/linux/build-deb.sh
# Produces build/installer/wxnote_<version>_<arch>.deb (amd64 or arm64, from the build host -
# the same script serves both the x86_64 and the ARM CI runners).
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNote VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
DEB_ARCH="${DEB_ARCH_OVERRIDE:-$(dpkg --print-architecture)}"   # amd64 / arm64 (build host), or an explicit
                                                               # target for cross builds (e.g. riscv64)
PKGDIR="build/deb-pkg"
OUTDIR="build/installer"

rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/DEBIAN" "$PKGDIR/opt/wxnote" "$PKGDIR/usr/bin" \
         "$PKGDIR/usr/share/applications" "$PKGDIR/usr/share/icons/hicolor/scalable/apps" \
         "$OUTDIR"

# Installs to /opt/wxnote (exe + resources co-located) with a /usr/bin symlink, rather than the
# traditional FHS split (exe in /usr/bin, resources in /usr/share) - same reasoning as
# build-appimage.sh: the app's resource lookups are all relative to its own executable path, and
# this layout needs no runtime code changes to work, which matters given this project has no
# Linux machine to verify a code change against - only CI.
cp -r build/bin/. "$PKGDIR/opt/wxnote/"
rm -rf "$PKGDIR/opt/wxnote/nib/nib_test_plugin.so" "$PKGDIR/opt/wxnote/nib/example" "$PKGDIR/opt/wxnote/plugins"   # dev-only test artifacts (nib/example is the compile-only recompiled-plugin proof)
ln -s /opt/wxnote/wxnote "$PKGDIR/usr/bin/wxnote"
cp installer/linux/wxnote.desktop "$PKGDIR/usr/share/applications/wxnote.desktop"
cp resources/wxnote.svg "$PKGDIR/usr/share/icons/hicolor/scalable/apps/wxnote.svg"

cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: wxnote
Version: ${VERSION}
Section: editors
Priority: optional
Architecture: ${DEB_ARCH}
Maintainer: wxNote Project <noreply@wx-notepad-plus-plus.invalid>
Description: Experimental cross-platform text editor
 wxWidgets-based cross-platform text editor built on the Scintilla and Lexilla
 editing engines, with an original permissive plugin API (Nib) and an optional
 Windows compatibility bridge for legacy Notepad++ plugin binaries.
EOF

dpkg-deb --build --root-owner-group "$PKGDIR" "$OUTDIR/wxnote_${VERSION}_${DEB_ARCH}.deb"
echo "Built $OUTDIR/wxnote_${VERSION}_${DEB_ARCH}.deb"
