#!/usr/bin/env python3
"""Generate resources/wxnote-doc.ico - the Explorer/Finder icon for files opened with wxNote.

Drawn rather than traced from a third-party icon set: the artwork is the project's own app mark
(the green plate + white "N" of resources/wxnote.svg) re-cut as a document, so the file icon carries
no third-party licence at all and stays visibly part of the same family as the app icon.

Everything is rendered at 8x and downsampled with LANCZOS, which is what gives the diagonal fold and
the monogram clean edges at 16px - the size Explorer's list view actually uses, and the one that
decides whether an icon reads or turns to mush.

    python tools/make_doc_icon.py [--variant B] [--out resources/wxnote-doc.ico]

Variants (all three keep their vector source in resources/wxnote-doc-{a,b,c}.svg):
    A  white page, green monogram          - lightest, but the page border fades on white backgrounds
    B  white page, green fold, green mono  - A with the fold picked out in brand green  <- shipped
    C  green page, white monogram          - brand-forward, heaviest

B is the shipped default. C holds together best at 16px in isolation, but B is the one that reads as
a document rather than a coloured tile at the sizes that carry the icon's meaning, and the green fold
still gives it a brand cue in a list of white file icons.
"""
import argparse, io, os, sys
from PIL import Image, ImageDraw

GREEN, GREEN_DARK, WHITE, EDGE, PAPER = "#37b24d", "#2f9e44", "#ffffff", "#c8ccd0", "#e6e9ec"
SS = 8                                   # supersampling factor
SIZES = [16, 24, 32, 48, 64, 128, 256]   # what a Windows .ico is expected to carry

# All geometry is expressed in the same 256-unit grid as resources/wxnote.svg so the two icons can be
# compared side by side without mentally rescaling one of them.
# Sized to fill ~66% of the tile's width, which is the proportion Windows' own document icons use.
# An earlier 56% page looked fine at 128px and lost the monogram at 16px, where every unit counts.
PAGE_BOX = (44, 14, 212, 242)   # left, top, right, bottom
RADIUS   = 15
FOLD_X, FOLD_Y = 156, 74        # the diagonal runs (FOLD_X, top) -> (right, FOLD_Y)
# The "N": the app mark's own polyline, shrunk and nudged down so it sits clear of the fold.
MONO = [(88, 174), (88, 82), (168, 174), (168, 82)]
MONO_W, MONO_SCALE, MONO_CENTER = 28, 0.82, (128, 148)


def _xf(p):
    """Apply the monogram's scale-about-centre transform (matches the SVG's transform= attribute)."""
    cx, cy = MONO_CENTER
    return ((p[0] - 128) * MONO_SCALE + cx, (p[1] - 128) * MONO_SCALE + cy)


def _page_mask(size):
    """Page silhouette: a rounded rectangle with the top-right corner cut off by the fold diagonal."""
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    l, t, r, b = [v * size / 256.0 for v in PAGE_BOX]
    d.rounded_rectangle([l, t, r, b], radius=RADIUS * size / 256.0, fill=255)
    fx, fy = FOLD_X * size / 256.0, FOLD_Y * size / 256.0
    d.polygon([(fx, t), (r + 2, t), (r + 2, fy)], fill=0)   # +2: bite past the edge, no seam left behind
    return m


def render(variant, size):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    k = S / 256.0
    l, t, r, b = [v * k for v in PAGE_BOX]
    fx, fy = FOLD_X * k, FOLD_Y * k

    page_fill = GREEN if variant == "C" else WHITE
    fold_fill = {"A": PAPER, "B": GREEN, "C": GREEN_DARK}[variant]
    mono_fill = WHITE if variant == "C" else GREEN

    d.rounded_rectangle([l, t, r, b], radius=RADIUS * k, fill=page_fill,
                        # The white variants need an outline or they vanish on Explorer's white list view.
                        outline=None if variant == "C" else EDGE, width=max(1, int(3 * k)))
    d.polygon([(fx, t), (r + 2 * k, t), (r + 2 * k, fy)], fill=(0, 0, 0, 0))   # cut the corner away
    d.polygon([(fx, t), (r, fy), (fx, fy)], fill=fold_fill)                    # ...and fold it back

    pts = [_xf(p) for p in MONO]
    w = MONO_W * MONO_SCALE * k
    d.line([(x * k, y * k) for x, y in pts], fill=mono_fill, width=int(round(w)), joint="curve")
    for x, y in (pts[0], pts[-1]):        # round caps: PIL's line has none, so cap the free ends by hand
        d.ellipse([x * k - w / 2, y * k - w / 2, x * k + w / 2, y * k + w / 2], fill=mono_fill)

    img = img.resize((size, size), Image.LANCZOS)
    # Re-cut the silhouette after downsampling: the corner cut is a hard alpha edge, and resizing a
    # transparent notch bleeds a faint halo into it that is clearly visible against a dark background.
    img.putalpha(Image.composite(img.getchannel("A"), Image.new("L", (size, size), 0), _page_mask(size)))
    return img


