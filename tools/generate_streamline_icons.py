#!/usr/bin/env python3
"""Generate resources/icons-streamline/ - the "Streamline" colored toolbar icon set.

Source: Streamline "Core" free icons, "flat" style, from the first-party vector repo
    https://github.com/webalys-hq/streamline-vectors  (CC BY 4.0)
pinned at commit PINNED_SHA below. Each source SVG is fetched from raw.githubusercontent.com
at that exact commit, so a re-run reproduces the shipped files byte-for-byte even if
upstream moves.

What this script does (the modifications recorded in resources/icons-streamline/CREDITS.md):
  1. Downloads the chosen "core/flat" glyphs (two-paint flat style: light fill #8fbffa,
     dark detail #2859c5) into a local cache (build/streamline-vectors-cache/).
  2. Composites the handful of concepts the free set has no direct glyph for (zoom-in/out,
     redo, save-all, save-macro, close-all, open, terminal, indent-guide, pin, cdrom,
     executable, filetype-document, filetype-audio) from the downloaded glyphs' own paths
     plus simple primitives, keeping the pack's two-paint discipline.
  3. Re-bakes the pack's stock blue palette to Open Color (https://github.com/yeun/open-color):
         #8fbffa (light fill)  -> #69db7c  Open Color green-4
         #2859c5 (dark detail) -> #099268  Open Color teal-8
     (#fff, used only by the save-macro badge ring added here, stays white.)
  4. Validates every output: well-formed XML, only approved paints, no mask/filter/
     clipPath/style/text elements, and the file list exactly matches the default
     resources/icons/ (Tabler) manifest - the FULL 50-concept set including the
     workspace-tree device/filetype icons (the sibling iconpark/solar colored sets
     still cover only the 38 toolbar concepts and fall back to the Tabler line
     icons for the rest - see iconColored() in src/main.cpp).

The baked colours are tuned for LIGHT chrome (green-4 matches the relative luminance of
the stock #8fbffa, so the set keeps its designed weight); dark mode lightens both at
runtime in iconColored() (src/main.cpp) - green-4 -> green-3 and teal-8 -> teal-4 -
mirroring the Solar/IconPark retint approach.

Usage:  python scripts/generate_streamline_icons.py
"""

import os
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET

PINNED_SHA = "52d750c9ce051e51cb181b7a78932120c48541d0"
RAW_URL = "https://raw.githubusercontent.com/webalys-hq/streamline-vectors/%s/%s"

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(REPO, "build", "streamline-vectors-cache")
OUT = os.path.join(REPO, "resources", "icons-streamline")
TABLER = os.path.join(REPO, "resources", "icons")  # default line set = the full 50-concept manifest

# Palette re-bake: Streamline flat stock blues -> Open Color (lime/green/teal/gray scales only).
REBAKE = {
    "#8fbffa": "#69db7c",  # light fill        -> Open Color green-4
    "#2859c5": "#099268",  # dark detail paint -> Open Color teal-8
}
APPROVED = set(REBAKE.values()) | {"#fff", "none"}

