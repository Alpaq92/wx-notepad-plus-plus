#!/usr/bin/env bash
# Copy wxNote's application payload out of a build's bin/ directory into a package's staging tree:
#   installer/stage-payload.sh <build-bin> <dest>
# Every Linux and macOS package stages through this one script - the .deb, .rpm, AppImage, Flatpak and
# .dmg all keep the resources beside the executable, so they all ship the same tree.
#
# It copies a LIST, never build/bin as a whole: CI builds the `selftests` target before it packages, and
# every suite lands in build/bin next to wxnote, so the wholesale copy these packages used to make
# shipped them all - 0.20.0's .deb installed 16 test programs into /opt/wxnote, bridge_selftest (22 MB)
# among them. A list cannot pick up a new test, and a missing file stops the build here instead of
# shipping a package without it.
#
# The list is the one installer/windows/wxnote.nsi's SecCore section and build.yml's "Package zip
# (Windows)" step ship - change all three together.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <build-bin> <dest>" >&2
    exit 2
fi
SRC="$1"
DEST="$2"

mkdir -p "$DEST/nib"
install -m 755 "$SRC/wxnote" "$DEST/"
install -m 644 "$SRC/contextmenu.yaml" "$DEST/"
for d in icons icons-solar icons-iconpark icons-streamline themes dictionaries fonts lexers locale; do
    cp -R "$SRC/$d" "$DEST/"
done
# The catalogs' sources stay behind (each .po, and the .pot template): the app reads only the .mo files.
find "$DEST/locale" -type f \( -name '*.po' -o -name '*.pot' \) -delete
# The three shipped bridge plugins - not nib_test_plugin (a dev-only loader test) or nib/example (the
# recompiled-plugin compile proof). .so on macOS too: that is what CMake names a MODULE library there.
for p in npp_bridge udl_compat npp_compat; do
    install -m 755 "$SRC/nib/$p.so" "$DEST/nib/"
done

# install and cp already fail on anything above that is missing; these are files inside a copied
# directory, whose absence would not. lexer.lua above all: without it there is no Scintillua
# highlighting at all, and no smoke test notices.
for must in themes/Default.yaml themes/DarkModeDefault.yaml lexers/lexer.lua; do
    if [ ! -f "$DEST/$must" ]; then
        echo "stage-payload.sh: the payload is missing $must" >&2
        exit 1
    fi
done
