# Plan — cross-platform wxNote with Windows binary-compatibility for Notepad++ plugins

How to make wxNote run natively on Windows, Linux, and macOS, while **on Windows** it can still
load existing **Notepad++ plugin binaries (`.dll`) unchanged**.

## The idea in one picture

```
                       ┌─────────────────────────────────────────────┐
   all platforms ──▶   │  wxNote CORE  (wx + wxStyledTextCtrl)  │   ◀── permissive (Apache)
                       │  + the Nib plugin API  (include/nib/nib.h)  │
                       └───────────────┬───────────────┬─────────────┘
                          loads (dlopen)│               │ loads (dlopen) — Windows only
                       ┌───────────────▼──────┐   ┌─────▼───────────────────────────┐
   all platforms ──▶   │  native Nib plugins  │   │  npp-bridge  (GPL, Win-only)    │  ◀── GPL
                       │  (recompiled per OS) │   │  loads N++ .dll, NPPM_* ⇄ Nib   │
                       └──────────────────────┘   └─────┬───────────────────────────┘
                                                        │ LoadLibrary + N++ ABI (Win32)
                                                  ┌─────▼──────────────┐
                                  Windows only ──▶│ existing N++ .dll  │
                                                  └────────────────────┘
```

**One portable core + one portable plugin API for every OS; the N++ binary-compat layer is a separate,
optional, Windows-only, GPL module.** That keeps the core permissive and cross-platform, and confines the
legally-grey ABI reproduction to the one place it can only run anyway (Windows).

## Principles

1. **One codebase, conditional only at the edges.** wxWidgets already gives a native UI on every OS
   (Win32 / GTK3 / Cocoa). Platform `#ifdef`s are for *enhancements*, not core function.
2. **The core never depends on Notepad++ or Win32-plugin specifics.** All of that lives in `npp-bridge`.
3. **N++ binary compatibility is Windows-only by nature** — N++ plugins are Win32 DLLs. Linux/macOS get
   native Nib plugins (recompiled), not N++ binaries. This is a fact, not a limitation we can engineer away.
4. **Windows-only polish degrades gracefully** elsewhere — where Windows uses DWM/uxtheme, GTK/Cocoa let
   the system theme the window instead.

## Target support matrix

| Capability | Windows | Linux | macOS |
|---|:---:|:---:|:---:|
| Editor, themes, find/replace, panels, UI | ✅ | ✅ | ✅ |
| **Nib** plugins (native, recompiled per OS) | ✅ | ✅ | ✅ |
| **Notepad++ plugins (binary `.dll`)** | ✅ via `npp-bridge` | — | — |
| Native dark **title bar** | ✅ DWM | system/GTK | system/Cocoa |
| Native dark **scrollbars** / custom size-grip | ✅ uxtheme | GTK theme | Cocoa theme |

## Where we are (baseline)

- **Done:** the wx core, 28 permissive themes, the **Nib API** (`nib.host`/`nib.editor`/`nib.commands`/
  `nib.events`/`nib.documents`/`nib.panels`, plus the Windows-only `nib.win32` capability — cross-platform,
  verified on Windows), and the **N++ host has been carried out of the core** into the optional GPL
  `packages/npp-bridge`, which reaches the frame only through `nib.win32`.
- **Windows-only code:** **~53 `__WXMSW__` guards** in `src/main.cpp` — none of it is the N++ host anymore
  (that moved to `npp-bridge`; the core keeps only the generic, non-N++-derived `SciHwndProc` editor
  bridge that `nib.win32` hands out). The guards remaining are the DWM dark title bar (`setTitleBarDark` /
  `DwmSetWindowAttribute`), native scrollbar theming (`SetWindowTheme`), the custom size-grip
  (`SizeGripWin`), and other Windows-only polish added since. `comctl32/uxtheme/dwmapi` are linked only
  `if(WIN32)`.
- **Not done:** counsel review of the ABI-reproduction approach (see `FUTURE_PLANS.md`) and the maintainer's
  actual relicense decision are still open. Everything else below is now verified, not just structured.

---

## Phase 1 — Make the core genuinely cross-platform (and prove it) — ✅ done

**Goal:** editor + themes + Nib plugins build and run on Windows, Linux, and macOS.

