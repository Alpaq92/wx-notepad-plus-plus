# "Solar" colored toolbar icon set — attribution

This is one of three "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`
and the sibling "IconPark" (`resources/icons-iconpark/`) and "Streamline"
(`resources/icons-streamline/`) colored sets.

**Solar Icons** (Bold Duotone style) — © 480 Design, distributed via
[Iconify](https://icon-sets.iconify.design/solar/) / <https://github.com/480-Design>.
Licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

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
light mode (green-9 / green-6) - this file's baked colours are the dark-mode ones. No other
modifications were made beyond selecting icons and these colour substitutions.

Source data: `solar.json` from <https://github.com/iconify/icon-sets> (Iconify's
normalized re-export of the original icon set).
