# Licensing

> **Not legal advice.** This records how wxNotepad++'s components are licensed, as understood from the
> license files in this repository. For anything you intend to rely on, consult a lawyer and read the
> full texts in [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), and the `third_party/*/License.txt` files.

## TL;DR

**wxNotepad++ is distributed under the GNU GPL v3** (see [`LICENSE`](LICENSE)), consistent with its
Notepad++ heritage. It is an *independent reimplementation* — no Notepad++ source code is copied into
`src/`; Notepad++ serves only as a functional reference and a test target (see [`NOTICE`](NOTICE)).

We have already replaced or clean-room-reimplemented essentially every Notepad++-derived file (icons,
command ids, plugin-ABI headers, themes, the default styler, and three GPL lexers). **Our committed goal
is to relicense the project permissively** — purely to give users and downstream developers more
freedom. **There is no commercial agenda.** We remain on GPL for now because that transition isn't safe
to *claim* yet; the full reasoning, progress, and plan live in
[`docs/FUTURE_PLANS.md`](docs/FUTURE_PLANS.md).

## Why still GPL (short version)

Three gates remain before a permissive relicense would be honest:

1. The plugin-ABI compatibility layer reproduces N++'s ABI for interoperability — permissively licensing
   that is legally unsettled (defensible under *Google v. Oracle* + the merger doctrine, but not
   settled law).
2. The application still presents the **"Notepad++" name and logo** (a trademark matter) and must be
   rebranded.
3. `src/` should get a clean-room audit to back the "no copied code" claim.

[`docs/FUTURE_PLANS.md`](docs/FUTURE_PLANS.md) explains these and the **Nib-API + GPL-bridge** plan that
severs gate #1 (move the ABI reproduction into a separate, optional GPL plugin so the core can go
permissive with zero N++-derived code).

## Per-component licenses

The project **as a whole is GPL v3**. Individual components we authored are licensed permissively where
we are confident — permissively-licensed files composing into a GPL aggregate is normal and intended:

| Component | License | Notes |
|---|---|---|
| `src/` (the editor) | under the project's **GPL v3** | original reimplementation |
| Toolbar icons — `resources/icons/` | **MIT** | Tabler © Paweł Kuna, Open Color © Heeyeun Jeong |
| Colored toolbar icon option — `resources/icons-solar/` | **CC BY 4.0** | Solar Icons (Bold Duotone) © 480 Design, recoloured to Open Color green-8 / green-3 |
| Colored toolbar icon option — `resources/icons-iconpark/` | **Apache-2.0** | IconPark © ByteDance, recoloured to Open Color teal-7 / lime-5 (thin stroke) |
| Colored toolbar icon option — `resources/icons-iconpark-bold/` | **Apache-2.0** | IconPark © ByteDance, recoloured to Open Color teal-7 / lime-5 (bold stroke) |
| Plugin ABI headers — `include/npp-compat/` | Apache-2.0 *expression*, but they **functionally reproduce N++'s GPL ABI** (gate #1) | to be replaced by the permissive Nib API |
| Regenerated themes + `stylers.model.xml` | **Apache-2.0** | our data: factual Lexilla structure + permissive palettes |
| Kept third-party themes — `resources/themes/` | **MIT** / upstream-permissive | © Fabio Zendhi Nagao, Oren Farhi, … |
| Scintilla / Lexilla — `third_party/` | **HPND** (permissive) | the editing/highlighting engine |
| wxWidgets | wxWindows Licence (LGPL + static-link exception) | fetched at build, not vendored |

## Scintilla / Lexilla license (permissive)

From `third_party/scintilla/License.txt` and `third_party/lexilla/License.txt` (Neil Hodgson, HPND-style):

> Permission to use, copy, modify, and distribute this software and its documentation for any
> purpose and without fee is hereby granted, provided that the above copyright notice appear in
> all copies and that both that copyright notice and this permission notice appear in supporting
> documentation.

Permissive and copyleft-free; it requires only that the copyright notice be preserved.

## Trademark

"Notepad++" is a trademark of its owner. wxNotepad++ is not affiliated with or endorsed by Notepad++ and
references the name only nominatively. The application uses the **wxNotepad++ name** throughout — a
"wxNotepad++" window title, an "About wxNotepad++" box with an independence disclaimer, and rebranded
menu labels. The app icon is the project's own SVG (its green plate + "N" monogram echo the upstream
styling). The remaining "Notepad++" mentions are nominative: code comments, the `<NotepadPlus>`
theme/session data format, and Help-menu links to Notepad++'s own resources.