# Toolbar concept -> direct source glyph (repo path inside streamline-vectors).
DIRECT = {
    "all-chars":           "core/flat/interface-essential/paragraph.svg",
    "bookmark":            "core/flat/interface-essential/bookmark.svg",
    "close":               "core/flat/interface-essential/delete-1.svg",
    "close-tab":           "core/flat/programming/browser-delete.svg",
    "computer":            "core/flat/computer-devices/computer-pc-desktop.svg",
    "copy":                "core/flat/interface-essential/multiple-file-2.svg",
    "cut":                 "core/flat/interface-essential/cut.svg",
    "doc-list":            "core/flat/interface-essential/bullet-list.svg",
    "doc-map":             "core/flat/map-travel/map-fold.svg",
    "drive":               "core/flat/computer-devices/hard-drive-1.svg",
    "file":                "core/flat/interface-essential/new-file.svg",
    "filetype-archive":    "core/flat/interface-essential/archive-box.svg",
    "filetype-code":       "core/flat/programming/file-code-1.svg",
    "filetype-image":      "core/flat/images-photography/landscape-2.svg",
    "filetype-video":      "core/flat/entertainment/camera-video.svg",
    "find":                "core/flat/interface-essential/magnifying-glass.svg",
    "floppy":              "core/flat/computer-devices/floppy-disk.svg",
    "folder":              "core/flat/interface-essential/new-folder.svg",
    "folder-as-workspace": "core/flat/computer-devices/local-storage-folder.svg",
    "func-leaf":           "core/flat/nature-ecology/leaf.svg",
    "func-node":           "core/flat/interface-essential/hierarchy-2.svg",
    "function-list":       "core/flat/programming/curly-brackets.svg",
    "monitoring":          "core/flat/interface-essential/visible.svg",
    "new":                 "core/flat/interface-essential/file-add-alternate.svg",
    "paste":               "core/flat/interface-essential/copy-paste.svg",
    "playback":            "core/flat/entertainment/button-play.svg",
    "playback-multiple":   "core/flat/entertainment/play-list-4.svg",
    "print":               "core/flat/computer-devices/printer.svg",
    "removable":           "core/flat/computer-devices/usb-drive.svg",
    "replace":             "core/flat/interface-essential/arrow-reload-horizontal-1.svg",
    "save":                "core/flat/computer-devices/floppy-disk.svg",
    "udl-dlg":             "core/flat/interface-essential/color-swatches.svg",
    "undo":                "core/flat/computer-devices/return-2.svg",
    "word-wrap":           "core/flat/interface-essential/text-flow-rows.svg",
}

# Extra glyphs fetched only as composite ingredients.
INGREDIENTS = {
    "delete-1":        "core/flat/interface-essential/delete-1.svg",
    "browser-delete":  "core/flat/programming/browser-delete.svg",
    "new-folder":      "core/flat/interface-essential/new-folder.svg",
    "return-2":        "core/flat/computer-devices/return-2.svg",
    "magnifying-glass": "core/flat/interface-essential/magnifying-glass.svg",
    "floppy-disk":     "core/flat/computer-devices/floppy-disk.svg",
    "button-record-3": "core/flat/entertainment/button-record-3.svg",
    "button-stop":     "core/flat/entertainment/button-stop.svg",
    "file-code-1":     "core/flat/programming/file-code-1.svg",
    "music-note-1":    "core/flat/entertainment/music-note-1.svg",
}

LB = "#8fbffa"  # stock light fill (re-baked at the end)
DB = "#2859c5"  # stock dark detail


def fetch(repo_path: str) -> str:
    os.makedirs(CACHE, exist_ok=True)
    dst = os.path.join(CACHE, repo_path.replace("/", "__"))
    if not os.path.exists(dst):
        url = RAW_URL % (PINNED_SHA, repo_path)
        print("  fetching", repo_path)
        urllib.request.urlretrieve(url, dst)
    with open(dst, encoding="utf-8") as f:
        return f.read()


def path_d(svg: str, path_id: str) -> str:
    """Extract the d= of the <path id=...> element."""
    m = re.search(r'<path[^>]*\bid="%s"[^>]*\bd="([^"]+)"' % re.escape(path_id), svg)
    if not m:
        m = re.search(r'<path[^>]*\bd="([^"]+)"[^>]*\bid="%s"' % re.escape(path_id), svg)
    if not m:
        raise SystemExit("path id %r not found" % path_id)
    return m.group(1)


def skeleton(title: str, desc: str, body: str) -> str:
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 14 14" id="%s">\n'
        "  <desc>\n"
        "    %s\n"
        "  </desc>\n"
        "%s"
        "</svg>\n" % (title, desc, body)
    )


DESC_SUFFIX = "derived from Streamline Core flat icons: https://streamlinehq.com"


