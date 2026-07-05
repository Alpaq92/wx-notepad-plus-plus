#!/usr/bin/env bash
# Build a .rpm package from the wxnpp build output. Run from the repo root after
# `cmake --build build --target wxnpp`:
#   installer/linux/build-rpm.sh
# Produces build/installer/wxnpp-<version>-1.*.x86_64.rpm
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root, as an absolute path (needed for the _srcdir macro below)

VERSION="0.2.0"
TOPDIR="build/rpmbuild"
OUTDIR="build/installer"
SRCDIR="$(pwd)"

rm -rf "$TOPDIR"
mkdir -p "$TOPDIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS,BUILDROOT} "$OUTDIR"

rpmbuild --define "_topdir $SRCDIR/$TOPDIR" \
         --define "_version $VERSION" \
         --define "_srcdir $SRCDIR" \
         -bb installer/linux/wxnpp.spec

built=$(find "$TOPDIR/RPMS" -name '*.rpm' | head -1)
if [ -z "$built" ]; then echo "rpmbuild did not produce an RPM" >&2; exit 1; fi
mv "$built" "$OUTDIR/"
echo "Built $OUTDIR/$(basename "$built")"
