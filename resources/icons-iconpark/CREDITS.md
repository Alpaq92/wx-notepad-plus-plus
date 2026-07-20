# "IconPark" colored toolbar icon set — attribution

This is one of three "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`
and the sibling "Solar" (`resources/icons-solar/`) and "Streamline"
(`resources/icons-streamline/`) colored sets. (A separate bolder-stroke
`icons-iconpark-bold/` variant existed briefly and was dropped - it read as visually
identical to this one in dark mode, so it wasn't worth the extra selectable option.)

**IconPark** — © ByteDance, <https://github.com/bytedance/IconPark>.
Licensed under [Apache-2.0](https://github.com/bytedance/IconPark/blob/master/LICENSE)
(confirmed via GitHub's license API — SPDX `Apache-2.0`, `LICENSE` at the repository
root; also reported as `Apache-2.0` by Iconify's collection metadata. The repository is
archived/read-only but the license remains in effect for existing content).

This set now covers the default line set's **full 50-concept manifest**: the 38 toolbar
concepts (below) plus the 12 workspace-tree device/filetype concepts (`computer`, `drive`,
`cdrom`, `removable`, `executable`, `floppy` and the six `filetype-*` icons) documented in
the "Workspace-tree icons" section at the end. Previously the toolbar concepts shipped
alone and the workspace tree fell back to the default line icons for those 12.

IconPark's default "Original" style bakes two fixed accent colours into every icon
(`#2F88FF` as the primary fill, `#43CCF8` as a secondary highlight) alongside black/white
structural outline strokes. Both accents were replaced here with **Open Color teal-7
(`#0ca678`)** and **lime-5 (`#94d82d`)** respectively, chosen as a pair that reads clearly
on both light and dark toolbar backgrounds. 9 of the 37 icons chosen for this toolbar are
pure line-art in IconPark's own set (no fill accent at all, structural stroke only) - for
those, the black stroke itself was tinted teal instead, so every icon carries the accent
colour consistently; the remaining black/white outline strokes on the two-tone icons are
left as upstream (black) on light chrome, and lightened to Open Color teal-3 at runtime in
dark mode only - see `iconColored()` in `src/main.cpp` (a same-hue-family tint reads better
there than the plain black, which nearly disappears on dark chrome). Outline weight was
also thinned from the upstream default (`stroke-width="4"`) to `2.5`, which reads more
clearly at 16px. No other modifications were made beyond selecting icons and these changes.

Source data: `icon-park.json` from <https://github.com/iconify/icon-sets> (Iconify's
normalized re-export of the original icon set).

## Workspace-tree icons (12)

The 12 device/filetype concepts used by the workspace tree were added later using the
**identical re-bake recipe** described above — each source glyph is IconPark's own
"Original" multi-colour style pulled from the same `icon-park.json`, with `#2F88FF` →
teal-7 (`#0ca678`), `#43CCF8` → lime-5 (`#94d82d`), and `stroke-width` thinned from `4`
to `2.5`. Every one of the 12 chosen glyphs already carries an IconPark fill accent (none
are pure line-art), so all 12 pick up the teal accent through the primary-fill swap and
none needed the black-stroke tinting used for the toolbar's line-art icons. No glyphs were
composited; each concept maps to a single upstream IconPark icon, chosen to match what the
default Tabler line set's same-named glyph depicts so the concept reads the same across
styles:

| Concept             | IconPark glyph      | Reads as                                    |
| ------------------- | ------------------- | ------------------------------------------- |
| `computer`          | `computer`          | desktop monitor on a stand                  |
| `drive`             | `hdd`               | hard-disk drive (platter + corner screws)   |
| `cdrom`             | `cd`                | compact disc with centre hub                |
| `removable`         | `u-disk`            | USB flash / thumb drive                     |
| `executable`        | `application-one`   | 3-D program package / binary                |
| `floppy`            | `disk`              | 3.5" floppy disk (shutter + label)          |
| `filetype-code`     | `file-code`         | file with `< >` code chevrons               |
| `filetype-document` | `file-text`         | file with a text mark                       |
| `filetype-image`    | `picture`           | photo frame with sun + mountain             |
| `filetype-audio`    | `file-music`        | file with a music note                      |
| `filetype-video`    | `video`             | film-strip screen with a play triangle      |
| `filetype-archive`  | `zip`               | zip / archive package                       |

`filetype-code`, `filetype-document` and `filetype-audio` share IconPark's one file-body
silhouette, so those three read as a consistent family (as they do in the default set).
The existing 38 toolbar files were left byte-for-byte unchanged when these were added.
