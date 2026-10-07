#!/bin/sh
# Build a FreeBSD package from the wxnote build output. Run from the repo root, on FreeBSD, after
# `cmake --build build --target wxnote`:
#   sh installer/freebsd/build-pkg.sh
# Produces build/installer/wxnote-<version>-freebsd<major>-<arch>.pkg (e.g. -freebsd14-amd64), for
# `pkg add`. A package records the FreeBSD major version it was built on, and pkg refuses it on any
# other, so CI builds one per supported major. POSIX sh rather than bash: FreeBSD ships no bash.
set -eu
cd "$(dirname "$0")/../.."   # repo root

# Read straight from the top-level CMakeLists.txt's project(... VERSION ...), like every other packaging
# script, so the package can never be labelled with a stale version.
VERSION="$(sed -n 's/.*project(wxNote VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
MAJOR="$(freebsd-version -u | cut -d. -f1)"   # 14.3-RELEASE-p2 -> 14
ARCH="$(uname -p)"                             # amd64 - pkg's own name for it
PREFIX=/usr/local
STAGE="build/freebsd-pkg"
APPDIR="$STAGE$PREFIX/lib/wxnote"
OUTDIR="build/installer"

rm -rf "$STAGE"
mkdir -p "$STAGE$PREFIX/bin" "$STAGE$PREFIX/share/applications" \
         "$STAGE$PREFIX/share/icons/hicolor/scalable/apps" "$STAGE$PREFIX/share/doc/wxnote" "$OUTDIR"

# The program and its resources stay together, as on every other platform - it looks them up next to
# its own executable - in /usr/local/lib/wxnote, where FreeBSD keeps an application's private files.
# stage-payload.sh holds the one list every package ships (never build/bin whole: it also holds every
# selftest CI has just run), and fails on anything missing. Run by sh: FreeBSD has no bash.
sh installer/stage-payload.sh build/bin "$APPDIR"
strip "$APPDIR/wxnote" "$APPDIR"/nib/*.so   # what the ports framework does to every program it installs

ln -s ../lib/wxnote/wxnote "$STAGE$PREFIX/bin/wxnote"   # relative, so it holds under `pkg -r <root>` too
cp installer/linux/wxnote.desktop "$STAGE$PREFIX/share/applications/wxnote.desktop"   # freedesktop, not Linux-specific
cp resources/wxnote.svg "$STAGE$PREFIX/share/icons/hicolor/scalable/apps/wxnote.svg"
# The licence travels with the package (Apache-2.0 4(a)): NOTICE carries the third-party attributions
# and the licence texts they ask for.
cp LICENSE NOTICE "$STAGE$PREFIX/share/doc/wxnote/"

# Dependencies: the package that provides each shared library the program and its plugins link
# directly (their NEEDED entries; pkg resolves what those need in turn). Base-system libraries belong to
# no package and drop out - as do pkgbase's FreeBSD-* packages, on a system that has them, since
# requiring those would make the package uninstallable everywhere else.
DEPS=""
for p in $(for f in "$APPDIR/wxnote" "$APPDIR"/nib/*.so; do
             readelf -d "$f" | sed -n 's/.*NEEDED.*\[\(.*\)\].*/\1/p'
           done | sort -u | while read -r lib; do
             pkg shlib -qP "$lib" 2>/dev/null | head -n 1
           done | sort -u); do
  case "$p" in FreeBSD-*) continue ;; esac
  # pkg shlib names name-version; a version never contains '-', so the name is all before the last one
  DEPS="$DEPS$(pkg query '  "%n": { origin: "%o", version: "%v" },' "${p%-*}")
"
done
[ -n "$DEPS" ] || { echo "found no dependencies - is GTK installed from packages?" >&2; exit 1; }

cat > "$STAGE/+MANIFEST" <<EOF
name: wxnote
version: "$VERSION"
origin: editors/wxnote
comment: "Experimental cross-platform text editor"
www: "https://alpaq92.github.io/wx-notepad-plus-plus/"
maintainer: "noreply@wx-notepad-plus-plus.invalid"
prefix: "$PREFIX"
categories: [ "editors" ]
# The editor is Apache-2.0; the three bridge plugins in nib/ are GPL-3.0-or-later, the bundled fonts
# OFL-1.1 or MIT, some icon sets CC-BY-4.0 - see LICENSING.md and NOTICE.
licenselogic: "multi"
licenses: [ "APACHE20", "GPLv3+", "OFL11", "MIT", "CC-BY-4.0" ]
desc: <<EOD
wxWidgets-based cross-platform text editor built on the Scintilla and Lexilla
editing engines, with an original permissive plugin API (Nib) and an optional
Windows compatibility bridge for legacy Notepad++ plugin binaries.
EOD
deps: {
$DEPS}
EOF

# Every staged file, relative to the prefix. No scripts: pkg's own triggers refresh the desktop, MIME
# and icon caches when a package installs into share/applications and share/icons.
{
  echo "@owner root"
  echo "@group wheel"
  (cd "$STAGE$PREFIX" && find . \( -type f -o -type l \) | sed 's|^\./||' | sort)
} > "$STAGE/plist"

pkg create -M "$STAGE/+MANIFEST" -p "$STAGE/plist" -r "$STAGE" -o "$STAGE"
built=$(ls "$STAGE/wxnote-${VERSION}".* 2>/dev/null | head -n 1)   # .pkg, whatever format pkg defaults to
[ -n "$built" ] || { echo "pkg create did not produce a package" >&2; exit 1; }
PKGFILE="$OUTDIR/wxnote-${VERSION}-freebsd${MAJOR}-${ARCH}.pkg"
mv "$built" "$PKGFILE"
pkg info -F "$PKGFILE"
pkg info -dF "$PKGFILE"
echo "Built $PKGFILE"
