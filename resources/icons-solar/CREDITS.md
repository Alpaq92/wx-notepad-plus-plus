# "Solar" colored toolbar icon set — attribution

This is one of four "Colored icons" options for the toolbar (Settings > Preferences >
General > Toolbar icon style), alongside the default line-icon set in `resources/icons/`
and the sibling IconPark colored sets in `resources/icons-iconpark/` and
`resources/icons-iconpark-bold/`.

**Solar Icons** (Bold Duotone style) — © 480 Design, distributed via
[Iconify](https://icon-sets.iconify.design/solar/) / <https://github.com/480-Design>.
Licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Solar's Bold Duotone icons achieve their two-tone look via a single `currentColor` fill
with `opacity=".5"` (or `.4`/`.7` on a couple of icons) on the lighter layer. That's alpha
blending against whatever sits behind it, so it reads fine on light chrome but nearly
vanishes on dark chrome. Each icon here instead has the full-opacity layer replaced with
one fixed colour, **Open Color green-8 (`#2f9e44`)**, and every reduced-opacity layer
replaced with a second fixed colour, **Open Color green-3 (`#8ce99a`)**, with the opacity
attribute removed — a solid duotone pair that reads clearly on both light and dark toolbar
backgrounds regardless of what's behind it (green-8 is this project's existing line-icon
accent, e.g. in `resources/icons/playback.svg`; green-3 was chosen to match what the old
50%-opacity blend looked like against a white background, so light-mode appearance is
unchanged). No other modifications were made beyond selecting icons and this colour
substitution.

Source data: `solar.json` from <https://github.com/iconify/icon-sets> (Iconify's
normalized re-export of the original icon set).