SVG_TEMPLATE = """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"
     role="img" aria-label="wxNote document icon">
  <title>wxNote - document icon (variant %(v)s)</title>

  <!-- page: the app mark re-cut as a document, folded corner top-right -->
  <path d="%(page)s" fill="%(page_fill)s"%(stroke)s/>
  <path d="%(fold)s" fill="%(fold_fill)s"/>

  <!-- "N" monogram: the polyline from resources/wxnote.svg, scaled and nudged clear of the fold -->
  <path d="%(mono)s" fill="none" stroke="%(mono_fill)s" stroke-width="%(sw)d"
        stroke-linecap="round" stroke-linejoin="round"
        transform="translate(%(cx)d %(cy)d) scale(%(scale)s) translate(-128 -128)"/>
</svg>
"""

def svg(variant):
    """The same artwork as vector. Shares every constant with render() above, so the .ico and the .svg
    cannot drift into being two different icons - regenerate both from this one script."""
    l, t, r, b = PAGE_BOX
    page = ("M%d %d H%d L%d %d V%d A%d %d 0 0 1 %d %d H%d A%d %d 0 0 1 %d %d V%d A%d %d 0 0 1 %d %d Z"
            % (l + RADIUS, t, FOLD_X, r, FOLD_Y, b - RADIUS, RADIUS, RADIUS, r - RADIUS, b,
               l + RADIUS, RADIUS, RADIUS, l, b - RADIUS, t + RADIUS, RADIUS, RADIUS, l + RADIUS, t))
    cx, cy = MONO_CENTER
    return SVG_TEMPLATE % {
        "v": variant,
        "page": page,
        "fold": "M%d %d L%d %d H%d Z" % (FOLD_X, t, r, FOLD_Y, FOLD_X),
        "page_fill": GREEN if variant == "C" else WHITE,
        "fold_fill": {"A": PAPER, "B": GREEN, "C": GREEN_DARK}[variant],
        "mono_fill": WHITE if variant == "C" else GREEN,
        # The white-page variants need the outline or they vanish on Explorer's white list view.
        "stroke": "" if variant == "C" else ' stroke="%s" stroke-width="3"' % EDGE,
        "mono": " ".join("%s%d %d" % ("M" if i == 0 else "L", x, y) for i, (x, y) in enumerate(MONO)),
        "sw": MONO_W, "cx": cx, "cy": cy, "scale": MONO_SCALE,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="B", choices=["A", "B", "C"])
    ap.add_argument("--out", default=None)
    ap.add_argument("--png-dir", default=None, help="also write per-size PNGs here (for previewing)")
    ap.add_argument("--svg-dir", default=None, help="also write the vector source here")
    a = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "resources", "wxnote-doc.ico")
    frames = [render(a.variant, s) for s in SIZES]
    frames[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES])
    print("wrote %s (variant %s, %s)" % (out, a.variant, ", ".join("%dx%d" % (s, s) for s in SIZES)))
    # The canonical vector twin, written beside the canonical .ico and always the SAME variant as it.
    # Windows reads the .ico and macOS rasterizes the .svg, so if these two were allowed to name
    # different variants the same build would ship two different document icons.
    twin = os.path.splitext(out)[0] + ".svg"
    io.open(twin, "w", encoding="utf-8", newline=chr(10)).write(svg(a.variant))
    print("wrote %s (same variant, for the macOS .icns and any vector use)" % twin)
    if a.png_dir:
        os.makedirs(a.png_dir, exist_ok=True)
        for s, f in zip(SIZES, frames):
            f.save(os.path.join(a.png_dir, "doc_%s_%d.png" % (a.variant, s)))
    if a.svg_dir:
        os.makedirs(a.svg_dir, exist_ok=True)
        p = os.path.join(a.svg_dir, "wxnote-doc-%s.svg" % a.variant.lower())
        io.open(p, "w", encoding="utf-8", newline=chr(10)).write(svg(a.variant))
        print("wrote %s" % p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
