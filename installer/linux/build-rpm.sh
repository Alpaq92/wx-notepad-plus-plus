#!/usr/bin/env bash
# Build a .rpm package from the wxnote build output. Run from the repo root after
# `cmake --build build --target wxnote`:
#   installer/linux/build-rpm.sh
# Produces build/installer/wxnote-<version>-1.*.x86_64.rpm
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root, as an absolute path (needed for the _srcdir macro below)

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
# out of sync with it again (every packaging script independently hardcoded its own version string
# and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
VERSION="$(sed -n 's/.*project(wxNote VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
TOPDIR="build/rpmbuild"
OUTDIR="build/installer"
SRCDIR="$(pwd)"

rm -rf "$TOPDIR"
mkdir -p "$TOPDIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS,BUILDROOT} "$OUTDIR"

rpmbuild --define "_topdir $SRCDIR/$TOPDIR" \
         --define "_version $VERSION" \
         --define "_srcdir $SRCDIR" \
         -bb installer/linux/wxnote.spec

built=$(find "$TOPDIR/RPMS" -name '*.rpm' | head -1)
if [ -z "$built" ]; then echo "rpmbuild did not produce an RPM" >&2; exit 1; fi
mv "$built" "$OUTDIR/"
echo "Built $OUTDIR/$(basename "$built")"
