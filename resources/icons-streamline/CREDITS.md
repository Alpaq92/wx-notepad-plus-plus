# "Streamline" colored toolbar icon set — attribution

This is one of three "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`
and the sibling "Solar" (`resources/icons-solar/`) and "IconPark"
(`resources/icons-iconpark/`) colored sets.

Unlike the two sibling colored sets (38 toolbar concepts each, workspace tree falls
back to the line icons), this set covers the default line set's **full 50-concept
manifest** — the 38 toolbar concepts plus the 12 workspace-tree device/filetype
concepts (`computer`, `drive`, `cdrom`, `removable`, `executable`, `floppy` and the
six `filetype-*` icons).

**Free icons from Streamline** (<https://streamlinehq.com>) — the "Core" free set,
"flat" style, © Streamline / Webalys.
Licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (the license
of this set is unchanged here — these files remain CC BY 4.0).

Source data: the first-party vector repository
<https://github.com/webalys-hq/streamline-vectors>, `core/flat/` (only the free Core
set), pinned at commit `52d750c9ce051e51cb181b7a78932120c48541d0` — every source SVG
was fetched from that exact commit, so the regeneration script reproduces these files
byte-for-byte.

## Modifications

(As CC BY 4.0 §3(a)(1)(B) requires, the changes made to the original icons are:)

- **Palette re-bake.** Streamline's flat style bakes a fixed two-blue palette into every
  icon (`#8fbffa` light fill, `#2859c5` dark detail). Both paints were replaced with
  [Open Color](https://github.com/yeun/open-color) values: the light fill with
  **green-4 (`#69db7c`)** (chosen to match the stock blue's relative luminance, so the
  set keeps its designed visual weight) and the dark detail with **teal-8 (`#099268`)**.
  These baked colours are tuned for LIGHT chrome; in dark mode `iconColored()` in
  `src/main.cpp` retints both at runtime (green-4 → green-3 `#8ce99a`,
  teal-8 → teal-6 `#0ca678`) so the glyphs pop against dark chrome instead of vanishing.
  (An earlier retint used teal-4 `#38d9a9` for the detail paint, but that sat too close
  in luminance to green-3, reading as visually flat at 16px; teal-6 restores a gap
  closer to the light-mode bake's own fill/detail separation.)
- **Composited concepts.** The free Core set has no direct glyph for a few toolbar
  concepts, so these files were assembled from the downloaded glyphs' own paths plus
  simple primitives, keeping the pack's two-paint flat discipline: `zoom-in`/`zoom-out`
  (+/− bars added into `magnifying-glass`'s lens), `redo` (`return-2` mirrored via a
  transform), `save-all` (`floppy-disk` offset-stacked using the pack's own
  dark-silhouette-behind idiom from `multiple-file-2`), `save-macro` (`floppy-disk`
  plus a record-dot badge — the badge ring is the only use of `#fff`, kept white in
  both themes), `close-all` (`delete-1`'s X inside `browser-delete`'s window frame),
  `open` (`new-folder` with an added open front flap), `terminal` (`browser-delete`'s
  window and title bar with a shell prompt in place of the X), `record`/`stop-record`
  (the single-paint `button-record-3`/`button-stop` given a dark inner dot/square so
  the pair reads at 16 px), `indent-guide` and `pin`, drawn from primitives in the
  pack's construction style (the free set has no indent-guide or pushpin glyph),
  `cdrom` (`button-record-3`'s full-bleed circle given a dark centre hub, a light
  hole and two sheen arcs — the free set has no compact-disc glyph), `executable`
  (`browser-delete`'s window body with the terminal composite's shell prompt but NO
  title bar, mirroring how the default Tabler set separates executable from
  terminal), `filetype-document` (`file-code-1`'s file body with dark text lines in
  place of the code chevrons, so the `filetype-*` file icons share one silhouette),
  and `filetype-audio` (the same file body with `music-note-1` scaled inside as the
  dark detail).
- No other modifications were made beyond selecting icons and the changes above.

The full concept-to-source-glyph mapping and the re-bake/compositing pipeline live in
`tools/generate_streamline_icons.py`, which regenerates this directory.
