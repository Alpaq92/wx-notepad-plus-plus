# Plan: Notepad++ plugins on Linux/macOS (source-level recompilation)

**Status: plan, not implementation.** Nothing here is built yet. This documents the
research and phased design for extending `packages/npp-bridge/` so that real Notepad++
plugins can run on Linux and macOS — not by loading their Windows binaries (impossible:
they are Win32 PE DLLs), but by having plugin authors **recompile their plugin source**
against a wxNote-provided compatibility layer.

## Why recompilation, not emulation

A real N++ plugin on Windows is a `.dll` that talks to the host exclusively through
Win32 mechanisms: it receives three real `HWND`s (`NppData`), and everything it does —
every `NPPM_*` host request, every `SCI_*` editor call — is a
`SendMessage(hwnd, msg, wParam, lParam)`. On Linux/macOS there are no HWNDs and no
message loop, so the binary can never load. But the *source* of most plugins is far less
Windows-bound than it looks: the message-passing surface is narrow, repetitive, and
mechanical to reimplement. The plan is therefore a **source shim**: the plugin's own code
compiles unchanged (or nearly so) into a `.so`/`.dylib`, and the shim's `SendMessage`
routes to wxNote's portable internals instead of a Win32 window.

## Current state (verified against the tree, 2026-07-11)

- `packages/npp-bridge/npp_bridge.cpp` (Windows-only, GPL-3.0-or-later) bridges **26
  `NPPM_*` messages** today (`bridge_handleNppm`) and forwards 4 notification kinds to
  plugins' `beNotified` (`SCN_MODIFIED` insert/delete, `SCN_UPDATEUI` selection,
  `SCN_SAVEPOINTREACHED`, `NPPN_BUFFERACTIVATED`). Everything else falls through and
  returns 0. `messageProc` is fetched but never invoked; `FuncItem`'s `_cmdID`,
  `ShortcutKey`, and `_init2Check` are ignored.
- Its central mechanism is **fundamentally Win32**: `SetWindowSubclass` on the real frame
  HWND intercepts plugins' `SendMessage` calls; the core (`src/main.cpp`, `SciHwndProc`)
  likewise subclasses each editor view's HWND and routes `SCI_*` message ranges
  (2000–2999 / 4000–4999) into `wxStyledTextCtrl::SendMsg`.
- **The header layer for source recompilation already exists**:
  `include/npp-compat/npp_plugin_port.h` already defines non-Windows equivalents
  (`HWND` = `void*`, `LRESULT` = `intptr_t`, `NPP_EXPORT` = `visibility("default")`,
  `RECT`/`POINT` structs) precisely so plugin sources *compile* off-Windows. What's
  missing is everything at runtime: a non-Windows loader, a `SendMessage`
  implementation, and the dispatch behind it.

## What real plugins actually use (measured, not guessed)

Three real codebases were categorized (official plugin template, NppConverter,
ComparePlus):

| Dependency class | Typical share of a plugin's code | Portability through a shim |
|---|---|---|
| `NPPM_*` / `NPPN_*` messages | ~10–15 distinct messages covers most plugins | Shim-portable (mechanical) |
| Direct `SCI_*` calls (incl. the `SCI_GETDIRECTFUNCTION` fast path) | The bulk of most plugins' host interaction | Shim-portable — wxSTC embeds real Scintilla, which has a real DirectFunction |
| Raw Win32 UI (`.rc` resource dialogs + DlgProcs, GDI painting, common-controls) | 30–40%, concentrated almost entirely in dialog/panel files | **Not** shim-portable cheaply — the hard part |
| Other Windows APIs (INI files, shlwapi paths, clipboard, registry) | Small | Case-by-case; mostly easy equivalents |

Two structural findings matter beyond the percentages:

