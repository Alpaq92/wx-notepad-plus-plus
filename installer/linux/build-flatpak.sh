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
VERSION="0.1.0"
OUTDIR="build/installer"
BUILDDIR="build/flatpak-build"
REPODIR="build/flatpak-repo"

mkdir -p "$OUTDIR"
rm -rf "$BUILDDIR" "$REPODIR"

flatpak-builder --force-clean --user --repo="$REPODIR" "$BUILDDIR" "installer/linux/${APP_ID}.yml"
flatpak build-bundle "$REPODIR" "$OUTDIR/wxNotepadPlusPlus-${VERSION}.flatpak" "$APP_ID"
echo "Built $OUTDIR/wxNotepadPlusPlus-${VERSION}.flatpak"
