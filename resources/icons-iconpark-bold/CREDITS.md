# "IconPark Bold" colored toolbar icon set — attribution

This is one of four "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`,
the sibling "Solar" colored set in `resources/icons-solar/`, and the thinner-stroke sibling
`resources/icons-iconpark/` (same colours, thinner outline — see its own CREDITS.md). This
directory is the bold-stroke variant, keeping IconPark's original upstream outline weight
(`stroke-width="4"`) rather than the thinned `2.5` used in the sibling set.

**IconPark** — © ByteDance, <https://github.com/bytedance/IconPark>.
Licensed under [Apache-2.0](https://github.com/bytedance/IconPark/blob/master/LICENSE)
(confirmed via GitHub's license API; the repository is archived/read-only but the license
remains in effect for existing content).

IconPark's default "Original" style bakes two fixed accent colours into every icon
(`#2F88FF` as the primary fill, `#43CCF8` as a secondary highlight) alongside black/white
structural outline strokes. Both accents were replaced here with **Open Color teal-7
(`#0ca678`)** and **lime-5 (`#94d82d`)** respectively, chosen as a pair that reads clearly
on both light and dark toolbar backgrounds. 9 of the 37 icons chosen for this toolbar are
pure line-art in IconPark's own set (no fill accent at all, structural stroke only) - for
those, the black stroke itself was tinted teal instead, so every icon carries the accent
colour consistently; the remaining black/white outline strokes on the two-tone icons were
left untouched (dark mode additionally lightens plain black outlines at runtime - see
`iconColored()` in `src/main.cpp`). No other modifications were made beyond selecting
icons and these changes.

Source data: `icon-park.json` from <https://github.com/iconify/icon-sets> (Iconify's
normalized re-export of the original icon set).
