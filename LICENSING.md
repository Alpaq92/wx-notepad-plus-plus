# Licensing

> **Not legal advice.** This records how wxNote's components are licensed, as
> understood from the license files in this repository. For anything you
> intend to rely on, consult a lawyer and read the full texts in
> [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), and the `third_party/*/License.txt`
> files.

## TL;DR

**wxNote is distributed under the Apache License 2.0** (see [`LICENSE`](LICENSE)),
with one exception in what ships: `packages/npp-bridge/` (see below) stays
**GPL-3.0-or-later** because it reproduces Notepad++'s plugin ABI for
interoperability — its never-shipped, Windows-only test fixture
`packages/test_plugin/` tracks the same GPL license for the same reason. It
is an *independent reimplementation* — no Notepad++ source code is copied
into `src/`; Notepad++ serves only as a functional reference and a test
target (see [`NOTICE`](NOTICE)).

The project started on GPL v3 and relicensed once every engineering
prerequisite was cleared: every Notepad++-derived file was replaced or
clean-room-reimplemented (icons, command ids, plugin-ABI headers, themes, the
default styler, three GPL lexers, and eventually the menu hierarchy itself),
and the one genuinely unsettled piece — the Notepad++-ABI reproduction — was
moved out of the core entirely into the optional `packages/npp-bridge`
module, which keeps its own GPL license. **There is no commercial agenda**;
the point of going permissive is to give users and downstream developers more
freedom.

## Why npp-bridge (and its test fixture) stay GPL

`packages/npp-bridge/` lets wxNote load real, compiled Notepad++ binary
plugins. To do that, it has to speak Notepad++'s plugin ABI — the same
message numbers, struct layouts, and exported symbol names real Notepad++
uses. Reproducing an ABI for interoperability is *defensible* (cf. *Google v.
Oracle* and the merger doctrine), but not settled law, so rather than bet the
whole project on that reading, the reproduction is confined to this one
optional, separately-licensed module (which also depends on the
`include/npp-compat/` headers). The core links against neither and contains
no Notepad++-ABI code at all. **This module is planned to move into its own
separate repository** — it isn't tied to wxNote's own release cadence, and
splitting it out makes the boundary unambiguous rather than just documented.

## Per-component licenses

