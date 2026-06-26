# wxNotepad++ — Future Plans & Licensing Roadmap

## Where we are today

wxNotepad++ is distributed under **GPL v3** (see [`LICENSE`](../LICENSE)), consistent with its
Notepad++ heritage. That is the honest, conservative position for now — see "why we're still GPL" below.

**The de-GPL engineering is now complete.** The core ships its own original cross-platform plugin API
("Nib"), and the entire Notepad++-ABI reproduction has been moved out of the core into a separate,
optional, GPL `npp-bridge` plugin. `src/` now contains **zero Notepad++-derived code** (verified), yet
real N++ binary plugins still load and run through the bridge. So staying on GPL is now a *deliberate
choice about timing*, not something forced by lingering N++ code in the core.

## Where we want to go

Our committed goal is to **drop every GPL dependency and relicense wxNotepad++ under a permissive
license** (Apache-2.0 / MIT-style).

**This has no commercial agenda.** We are *not* trying to make wxNotepad++ proprietary, sell it, or
close any of it off. The only reason for wanting a permissive license is to give **users and downstream
developers the most freedom possible** — to use, study, modify, embed, fork, and redistribute the editor
however they like, including in places a copyleft license makes awkward (other permissively-licensed
projects, commercial-but-unrelated products, redistributions with different terms, etc.). Permissive
does not mean "for profit"; it means *fewer restrictions on you*. The project stays free and open either
way.

## Why we're still GPL for now (the honest reasons)

We have already done most of the de-GPL engineering (see the progress table below). What still keeps us
conservatively on GPL is **not** lingering GPL files — it's three unresolved gates:

1. **The plugin-ABI compatibility layer is legally unsettled — and is now confined to the GPL bridge.**
   To load real Notepad++ plugins, some headers reproduce N++'s ABI (struct layouts + message numbers).
   Reproducing an ABI for interoperability is *defensible* (cf. *Google v. Oracle* + the merger doctrine)
   but **not settled law**. Rather than bet the core on that, we moved the entire reproduction into the
   optional GPL `npp-bridge`: the **core reproduces nothing** and the grey area stays GPL, compliant under
   any reading. This gate no longer blocks the *core's* relicense — it is resolved by construction.
2. **Trademark — name rebranded.** The app now uses the **wxNotepad++ name** throughout: a "wxNotepad++"
   window title, an "About wxNotepad++" box with an independence disclaimer, and rebranded menu labels.
   The app icon is the project's own SVG (`src/app_icon_svg.h`); its green plate + "N" monogram echo the
   upstream styling. The remaining "Notepad++" mentions are nominative (code comments, the
   `<NotepadPlus>` data format, and Help-menu links to Notepad++'s own resources).
3. **A final clean-room audit of `src/`.** — ✅ **done.** A full line-audit confirmed the core
   (`main.cpp` + `npp_menu.h`) carries no `NPPM_*`/`NppData`/`FuncItem` ABI code and no N++ plugin-ABI
   headers — only nominative references (comments, the `<NotepadPlus>` data format, Help links). The one
   N++-ABI *consumer* still in the tree (the Windows-only test-fixture plugin) was relocated out of
   `src/` to `packages/test_plugin/`, so `src/` is now uniformly permissive.

The engineering gates are now cleared — the core is *technically* relicensable. We nonetheless keep GPL
until the maintainer makes the deliberate call; claiming permissive prematurely would be dishonest.

## The plan to get there

1. **Ship our own original plugin API (codename "Nib").** — ✅ **done.** A clean-sheet, cross-platform,
   permissive API (`include/nib/nib.h`) that borrows nothing from N++ (no `NPPM_*`, `FuncItem`, `NppData`,
   `SCNotification`, `WM_USER`, or `HWND` in the contract). Interface-query + typed-vtable model with a
   manifest, typed events, and capabilities. Interfaces shipped: nib.host / nib.editor / nib.commands /
   nib.events / nib.panels, plus a Windows-only native-handle capability (`nib.win32`).
2. **Isolate the N++ ABI into a separate, optional GPL "bridge" plugin.** — ✅ **done.** The N++-ABI
   reproduction (`include/npp-compat/`) plus the host's loader + `NPPM_*` router + `beNotified` now live
   in the GPL `packages/npp-bridge` (itself a Nib plugin) that reaches the core only through `nib.win32`
   and translates N++ `NPPM_*`/`FuncItem`/`SCNotification` ⇄ Nib. **The core now contains zero N++-derived
   code** — verified, and real N++ plugins (MIME Tools, Converter) still load + run through the bridge.
3. **Rebrand** — ✅ name done: window title, About box, dialog captions, and menu labels now carry the
   wxNotepad++ name (the app icon is the project's own SVG).
4. **Clean-room audit** of `src/` — ✅ **done.** Full line-audit; only nominative N++ references remain.
   The one N++-ABI test fixture was moved out of `src/` to `packages/test_plugin/`, keeping `src/` permissive.
5. **Relicense the core permissively.** — the remaining step, a deliberate maintainer decision. The
   optional N++-bridge stays GPL; everyone else gets a permissive editor.

## De-GPL progress so far

Engineering already completed toward the goal (kept here for the record):

| Notepad++-derived item | Replaced with | Status |
|---|---|---|
| Toolbar icons | Tabler × Open Color SVG set (MIT) | ✅ done |
| Command ids (`menuCmdID.h`) | our own command-id header | ✅ done (clean-room) |
| Own permissive plugin API ("Nib", `include/nib/`) | new clean-sheet API — host/editor/commands/events/panels/win32 | ✅ done |
| Plugin-ABI reproduction (`NPPM_*`/`FuncItem`/`NppData` + the host) | moved OUT of the core into the GPL `packages/npp-bridge`; core has none | ✅ done (gate #1 resolved by construction) |
| Theme XMLs | regenerated from permissive palettes (GitHub/One/Nord/Dracula/VS Code, MIT) + kept-MIT themes | ✅ done |
| Default styler (`stylers.model.xml`) | regenerated, factual Lexilla structure + permissive palette | ✅ done |
| GPL lexers in the vendored Lexilla (`LexUser`, `LexSearchResult`, `LexObjC`) | removed (unused) | ✅ done |

The source tree no longer contains Notepad++ *source*, and the **functional** ABI reproduction is no
longer in the core — it now lives only in the optional GPL `packages/npp-bridge`. The core's remaining
tie to Notepad++ is down to nominative references (comments, the `<NotepadPlus>` data format, Help links).

> *None of the above is legal advice; the licensing notes are this project's own reasoning.*
