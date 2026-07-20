# "Solar" colored toolbar icon set — attribution

This is one of three "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`
and the sibling "IconPark" (`resources/icons-iconpark/`) and "Streamline"
(`resources/icons-streamline/`) colored sets.

Like the default line set (and the "Streamline" colored set), this set now covers the
full **50-concept manifest**: the 38 toolbar concepts plus the 12 workspace-tree
device/filetype concepts (`computer`, `drive`, `cdrom`, `removable`, `executable`,
`floppy` and the six `filetype-*` icons). Earlier releases shipped only the 38 toolbar
icons and let the workspace tree fall back to the line icons for the other 12.

**Solar Icons** (Bold Duotone style) — © 480 Design, distributed via
[Iconify](https://icon-sets.iconify.design/solar/) and
<https://github.com/480-Design/Solar-Icon-Set>.
Licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The 480-Design
GitHub repository carries **no `LICENSE` file** — the CC BY 4.0 grant is published on the
first-party **Figma Community** file, which is therefore the authoritative license source:
<https://www.figma.com/community/file/1166831539721848736/solar-icons-set>.

Solar's Bold Duotone icons achieve their two-tone look via a single `currentColor` fill
with `opacity=".5"` (or `.4`/`.7` on a couple of icons) on the lighter layer. That's alpha
blending against whatever sits behind it, so it reads fine on light chrome but nearly
vanishes on dark chrome. Each icon here instead has the full-opacity layer replaced with
one fixed colour, **Open Color green-8 (`#2f9e44`)**, and every reduced-opacity layer
replaced with a second fixed colour, **Open Color green-3 (`#8ce99a`)**, with the opacity
attribute removed — a solid duotone pair baked in as the file's default, tuned for DARK
chrome (green-3 is deliberately pale so it pops against a dark background instead of
alpha-blending into it). On light chrome that same pale secondary reads as washed-out, so
`iconColored()` in `src/main.cpp` retints both tones darker at runtime specifically for
light mode (green-9 / green-6) - this file's baked colours are the dark-mode ones. This
recolor-and-flatten is the only change made to the original glyphs; NanoSVG (the runtime
renderer) drops sub-1.0 opacity inconsistently, so the flatten is what makes the duotone
render reliably.

Source data: `solar.json` from <https://github.com/iconify/icon-sets> (Iconify's
normalized re-export of the original icon set), Bold Duotone style throughout.

## Workspace-tree concepts (the 12 added to reach 50)

Eleven of the twelve map to a direct Solar Bold-Duotone glyph, recolored/flattened exactly
as above. Concept → source glyph:

| Concept                | Solar Bold-Duotone glyph      |
| ---------------------- | ----------------------------- |
| `computer`             | `monitor-bold-duotone`        |
| `drive`                | `server-square-bold-duotone`  |
| `cdrom`                | `vinyl-record-bold-duotone`   |
| `removable`            | `usb-square-bold-duotone`     |
| `floppy`               | `diskette-bold-duotone`       |
| `filetype-code`        | `code-file-bold-duotone`      |
| `filetype-document`    | `file-text-bold-duotone`      |
| `filetype-image`       | `gallery-bold-duotone`        |
| `filetype-video`       | `videocamera-bold-duotone`    |
| `filetype-archive`     | `zip-file-bold-duotone`       |

`floppy` uses Solar's `diskette-bold-duotone` — the same glyph the toolbar's `save` icon is
built from — so `floppy.svg` is byte-identical to `save.svg`. That is intentional: a floppy
disk *is* the save glyph, and the two never appear next to each other (one is a toolbar
button, the other a file-tree node).

### Composited concepts

The free Solar set has no direct glyph for a couple of concepts, so these two were assembled
from the pack's own primitives, keeping the set's baked green-8/green-3 fill discipline:

- **`executable`** — Solar has no distinct "run/executable" glyph, and its `terminal` glyph
  is a soft rounded window that this set already ships. So `executable` is composed from an
  app-window primitive (a sharper-cornered `rx=2.5` rounded rectangle in green-3) plus the
  green-8 `>` shell prompt and underscore bar drawn with the same stroke idiom as
  `terminal.svg`. The sharper corners keep it visually distinct from the Solar `terminal`
  glyph, mirroring how the default Tabler set separates `executable` from `terminal`.
- **`filetype-audio`** — Solar has no file-with-music-note glyph. It is composed from the
  shared Solar file body (`file-bold-duotone`'s green-3 body + green-8 folded corner — the
  exact silhouette used by `filetype-code`/`filetype-document` and the plain `file` icon)
  with a green-8 eighth-note (head + stem + flag) drawn on top as fills, so the four
  file-shaped `filetype-*` icons share one silhouette.

No other modifications were made beyond selecting icons, the colour substitutions, and the
two composites above.