def composites() -> dict:
    """Build the concepts the free flat set has no direct glyph for."""
    out = {}
    x_d = path_d(fetch(INGREDIENTS["delete-1"]), "Union")
    bd = fetch(INGREDIENTS["browser-delete"])
    win_d = path_d(bd, "Union")        # rounded window body
    bar_d = path_d(bd, "Union_2")      # window title bar
    folder_d = path_d(fetch(INGREDIENTS["new-folder"]), "Union")
    ret_d = path_d(fetch(INGREDIENTS["return-2"]), "Union")
    mg = fetch(INGREDIENTS["magnifying-glass"])
    lens_d = path_d(mg, "Ellipse 651")
    ring_d = path_d(mg, "Union")
    fd = fetch(INGREDIENTS["floppy-disk"])
    fbody_d = path_d(fd, "Union")
    flabel_d = path_d(fd, "Vector")
    fslot_d = path_d(fd, "Vector_2")
    rec_d = path_d(fetch(INGREDIENTS["button-record-3"]), "Vector")
    stop_d = path_d(fetch(INGREDIENTS["button-stop"]), "Union")

    # record / stop-record: the flat set's button glyphs are single light-fill shapes,
    # which read washed-out as the record/stop pair; give each a dark inner mark
    # (dot / square) in the pack's own light-fill + dark-detail discipline.
    out["record"] = skeleton(
        "Record--Streamline-wxNote",
        "record macro (record button + dot), %s" % DESC_SUFFIX,
        '  <path fill="%s" d="%s"/>\n' % (LB, rec_d)
        + '  <circle cx="7" cy="7" r="3.2" fill="%s"/>\n' % (DB,),
    )
    out["stop-record"] = skeleton(
        "Stop-Record--Streamline-wxNote",
        "stop recording (stop button + square), %s" % DESC_SUFFIX,
        '  <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n' % (LB, stop_d)
        + '  <rect x="4.3" y="4.3" width="5.4" height="5.4" rx="1" fill="%s"/>\n' % (DB,),
    )

    # zoom-in / zoom-out: the magnifier with a +/- dropped into its lens.
    zoom_bars = {
        "zoom-in": '    <rect x="3.9" y="5.3" width="4.2" height="1.4" rx="0.7" fill="%s"/>\n'
                   '    <rect x="5.3" y="3.9" width="1.4" height="4.2" rx="0.7" fill="%s"/>\n' % (DB, DB),
        "zoom-out": '    <rect x="3.9" y="5.3" width="4.2" height="1.4" rx="0.7" fill="%s"/>\n' % (DB,),
    }
    for name, bars in zoom_bars.items():
        out[name] = skeleton(
            "%s--Streamline-wxNote" % name.title(),
            "%s, %s" % (name, DESC_SUFFIX),
            '  <path fill="%s" d="%s"/>\n' % (LB, lens_d)
            + bars
            + '  <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n' % (DB, ring_d),
        )

    # redo: the undo glyph (return-2) mirrored horizontally. NanoSVG parses the transform.
    out["redo"] = skeleton(
        "Redo--Streamline-wxNote",
        "redo (mirrored return-2), %s" % DESC_SUFFIX,
        '  <g transform="translate(14 0) scale(-1 1)">\n'
        '    <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n'
        "  </g>\n" % (LB, ret_d),
    )

    # save-all: offset-stacked floppies, using the pack's own stacking idiom
    # (multiple-file-2: solid dark back silhouette behind a full light front copy).
    floppy_full = (
        '    <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n'
        '    <path fill="%s" d="%s"/>\n'
        '    <path fill="%s" d="%s"/>\n' % (LB, fbody_d, DB, flabel_d, DB, fslot_d)
    )
    out["save-all"] = skeleton(
        "Save-All--Streamline-wxNote",
        "save all (stacked floppy disks), %s" % DESC_SUFFIX,
        '  <g transform="translate(3.5 0) scale(0.75)">\n'
        '    <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n'
        "  </g>\n"
        '  <g transform="translate(0 3.5) scale(0.75)">\n'
        "%s"
        "  </g>\n" % (DB, fbody_d, floppy_full),
    )

    # save-macro: the floppy with a record-dot badge (the badge ring is the pack-external
    # white, kept #fff in both themes).
    out["save-macro"] = skeleton(
        "Save-Macro--Streamline-wxNote",
        "save recorded macro (floppy disk + record dot), %s" % DESC_SUFFIX,
        floppy_full.replace("    <", "  <")
        + '  <circle cx="10.6" cy="10.6" r="3.1" fill="#fff"/>\n'
        + '  <circle cx="10.6" cy="10.6" r="2.2" fill="%s"/>\n' % (DB,),
    )

    # close-all: the close X inside a rounded-square frame (the Tabler set's own
    # close-all shape), frame from browser-delete's window body.
    out["close-all"] = skeleton(
        "Close-All--Streamline-wxNote",
        "close all (X in a window frame), %s" % DESC_SUFFIX,
        '  <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n' % (LB, win_d)
        + '  <g transform="translate(3.15 3.15) scale(0.55)">\n'
        '    <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n'
        "  </g>\n" % (DB, x_d),
    )

    # open: the folder with an opened front flap.
    out["open"] = skeleton(
        "Open--Streamline-wxNote",
        "open file (opened folder), %s" % DESC_SUFFIX,
        '  <path fill="%s" d="%s"/>\n' % (LB, folder_d)
        + '  <path fill="%s" d="M1.62 13.5c-0.62 0 -0.72 -0.4 -0.57 -0.84l1.75 -5.16c0.15 -0.44 0.5 -0.75 1 -0.75h9.1c0.6 0 0.85 0.4 0.7 0.85l-1.75 5.15c-0.17 0.45 -0.55 0.75 -1.05 0.75H1.62Z"/>\n' % (DB,),
    )

    # terminal: browser-delete's window + title bar with a shell prompt instead of the X.
    out["terminal"] = skeleton(
        "Terminal--Streamline-wxNote",
        "terminal (window + shell prompt), %s" % DESC_SUFFIX,
        '  <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n' % (LB, win_d)
        + '  <path fill="%s" d="%s"/>\n' % (DB, bar_d)
        + '  <path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" d="M3.4 6.6 5.6 8.5 3.4 10.4"/>\n' % (DB,)
        + '  <path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" d="M7.4 10.5h3.2"/>\n' % (DB,),
    )

    # indent-guide: a vertical guide bar with indented text lines, drawn from primitives
    # in the pack's rounded-bar style (mirrors the Tabler concept shape).
    out["indent-guide"] = skeleton(
        "Indent-Guide--Streamline-wxNote",
        "indent guide (guide bar + indented lines), %s" % DESC_SUFFIX,
        '  <rect x="1.1" y="2.5" width="1.5" height="9" rx="0.75" fill="%s"/>\n' % (DB,)
        + '  <rect x="4.4" y="2.5" width="9.2" height="1.5" rx="0.75" fill="%s"/>\n' % (LB,)
        + '  <rect x="4.4" y="5.0" width="6.5" height="1.5" rx="0.75" fill="%s"/>\n' % (LB,)
        + '  <rect x="4.4" y="7.5" width="8.0" height="1.5" rx="0.75" fill="%s"/>\n' % (LB,)
        + '  <rect x="4.4" y="10.0" width="5.5" height="1.5" rx="0.75" fill="%s"/>\n' % (LB,),
    )

    # pin: a pushpin drawn from primitives in the pack's flat two-paint style (the free
    # core set has no pushpin - only location pins), tilted 45 degrees like the classic
    # pushpin glyph so the diagonal needle reads at 16 px.
    out["pin"] = skeleton(
        "Pin--Streamline-wxNote",
        "pinned tab (pushpin), %s" % DESC_SUFFIX,
        '  <g transform="rotate(45 7 7)">\n'
        '    <path fill="%s" d="M5.3 1.05c0 -0.45 0.35 -0.8 0.8 -0.8h1.8c0.45 0 0.8 0.35 0.8 0.8V4.4H5.3V1.05Z"/>\n' % (LB,)
        + '    <rect x="3.1" y="4.4" width="7.8" height="1.9" rx="0.95" fill="%s"/>\n' % (DB,)
        + '    <path fill="%s" d="M6.2 6.3h1.6l-0.55 6.7c-0.03 0.4 -0.47 0.4 -0.5 0l-0.55 -6.7Z"/>\n' % (DB,)
        + "  </g>\n",
    )

    # ---- workspace-tree device/filetype concepts (the Tabler manifest's last 12; the
    # eight with a direct free-set glyph live in DIRECT, these four are composites) ----
    filebody_d = path_d(fetch(INGREDIENTS["file-code-1"]), "Union")
    note_d = path_d(fetch(INGREDIENTS["music-note-1"]), "Union")

    # cdrom: the free core set has no compact-disc glyph; rebuild the Tabler disc
    # (disc + centre hub/hole + two sheen arcs) from button-record-3's full-bleed
    # circle plus primitives, in the pack's two-paint discipline.
    out["cdrom"] = skeleton(
        "Cdrom--Streamline-wxNote",
        "CD-ROM drive (compact disc), %s" % DESC_SUFFIX,
        '  <path fill="%s" d="%s"/>\n' % (LB, rec_d)
        + '  <path fill="none" stroke="%s" stroke-width="1.25" stroke-linecap="round" d="M3.1 7a3.9 3.9 0 0 1 3.9 -3.9"/>\n' % (DB,)
        + '  <path fill="none" stroke="%s" stroke-width="1.25" stroke-linecap="round" d="M7 10.9a3.9 3.9 0 0 0 3.9 -3.9"/>\n' % (DB,)
        + '  <circle cx="7" cy="7" r="2.3" fill="%s"/>\n' % (DB,)
        + '  <circle cx="7" cy="7" r="1" fill="%s"/>\n' % (LB,),
    )

    # executable: the Tabler glyph is an app window with a shell prompt and NO title
    # bar (the title bar is what distinguishes terminal from executable in that set,
    # mirrored here): browser-delete's window body + the terminal composite's prompt.
    out["executable"] = skeleton(
        "Executable--Streamline-wxNote",
        "executable file (app window + prompt), %s" % DESC_SUFFIX,
        '  <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n' % (LB, win_d)
        + '  <path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" d="M3.8 4.6 6.4 7 3.8 9.4"/>\n' % (DB,)
        + '  <path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" d="M7.6 9.4h3.2"/>\n' % (DB,),
    )

    # filetype-document: file-code-1's file body (the same silhouette filetype-code
    # ships directly, so the filetype-* file icons match) with dark text lines in
    # place of the code chevrons (the Tabler glyph's short + two long lines).
    out["filetype-document"] = skeleton(
        "Filetype-Document--Streamline-wxNote",
        "document file (file + text lines), %s" % DESC_SUFFIX,
        '  <path fill="%s" d="%s"/>\n' % (LB, filebody_d)
        + '  <rect x="3.4" y="5.7" width="2.6" height="1.3" rx="0.65" fill="%s"/>\n' % (DB,)
        + '  <rect x="3.4" y="8" width="7.2" height="1.3" rx="0.65" fill="%s"/>\n' % (DB,)
        + '  <rect x="3.4" y="10.3" width="7.2" height="1.3" rx="0.65" fill="%s"/>\n' % (DB,),
    )

    # filetype-audio: the same file body with music-note-1's note scaled inside as
    # the dark detail (the Tabler glyph: file + note).
    out["filetype-audio"] = skeleton(
        "Filetype-Audio--Streamline-wxNote",
        "audio file (file + music note), %s" % DESC_SUFFIX,
        '  <path fill="%s" d="%s"/>\n' % (LB, filebody_d)
        + '  <g transform="translate(2.9 5.2) scale(0.5)">\n'
        '    <path fill="%s" fill-rule="evenodd" clip-rule="evenodd" d="%s"/>\n'
        "  </g>\n" % (DB, note_d),
    )
    return out


