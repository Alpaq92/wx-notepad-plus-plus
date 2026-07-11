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
| Colored toolbar icon option — `resources/icons-iconpark/` | **Apache-2.0** | IconPark © ByteDance, recoloured to Open Color teal-7 / lime-5 |
| Default editor font — `resources/fonts/` | **SIL OFL 1.1** | JetBrains Mono, unmodified, bundled in place of the proprietary Consolas |
| Plugin ABI headers — `include/npp-compat/` | Apache-2.0 *expression*, but they **functionally reproduce N++'s GPL ABI** (gate #1) | to be replaced by the permissive Nib API |
| Menu structure — `src/menu_builder.h` + `src/menu_data_*.h` | under the project's **GPL v3** (original code) | Phase B (2026-07-09) reshaped this into an original 10-menu hierarchy — no longer reproduces N++'s menu structure; only the numeric `IDM_*` ids carry over, as documented in the ABI-headers row above |
| Regenerated themes + `stylers.model.xml` | **Apache-2.0** | our data: factual Lexilla structure + permissive palettes |
| Kept third-party themes — `resources/themes/` | **MIT** (© Fabio Zendhi Nagao, Oren Farhi, Renato Silva) / **CC BY 3.0** (© Paul Neubauer) | each file keeps its original author header; see the removal note below |
| Scintilla / Lexilla — `third_party/` | **HPND** (permissive) | the editing/highlighting engine |
| wxBorderlessFrame (wxbf) — `third_party/wxbf/` | wxWindows Licence (LGPL + static-link exception) | vendored; Windows/Linux only (no macOS backend) |
| wxWidgets | wxWindows Licence (LGPL + static-link exception) | fetched at build, not vendored |
| Project site — `site/` | **MIT** (matches its template's license) | adapted from codewithsadee/vcard-personal-portfolio (MIT) — see [`site/CREDITS.md`](site/CREDITS.md) |

> **Removed theme (2026-07-11):** `DansLeRuSH-Dark.xml` (© Franck Albaret) was dropped from the shipped
> set. Its header contains MIT-style permission text, but that text is labeled "[ LEGAL DISCLAIMER ]"
> and is contradicted two lines later by an explicit `Licence : Creative Commons BY-NC-SA 3.0` field;
> the author's canonical repository
> ([codeberg.org/DansLeRuSH/notepad-plus-plus-dark-theme](https://codeberg.org/DansLeRuSH/notepad-plus-plus-dark-theme))
> declares the same CC BY-NC-SA 3.0 license, so the author's intent is NonCommercial-ShareAlike and we
> do not rely on the pasted MIT text. A NonCommercial asset is incompatible with this project's
> redistribution goals (and with non-free policies of Debian/Fedora/Flathub), so the theme is not
> shipped. It can return if the author grants a permissive license for our redistribution.

## Compatibility surface (ABI)

One part of this project intentionally reproduces *facts about how Notepad++ operates* — as distinct
from reproducing its code:

- **The plugin ABI** (`include/npp-compat/`): numeric message/command ids, struct layouts, and exported
  symbol names. These are the wire protocol a compiled N++ plugin speaks; a compatible host has no
  choice about their values. Covered by gate #1 above.

This does not involve copying Notepad++ implementation code. It is documented here so the
"independent reimplementation" claim above is precise about what *is* deliberately kept identical and
why.

> **Historical note:** until 2026-07-09, the menu *hierarchy* (which popup a command lived under, item
> order) also intentionally mirrored Notepad++'s, on the same interoperability-adjacent reasoning — the
> `IDM_*` ids double as plugin-invocable command ids, so keeping them in familiar places aided the port.
> That mirroring was never a legal necessity (menu trees are the kind of "method of operation" *Lotus v.
> Borland* held outside copyright's scope), and it was dropped in the Phase B menu redesign
> (`src/menu_builder.h`, `src/menu_data_*.h`): the app now uses an original 10-menu hierarchy
> (File/Edit/Selection/Go/View/Document/Automation/Extensions/Window/Help) designed from research across
> five editors (VS Code, Notepad++, Pulsar Edit, Android Studio, Visual Studio), not copied from any one
> of them. Only the numeric `IDM_*` ids still carry over, as an unavoidable ABI fact — see above.

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