1. **Nearly every UI plugin vendors N++'s `DockingFeature` framework verbatim**
   (`Docking.h`, `StaticDialog`, `DockingDlgInterface`, `Window.h` — copy-pasted into
   each plugin's tree). That folder is *de facto* plugin API surface: reimplement those
   few classes once on wxWidgets, and every plugin that uses them gets its docking
   panel back without per-plugin work — except that they wrap `.rc` dialog resources,
   which is exactly the wall (see Tier 2).
2. **Heavy plugins also drive the host's own UI directly** — ComparePlus pokes N++'s tab
   control with `TabCtrl_*` macros, restyles it via `SetWindowLongPtr`, sends `NPPM_MENUCOMMAND`
   with `IDM_*` ids (so the shim also needs the frozen `src/command_ids.h` table, which
   wxNote already keeps value-identical for exactly this kind of reason), and paints its
   navigation bar with raw GDI. That class of code can't be shimmed honestly.

### Realistic ecosystem tiers

- **Recompile nearly unchanged** (message-passing + text processing, no custom UI):
  mimeTools, Code Alignment-style transformers, Chinese Converter, Language Selector,
  CompressedFileViewer, and lexer plugins (GEDCOM, Papyrus Script, EnhanceAnyLexer —
  `ILexer5` is already platform-neutral).
- **Days of author effort** (one or two simple dialogs to rewrite): NppConverter's
  panel, CSV Lint, JSON Tools, MultiReplace.
- **Effectively a UI rewrite** (engines are portable C++, UI is not): ComparePlus,
  HEX-Editor, DSpellCheck, Explorer/NppFTP, PythonScript/LuaScript, NppExec (spawns
  `cmd.exe` consoles — Windows-semantic by design).

## Precedents (what history says works)

- **Winelib (rejected as a model).** Wine's recompile-against-Wine-headers mode covers
  essentially all of Win32, and still failed to gain adoption: you drag in the whole
  Wine runtime, the flagship port (Corel WordPerfect Office 2000) was slow and unstable
  and abandoned, and Google shipped Picasa-on-Linux as a *Windows binary under Wine*
  rather than a Winelib port. Lesson: full-fidelity Win32 emulation is the wrong
  trade for source ports.
- **Cockos SWELL (adopted as the model).** REAPER's extension API is Win32-flavored on
  every platform; on macOS/Linux, extensions recompile against SWELL — a deliberately
  *partial* Win32 subset where `HWND` is a real struct backed by a native NSView/GDK
  widget, plus a script that converts `.rc` dialog resources. Proven at scale: SWS, the
  largest REAPER extension (heavily Win32 GUI code), ships Windows/macOS/Linux from one
  codebase. SWELL is zlib-licensed. Lesson: **implement the narrow subset plugins
  actually use, back tokens with real native widgets only where needed, and provide a
  resource-conversion path.**
- **X-Plane's XPLM** shows opaque-token handles scale to a big plugin ecosystem — but
  only when designed in from day one, which N++'s API wasn't.
- **Nobody has shipped N++ plugin compat off-Windows before.** NotepadNext's maintainer
  called it "won't be possible"; Notepadqq went out-of-process JS instead. If this ships
  even at Tier 1, it's a genuine first.

## Architecture: three tiers

### Tier 1 — message shim (the core deliverable)

A new static library, `libnpp_shim`, that a plugin links when building for Linux/macOS:

- **Types**: `npp_plugin_port.h` already provides them (exists today).
- **`SendMessage` replacement**: the shim exports a `SendMessage(HWND, UINT, WPARAM,
  LPARAM)` symbol. The three `NppData` handles become opaque tokens the bridge hands
  out. Dispatch: `SCI_*` ranges route straight into `wxStyledTextCtrl::SendMsg` on the
  addressed view (main/sub — same routing the Windows `SciHwndProc` does today, minus
  the HWND subclassing transport); `NPPM_*`/`RUNCOMMAND` ranges route to the bridge's
  existing `bridge_handleNppm` logic, ported off `DefSubclassProc`.
- **`SCI_GETDIRECTFUNCTION` / `CallScintilla`**: answered honestly — wxSTC embeds real
  Scintilla, which has a real direct function; the shim returns it. This is the fast
  path heavy plugins (ComparePlus) already use.
- **Loader**: `wxDynamicLibrary` scanning `<exe>/plugins/<Name>/<Name>.so|.dylib`,
  replacing `LoadLibraryW`/`FindFirstFileW`. Same six-symbol entry contract
  (`setInfo`/`getName`/`getFuncsArray`/`beNotified`/`messageProc`/`isUnicode`).
- **Small Win32-API stubs** plugins commonly need: `MessageBox` → `wxMessageBox`,
  `GetPrivateProfileString` family → a tiny INI reader, `PathFileExists` →
  `wxFileExists`, basic clipboard → wx clipboard. SWELL's scope discipline applies:
  implement what surveyed plugins use, refuse the rest loudly (link errors are a
  feature — they tell the author exactly what to port).

**Unlocks:** the text-transformer + lexer third of the ecosystem, nearly for free.

### Tier 2 — docking/dialog layer (the hard 40%)

Reimplement the vendored `DockingFeature` classes (`StaticDialog`,
`DockingDlgInterface`) on wxWidgets, so the de-facto-standard panel pattern works. The
wall: those classes wrap `.rc` resource dialogs and DlgProcs. Options, in order of
preference:

1. **wx-native rewrite path** (recommended start): the shim provides the
   `DockingDlgInterface` lifecycle (register/show/hide/update → wxAui panes via the
   bridge), but the dialog *content* must be rebuilt by the author as a `wxPanel`.
   Honest, bounded per-plugin effort — the "days of work" tier.
2. **`.rc` interpreter** (SWELL's `swell-dlggen` approach): a converter that turns
   dialog resources into wx sizer layouts and a `DlgProc`-emulation for
   `WM_INITDIALOG`/`WM_COMMAND`/`SendDlgItemMessage` with common-control messages.
   High leverage, large surface — only worth building if Tier 1+2.1 adoption proves
   demand.
3. **Full GDI/custom-paint emulation**: not planned (Winelib lesson).

### Tier 3 — host-UI surgery (explicitly out of scope)

Plugins that subclass N++'s own controls, restyle host windows, or spawn Windows
consoles (ComparePlus's nav bar, NppExec) are out of scope on non-Windows platforms,
permanently. The honest recommendation for those authors is a native port of their UI
layer on top of Tiers 1–2 (their engines — diff, spell-check, hex logic — are already
portable C++).

## Nib API prerequisites

The bridge is itself a Nib plugin, and today's `nib.h` can't express what a
cross-platform bridge needs. Gaps, in priority order (from the bridge inventory):

1. **`nib.sci/1` — generic Scintilla pass-through** (`sci_call(view, msg, wParam,
   lParam)`), per-view addressing (main/sub). The single biggest gap; everything else
   is decoration without it. (On Windows the bridge sidesteps this via HWND subclassing;
   off-Windows there is no sidestep.)
2. **Richer events** mapping to the `NPPN_*` set plugins expect: file
   opened/closed/before-save, ready, shutdown, language-changed, plus `SCN` char-added /
   margin-click / dwell.
3. **Document/host state**: language get/set, encoding/EOL, dirty flag, save-as/save-all,
   status-bar text (replacing the `msctls_statusbar32` hack), menu-item check state,
   shortcut registration (honoring `FuncItem::ShortcutKey`, ignored today).
4. **Panels beyond text**: `nib.panels/1` is text-only; Tier 2 needs "host a
   plugin-provided wxWindow" (and on Windows, the existing native-HWND docking stays).
5. **Config-dir correctness**: the bridge currently answers `NPPM_GETPLUGINSCONFIGDIR`
   with exe-relative paths — must move to `userDataDir()` (known project convention:
   exe dir isn't user-writable on installed builds). The portable API for this now
   exists: **`nib.paths/1`** (`user_data_dir(host, buf, cap)`) was added to the core
   this round, so the remaining work is wiring the bridge onto it rather than adding a
   new capability.
6. Later: allocate-cmdID/marker/indicator, dark-mode query, inter-plugin messaging
   (`NPPM_MSGTOPLUGIN`).

## Licensing

- `libnpp_shim` + the extended bridge stay **GPL-3.0-or-later** inside
  `packages/npp-bridge/` (same reasoning as today: it reproduces the N++ plugin ABI;
  the planned split into a separate repo carries this whole plan with it).
- `include/npp-compat/` headers stay Apache-2.0 (clean-room ABI facts, unchanged).
- The Nib API additions (`nib.sci/1`, panel/event extensions) are core, Apache-2.0 —
  they're general-purpose capabilities, useful to native Nib plugins too, with no N++
  specifics.
- If any SWELL code is adapted (rather than just its ideas), zlib license is compatible
  with both zones; attribution goes in `docs/CREDITS.md`.

## Phases

| Phase | Deliverable | Depends on | Risk |
|---|---|---|---|
| 1 | `nib.sci/1` (+ per-view addressing) in core; Windows bridge migrates its SCI routing onto it (proves the API against real plugin binaries — the existing Windows binary-loading path is the test harness) | — | Medium |
| 2 | Richer Nib events + document/host-state APIs; Windows bridge forwards the full `NPPN_*` set | 1 | Medium |
| 3 | `libnpp_shim` Tier 1: loader, token-HWND `SendMessage`, Win32-API stubs; CI builds a sample plugin (official template + one real text-transformer, e.g. mimeTools) on all three platforms | 1, 2 | Medium-high |
| 4 | Tier 2.1: `DockingDlgInterface`/`StaticDialog` on wx + `nib.panels` widget hosting; port NppConverter's panel as the proof | 3 | High |
| 5 | Author-facing docs + a `plugintemplate-crossplatform` fork showing the CMake setup; publish shim as part of the future separate npp-bridge repo | 3 | Low |
| 6 (only if demand) | Tier 2.2 `.rc` dialog converter | 4 | High |

**Sequencing note:** Phases 1–2 are pure wins even if the cross-platform shim never
ships — they thicken the Nib API and put the *Windows* bridge on portable plumbing,
shrinking its Win32-only surface to the binary-loading transport itself.

## Success criteria & non-goals

- **Success**: an unmodified (or trivially patched) text-transformer plugin's source
  builds as a `.so`/`.dylib` via documented CMake, loads in wxNote on Linux/macOS, its
  commands appear in Extensions, and it manipulates the buffer correctly.
- **Non-goals**: running Windows plugin *binaries* off-Windows (that's Wine's job);
  Win32-emulation completeness (Winelib's grave); Tier-3 host-UI surgery; any promise
  that the marquee heavy-UI plugins arrive without author effort.

## Open questions

1. Whether Tier 1 plugins should be *dual-target* — the same recompiled source could
   also target Nib natively via a thin adapter, letting authors migrate off the N++ ABI
   entirely over time. Worth designing the shim so this is possible.
2. Distribution: does wxNote's site/docs host a list of known-good recompiled plugins,
   or does that live with the future separate npp-bridge repo?
3. Naming: "npp-shim"? "npp-sdk"? The author-facing name matters for discoverability.