1. **Run the CI matrix.** — ✅ **done.** `build.yml` has the `windows` / `ubuntu` / `macos` matrix, with
   wxWidgets-build caching and GTK3 install, and it runs green on every push/PR to `wx-migration` and on
   merges to `master` (it also triggers on `include/**`). The repo is public, so free-tier Actions minutes
   are not a blocker.
2. **Fix per-platform build errors.** — ✅ **done.** wxWidgets is portable and the `__WXMSW__` blocks
   compile out cleanly; the remaining per-OS differences (GTK3 dev packages on Linux, the Ninja generator
   + MSVC dev environment on Windows) are handled in `build.yml`.
3. **Audit the `__WXMSW__` blocks** and classify each: (a) *graceful-degrade* — the system already
   handles it on Linux/macOS (dark title bar, native scrollbars, resize grip); or (b) *needs a portable
   path*. The N++ host blocks no longer exist in the core (see Phase 3, done) — the remaining guards are
   all Windows-only polish, category (a).
4. **Verify per OS:** launch, open a file (highlighting), apply a theme, load `nib_test_plugin`, run its
   command.

**Done when:** CI is green on all three OSes and the Nib smoke test passes on each. — ✅ **met**: all three
matrix jobs (`windows`/`linux`/`macos`) are green on current `wx-migration`/`master`.

## Phase 2 — Grow the Nib API to what plugins (and the bridge) need — ✅ done

**Goal:** enough Nib surface that real plugins *and* the N++ bridge can be built. All additive, all portable.

- `nib.events` — ✅ shipped. Typed events (text-changed, doc opened/saved/closed, selection-changed).
- `nib.documents` — ✅ shipped. The open-document set + lifecycle.
- `nib.panels` — ✅ shipped. Dockable panels, backed by the existing cross-platform `wxAuiManager`.
- `nib.notifications` — not shipped (no `NIB_IFACE_NOTIFICATIONS` in `include/nib/nib.h`). Not needed by
  the bridge or by `nib_test_plugin` so far; still a candidate for a future Nib API bump if a plugin needs
  toasts/dialogs/input beyond what `nib.win32` + host dialogs already cover.
- `nib.win32` — ✅ shipped. **A Windows-only capability** that hands a plugin the native `HWND`s + a
  message-hook channel. This is the escape hatch `npp-bridge` uses to reconstruct the `NppData` world; on
  Linux/macOS the capability simply isn't offered.

Dogfood each new interface by extending `nib_test_plugin`. — ✅ done: `nib_test_plugin.cpp` exercises
`nib.win32`, `nib.panels`, and `nib.events`.

**Done when:** the reference plugin exercises events + a panel, and `nib.win32` exposes the editor HWNDs
on Windows (a prerequisite for Phase 3). — ✅ **met.**

## Phase 3 — Carve the N++ host into the GPL bridge (Windows binary-compat, isolated) — ✅ done

**Goal:** the core ends with **zero** N++-derived code, while Windows N++ binary-compat is preserved.

1. Create `packages/npp-bridge/` — a GPL, Windows-only build target, and **move** `include/npp-compat/`
   plus the N++ host out of `src/main.cpp` (`loadPlugins`, `buildPluginsMenu`, `handleNppm`,
   `FrameSubclassProc`, `SciHwndProc`, `m_npp`/`m_plugins`/`LoadedPlugin`, `pluginsMenuHandle`). — ✅ done:
   `packages/npp-bridge/npp_bridge.cpp` exists and owns all of this.
2. Restructure it as a Windows-only **Nib plugin**: on `activate`, it uses `nib.win32` to get the editor
   HWNDs, builds `NppData`, installs the `WM_USER+1000` router, scans `plugins/`, `LoadLibrary`s the N++
   DLLs, and translates **NPPM_\* ⇄ Nib**, **SCNotification ⇄ nib.events**, **FuncItem → nib.commands/menu**,
   **NPPM_DMMREGASDCKDLG → nib.panels**. — ✅ done.
3. Delete the N++ ABI `#include`s and the `__WXMSW__` N++ host from the core. — ✅ done: `src/main.cpp`
   has no `NPPM_*`/`NppData`/`FuncItem` code left, only nominative comments explaining the bridge handoff.