def rebake(svg: str) -> str:
    for old, new in REBAKE.items():
        svg = re.sub(re.escape(old), new, svg, flags=re.IGNORECASE)
    return svg


FORBIDDEN_ELEMENTS = {"mask", "filter", "clipPath", "style", "text"}


def validate(name: str, svg: str) -> None:
    root = ET.fromstring(svg)  # well-formed XML
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        if tag in FORBIDDEN_ELEMENTS:
            raise SystemExit("%s: forbidden element <%s>" % (name, tag))
        for attr in ("fill", "stroke"):
            v = el.get(attr)
            if v is not None and v.lower() not in APPROVED:
                raise SystemExit("%s: unapproved paint %r" % (name, v))
    for old in REBAKE:
        if old.lower() in svg.lower():
            raise SystemExit("%s: stock paint %s survived the re-bake" % (name, old))


def main() -> None:
    files = {}
    for concept, repo_path in sorted(DIRECT.items()):
        files[concept] = fetch(repo_path)
    files.update(composites())

    manifest = sorted(f[:-4] for f in os.listdir(TABLER) if f.endswith(".svg"))
    if sorted(files) != manifest:
        raise SystemExit(
            "manifest mismatch:\n  missing: %s\n  extra: %s"
            % (sorted(set(manifest) - set(files)), sorted(set(files) - set(manifest)))
        )

    os.makedirs(OUT, exist_ok=True)
    for concept, svg in sorted(files.items()):
        svg = rebake(svg)
        validate(concept, svg)
        with open(os.path.join(OUT, concept + ".svg"), "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)
    print("wrote %d icons to %s" % (len(files), OUT))


if __name__ == "__main__":
    main()
