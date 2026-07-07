#!/usr/bin/env bash
# Build a .flatpak single-file bundle from the wxnpp build output. Run from the repo root after
# `cmake --build build --target wxnpp`, with flatpak-builder installed and the
# org.gnome.Platform//46 + org.gnome.Sdk//46 runtimes already available (see build.yml's "Install
# Flatpak runtime" step):
#   installer/linux/build-flatpak.sh
# Produces build/installer/wxNotepadPlusPlus-<version>.flatpak
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

APP_ID="io.github.Alpaq92.WxNotepadPlusPlus"
# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNotepadPlusPlus VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
OUTDIR="build/installer"
BUILDDIR="build/flatpak-build"
REPODIR="build/flatpak-repo"

mkdir -p "$OUTDIR"
rm -rf "$BUILDDIR" "$REPODIR"

# flatpak-builder's cleanup phase runs `appstreamcli compose` to validate/rasterize the installed
# icon, and the Ubuntu-packaged appstreamcli is commonly built without librsvg support - it can't
# decode our SVG icon at all, failing with an opaque "file-read-error"/"filters-but-no-output"
# rather than a clear "no SVG support" message. Rasterize to PNG ourselves so icon processing
# never depends on that optional (and here, absent) library.
rsvg-convert -w 256 -h 256 "resources/wxNotepad++.svg" -o "build/flatpak-icon-256.png"

flatpak-builder --force-clean --user --repo="$REPODIR" "$BUILDDIR" "installer/linux/${APP_ID}.yml"
flatpak build-bundle "$REPODIR" "$OUTDIR/wxNotepadPlusPlus-${VERSION}.flatpak" "$APP_ID"
echo "Built $OUTDIR/wxNotepadPlusPlus-${VERSION}.flatpak"