4. **Relicense the core permissively** (the FUTURE_PLANS gate) — **not done yet**, deliberately: this is
   the maintainer's own call per `FUTURE_PLANS.md`, not an engineering gate. `npp-bridge` stays GPL and
   ships separately (bundled into the Windows installer as `nib/npp_bridge.dll`).

**Done when:** on Windows, dropping a real N++ plugin into `plugins/` still loads it through the bridge —
menu commands, `NPPM_*`, and docking all work exactly as they do today (the in-repo TestPlugin is the
regression check) — and `git grep` finds no N++ reproduction in `src/`. — ✅ **met**: real N++ plugins
(MIME Tools, Converter) load and run through the bridge; `packages/test_plugin/` is the regression fixture
(relocated out of `src/`); `git grep` for `NPPM_`/`NppData`/`FuncItem` in `src/` returns only comments.

## Phase 4 — Package + ship per platform — mostly done

- **Windows:** ✅ done — NSIS installer (`installer/windows/wxnote.nsi`), bundles the optional
  `nib/npp_bridge.dll` + a `plugins/` folder, built in CI on every push.
- **Linux:** ✅ done — AppImage (`installer/linux/build-appimage.sh`) and `.deb`
  (`installer/linux/build-deb.sh`), both built in CI (GTK3 runtime dep).
- **macOS:** ✅ done — `.app` bundle + `.dmg` (`installer/macos/build-dmg.sh`), built in CI. Code-signing
  and notarization are still deliberately deferred ("later"), same as originally planned.
- A small **Nib plugin SDK** (`nib.h` + a template plugin + docs) — **not done.** `src/plugins/nib_test_plugin/`
  exists as an in-tree dogfooding fixture that exercises `nib.win32`/`nib.panels`/`nib.events`, but there is
  no packaged, documented template/SDK yet for third-party plugin authors to start from.

All three installers are wired into `.github/workflows/build.yml` as per-push build artifacts, and
`.github/workflows/release.yml` attaches all three to a GitHub Release on a `v*` tag push — that workflow
has never actually been run (no tags exist yet in the repo).

---

## Honest constraints (read these before committing to dates)

1. **Local verification is still Windows-only here** (no WSL/macOS on this machine). The repo is public and
   the CI matrix (`windows`/`linux`/`macos`) runs green on every push/PR, so Linux/macOS correctness is
   proven by CI — but any hands-on interactive verification (as opposed to "it built and the smoke steps in
   CI passed") still only happens on Windows.
2. **N++ binary-compat is inherently Windows-only.** Don't promise N++-`.dll` loading on Linux/macOS — it
   can't exist there. Those users get native Nib plugins.
3. **Phase 3 depended on Phase 2** — the bridge could not leave the core until Nib could carry HWNDs,
   events, and panels across the boundary. Both are now done.
4. **Licensing:** the core becomes permissive only *after* the maintainer makes the deliberate relicense
   call described in `FUTURE_PLANS.md`. The engineering gates that call was waiting on are now all cleared
   (N++ code removed from the core in Phase 3; the clean-room audit of `src/` is done — see
   `docs/LICENSE_AUDIT.md`); what's left is genuinely just the decision itself, plus the fact that a
   counsel review of the ABI-reproduction approach has not happened (nice-to-have, not blocking per
   `FUTURE_PLANS.md`). `npp-bridge` stays GPL regardless.

## Status: Phases 1–3 done, Phase 4 mostly done

All of the engineering work this plan called out is now complete or close to it:

- **Phase 1** (cross-platform core, CI matrix green on all three OSes) — ✅ done.
- **Phase 2** (Nib API surface for plugins + the bridge) — ✅ done, except the never-needed
  `nib.notifications` interface.
- **Phase 3** (N++ host carved into the GPL `npp-bridge`, core has zero N++-derived code) — ✅ done.
- **Phase 4** (packaging) — ✅ done for all three OSes and wired into CI; still open: macOS code-signing/
  notarization, a documented Nib plugin SDK/template, and actually cutting a tagged release (the `release.yml`
  workflow exists but has never run — no git tags exist yet).

What remains is no longer "structure this cross-platform" work — it's the maintainer's relicense decision
(see `FUTURE_PLANS.md`), the Nib plugin SDK, and eventually cutting a first tagged release.
