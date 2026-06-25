# wxNotepad++ — Future Plans & Licensing Roadmap

## Where we are today

wxNotepad++ is distributed under **GPL v3** (see [`LICENSE`](../LICENSE)), consistent with its
Notepad++ heritage. That is the honest, conservative position for now — see "why we're still GPL" below.

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

1. **The plugin-ABI compatibility layer is legally unsettled.** To load real Notepad++ plugins, our
   headers reproduce N++'s ABI (struct layouts + message numbers). Reproducing an ABI for
   interoperability is *defensible* (cf. *Google v. Oracle* + the merger doctrine) but **not settled
   law**. We won't claim a permissive license over it until a lawyer blesses it.
2. **Trademark — name rebranded.** The app now uses the **wxNotepad++ name** throughout: a "wxNotepad++"
   window title, an "About wxNotepad++" box with an independence disclaimer, and rebranded menu labels.
   The app icon is the project's own SVG (`src/app_icon_svg.h`); its green plate + "N" monogram echo the
   upstream styling. The remaining "Notepad++" mentions are nominative (code comments, the
   `<NotepadPlus>` data format, and Help-menu links to Notepad++'s own resources).
3. **A clean-room audit of `src/`.** The whole permissive claim rests on `src/` being original. We
   assert it is; a focused line-audit (especially where we matched N++ behavior closely) would make the
   claim airtight.

Until #1 is reviewed and #3 is done, **staying on GPL is the truthful choice** — claiming permissive
prematurely would be dishonest.

## The plan to get there

1. **Ship our own original plugin API (codename "Nib").** A clean-sheet, cross-platform, permissive API
   that borrows nothing from N++ (no `NPPM_*`, `FuncItem`, `NppData`, `SCNotification`, `WM_USER`, or
   `HWND` in the contract). It uses an interface-query + typed-vtable model with a declarative manifest,
   typed events, capabilities, and language-service hooks — aiming to be *more* capable than N++'s
   plugin model, not a relabel of it.
2. **Isolate the N++ ABI into a separate, optional GPL "bridge" plugin.** Today's `include/npp-compat/`
   plus the host's N++ message handling move out of the core into an `npp-bridge` package that
   translates N++ `NPPM_*`/`FuncItem`/`SCNotification` ⇄ Nib. It is GPL and shipped separately. After
   this move, **the core contains zero N++-derived code** — the legally-grey reproduction lives only in
   a GPL module, which is compliant under any reading.
3. **Rebrand** — ✅ name done: window title, About box, dialog captions, and menu labels now carry the
   wxNotepad++ name (the app icon is the project's own SVG).
4. **Clean-room audit** of `src/`.
5. **Relicense the core permissively.** The optional N++-bridge stays GPL; everyone else gets a
   permissive editor.

## De-GPL progress so far

Engineering already completed toward the goal (kept here for the record):

| Notepad++-derived item | Replaced with | Status |
|---|---|---|
| Toolbar icons | Tabler × Open Color SVG set (MIT) | ✅ done |
| Command ids (`menuCmdID.h`) | our own command-id header | ✅ done (clean-room) |
| Plugin ABI headers | clean-room, cross-platform headers in `include/npp-compat/` | ✅ done (see gate #1) |
| Theme XMLs | regenerated from permissive palettes (GitHub/One/Nord/Dracula/VS Code, MIT) + kept-MIT themes | ✅ done |
| Default styler (`stylers.model.xml`) | regenerated, factual Lexilla structure + permissive palette | ✅ done |
| GPL lexers in the vendored Lexilla (`LexUser`, `LexSearchResult`, `LexObjC`) | removed (unused) | ✅ done |

The source tree no longer contains Notepad++ *source*; the remaining tie is the **functional** ABI
reproduction (gate #1), which the Nib + bridge plan above is designed to sever.

> *None of the above is legal advice; the licensing notes are this project's own reasoning.*