| Component | License | Notes |
|---|---|---|
| `src/` (the editor) | **Apache-2.0** | original reimplementation |
| Toolbar icons — `resources/icons/` | **MIT** | Tabler © Paweł Kuna, Open Color © Heeyeun Jeong |
| Colored toolbar icon option — `resources/icons-solar/` | **CC BY 4.0** | Solar Icons (Bold Duotone) © 480 Design, recoloured to Open Color green-8 / green-3 |
| Colored toolbar icon option — `resources/icons-iconpark/` | **Apache-2.0** | IconPark © ByteDance, recoloured to Open Color teal-7 / lime-5 |
| Default editor font — `resources/fonts/` | **SIL OFL 1.1** | JetBrains Mono, unmodified, bundled in place of the proprietary Consolas |
| Menu structure — `src/menu_builder.h` + `src/menu_data_*.h` | **Apache-2.0** (original code) | an original 11-menu hierarchy, designed from research across five editors — no longer reproduces N++'s menu structure; only the numeric `IDM_*` id *values* carry over, via the core's own `src/command_ids.h` (see below) |
| Command ids — `src/command_ids.h` | **Apache-2.0** | the core's own, authoritative id table; the core includes **nothing** from `include/npp-compat`. The numeric values are frozen to stay value-identical with the plugin ABI's ids so npp-bridge's `NPPM_MENUCOMMAND` passthrough dispatches correctly — an interoperability fact, enforced by static_asserts |
| Regenerated themes + `stylers.model.xml` | **Apache-2.0** | our data: factual Lexilla structure + permissive palettes |
| Kept third-party themes — `resources/themes/` | **MIT** (© Fabio Zendhi Nagao, Oren Farhi, Renato Silva) / **CC BY 3.0** (© Paul Neubauer) | each file keeps its original author header; see the removal note below |
| Scintilla / Lexilla — `third_party/` | **HPND** (permissive) | the editing/highlighting engine |
| wxBorderlessFrame (wxbf) — `third_party/wxbf/` | wxWindows Licence (LGPL + static-link exception) | vendored; Windows/Linux only (no macOS backend) |
| wxWidgets | wxWindows Licence (LGPL + static-link exception) | fetched at build, not vendored |
| Project site — `site/` | **MIT** (matches its template's license) | adapted from codewithsadee/vcard-personal-portfolio (MIT) — see [`site/CREDITS.md`](site/CREDITS.md) |
| Plugin ABI headers — `include/npp-compat/` | **Apache-2.0** | clean-room *expression* of N++'s ABI facts (numeric ids, struct layouts, entry-point names). Consumed **only** by `packages/npp-bridge/` and `packages/test_plugin/` — the core includes nothing from this directory. Apache code being consumed by GPL code is one-directional-compatible and unproblematic; it does not make this directory GPL. |
| **`packages/npp-bridge/`** | **GPL-3.0-or-later** | its own [`LICENSE`](packages/npp-bridge/LICENSE); see "Why npp-bridge (and its test fixture) stay GPL" above |
| **`packages/test_plugin/`** | **GPL-3.0-or-later** | its own [`LICENSE`](packages/test_plugin/LICENSE); a Windows-only test fixture that consumes the same N++ ABI as npp-bridge, so it tracks the same license rather than the Apache default |

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

One part of this project intentionally reproduces *facts about how Notepad++
operates* — as distinct from reproducing its code:

- **The plugin ABI** (`include/npp-compat/`): numeric message/command ids,
  struct layouts, and exported symbol names. These are the wire protocol a
  compiled N++ plugin speaks; a compatible host has no choice about their
  values. `packages/npp-bridge/` is the only shipped component that uses
  this surface, which is why the *bridge* — not these headers — is the GPL
  boundary. The core includes nothing from this directory; its own
  `src/command_ids.h` merely keeps its numeric command-id *values* identical
  to the ABI's, so the bridge's `NPPM_MENUCOMMAND` passthrough dispatches
  correctly.

This does not involve copying Notepad++ implementation code. It is documented
here so the "independent reimplementation" claim above is precise about what
*is* deliberately kept identical, and why.

> **Historical note:** until 2026-07-09, the menu *hierarchy* (which popup a
> command lived under, item order) also intentionally mirrored Notepad++'s,
> on the same interoperability-adjacent reasoning — the `IDM_*` ids double as
> plugin-invocable command ids, so keeping them in familiar places aided the
> port. That mirroring was never a legal necessity (menu trees are the kind
> of "method of operation" *Lotus v. Borland* held outside copyright's
> scope), and it was dropped in the Phase B menu redesign
> (`src/menu_builder.h`, `src/menu_data_*.h`): the app now uses an original
> 11-menu hierarchy (File/Edit/Selection/Go/View/Document/Automation/
> Extensions/Settings/Window/Help) designed from research across five editors
> (VS Code, Notepad++, Pulsar Edit, Android Studio, Visual Studio), not
> copied from any one of them. Only the numeric `IDM_*` id values still carry
> over, as an unavoidable ABI fact — see above.

## Scintilla / Lexilla license (permissive)

From `third_party/scintilla/License.txt` and `third_party/lexilla/License.txt`
(Neil Hodgson, HPND-style):

> Permission to use, copy, modify, and distribute this software and its
> documentation for any purpose and without fee is hereby granted, provided
> that the above copyright notice appear in all copies and that both that
> copyright notice and this permission notice appear in supporting
> documentation.

Permissive and copyleft-free; it requires only that the copyright notice be
preserved.

## Trademark

"Notepad++" is a trademark of its owner. wxNote is not affiliated with or
endorsed by Notepad++ and references the name only nominatively. The
application uses the **wxNote name** throughout — a "wxNote" window title, an
"About wxNote" box with an independence disclaimer, and its own menu labels.
The app icon is the project's own SVG. The remaining "Notepad++" mentions are
nominative: code comments describing ABI/format compatibility, the
`<NotepadPlus>` theme/session data format (kept so real Notepad++ theme files
still load unmodified), and Help-menu links to Notepad++'s own resources.
