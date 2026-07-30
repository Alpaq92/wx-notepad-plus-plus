# Default toolbar icon set — attribution

This is the default line-icon toolbar set (Settings > Preferences > General > Toolbar
icon style), alongside the three "Colored icons" options in `resources/icons-solar/`,
`resources/icons-iconpark/` and `resources/icons-streamline/`.

**"Notepad++ Toolbar Icons (Tabler x Open Color)"** — a derivative work of two
MIT-licensed projects, released under the MIT License:

- **Tabler Icons** — © Paweł Kuna — <https://tabler.io/icons> —
  [MIT License](https://github.com/tabler/tabler-icons/blob/main/LICENSE)
- **Open Color** — © Heeyeun Jeong — <https://yeun.github.io/open-color/> —
  [MIT License](https://github.com/yeun/open-color/blob/master/LICENSE.md)

No modifications beyond selecting which icons to include; colours follow the theme
(light/dark) at runtime rather than being baked into the SVGs, unlike the two colored
sibling sets.

## Original glyphs (NOT from Tabler)

Three files in this directory were drawn for wxNote rather than selected from Tabler, and are
therefore **not** covered by the Tabler attribution above. They are licensed under the
project's own licence (Apache-2.0, see the root `LICENSE`). All three follow this set's
conventions — 24x24 viewBox, `fill="none"`, `stroke="currentColor"`, round caps/joins — so
they retint with the theme exactly like the selected Tabler icons:

| File                 | Why it exists                                                        |
| -------------------- | -------------------------------------------------------------------- |
| `wrap-selection.svg` | Curly braces around two text lines - "wrap the selection in delimiters". Tabler has no glyph for this concept, and every near-miss (`braces`, `code`) is already spoken for by another wxNote command. |
| `print-preview.svg`  | Page text with a magnifier over it. Tabler's print glyphs are the printer device itself, which cannot distinguish preview from print. |
| `func-node.svg`      | The Function List tree's GROUP node (class/namespace/section): a stack of layers, drawn in the idiom of Tabler's own `stack-2` but on this set's own coordinates. The tree's LEAF node next to it is Tabler's stock `math-function` (the italic *fx*), which is a selected glyph and *is* covered by the attribution above. |

See the root `LICENSING.md` and `NOTICE` for the project-wide licensing summary this
entry is consistent with.
