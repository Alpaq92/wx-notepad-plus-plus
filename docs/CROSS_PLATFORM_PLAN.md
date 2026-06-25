# Plan — cross-platform wxNotepad++ with Windows binary-compatibility for Notepad++ plugins

How to make wxNotepad++ run natively on Windows, Linux, and macOS, while **on Windows** it can still
load existing **Notepad++ plugin binaries (`.dll`) unchanged**.

## The idea in one picture

```
                       ┌─────────────────────────────────────────────┐
   all platforms ──▶   │  wxNotepad++ CORE  (wx + wxStyledTextCtrl)  │   ◀── permissive (Apache)
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

- **Done:** the wx core, 28 permissive themes, the **Nib API Part 1** (cross-platform, verified on
  Windows), and the **N++ host working on Windows** — but that host still lives *inside* the core.
- **Windows-only code:** **37 `__WXMSW__` guards** in `src/main.cpp` — the N++ host (`loadPlugins`,
  `handleNppm`, `FrameSubclassProc`, `SciHwndProc`), the DWM dark title bar (`setTitleBarDark` /
  `DwmSetWindowAttribute`), native scrollbar theming (`SetWindowTheme`), and the custom size-grip
  (`SizeGripWin`). `comctl32/uxtheme/dwmapi` are linked only `if(WIN32)`.
- **Not done:** Linux/macOS are *structured* but **not actually built/verified**; the N++ host is still
  in the core; the Nib API is minimal; CI exists (`.github/workflows/build.yml`) but doesn't run yet.

---

## Phase 1 — Make the core genuinely cross-platform (and prove it)

**Goal:** editor + themes + Nib plugins build and run on Windows, Linux, and macOS.

1. **Run the CI matrix.** `build.yml` *already* has the `windows` / `ubuntu` / `macos` matrix, with
   wxWidgets-build caching and GTK3 install — it just needs to actually run green (and now also triggers
   on `include/**`). **Gate:** free GitHub Actions needs the repo **public** (or self-hosted runners);
   that is the long-standing blocker for verifying non-Windows, and the heavy from-source build consumes
   Actions minutes on a private repo (macOS counts 10x).
2. **Fix per-platform build errors.** wxWidgets is portable and the `__WXMSW__` blocks compile out, so
   expect mostly include-path / link / `wxStandardPaths` issues, plus a few GTK3 dev-package needs.
3. **Audit the 37 `__WXMSW__` blocks** and classify each: (a) *graceful-degrade* — the system already
   handles it on Linux/macOS (dark title bar, native scrollbars, resize grip); or (b) *needs a portable
   path*. The N++ host blocks are out of scope here (Phase 3).
4. **Verify per OS:** launch, open a file (highlighting), apply a theme, load `nib_test_plugin`, run its
   command.

**Done when:** CI is green on all three OSes and the Nib smoke test passes on each.

## Phase 2 — Grow the Nib API to what plugins (and the bridge) need

**Goal:** enough Nib surface that real plugins *and* the N++ bridge can be built. All additive, all portable.

- `nib.events` — typed events (text-changed, doc opened/saved/closed, selection-changed).
- `nib.documents` — the open-document set + lifecycle.
- `nib.panels` — dockable panels, backed by the existing cross-platform `wxAuiManager`.
- `nib.notifications` — toasts / dialogs / input.
- `nib.win32` — **a Windows-only capability** that hands a plugin the native `HWND`s + a message-hook
  channel. This is the escape hatch `npp-bridge` needs to reconstruct the `NppData` world; on Linux/macOS
  the capability simply isn't offered.

Dogfood each new interface by extending `nib_test_plugin`.

**Done when:** the reference plugin exercises events + a panel, and `nib.win32` exposes the editor HWNDs
on Windows (a prerequisite for Phase 3).

## Phase 3 — Carve the N++ host into the GPL bridge (Windows binary-compat, isolated)

**Goal:** the core ends with **zero** N++-derived code, while Windows N++ binary-compat is preserved.

1. Create `packages/npp-bridge/` — a GPL, Windows-only build target, and **move** `include/npp-compat/`
   plus the N++ host out of `src/main.cpp` (`loadPlugins`, `buildPluginsMenu`, `handleNppm`,
   `FrameSubclassProc`, `SciHwndProc`, `m_npp`/`m_plugins`/`LoadedPlugin`, `pluginsMenuHandle`).
2. Restructure it as a Windows-only **Nib plugin**: on `activate`, it uses `nib.win32` to get the editor
   HWNDs, builds `NppData`, installs the `WM_USER+1000` router, scans `plugins/`, `LoadLibrary`s the N++
   DLLs, and translates **NPPM_\* ⇄ Nib**, **SCNotification ⇄ nib.events**, **FuncItem → nib.commands/menu**,
   **NPPM_DMMREGASDCKDLG → nib.panels**.
3. Delete the N++ ABI `#include`s and the `__WXMSW__` N++ host from the core.
4. **Relicense the core permissively** (the FUTURE_PLANS gate); `npp-bridge` stays GPL and ships separately.

**Done when:** on Windows, dropping a real N++ plugin into `plugins/` still loads it through the bridge —
menu commands, `NPPM_*`, and docking all work exactly as they do today (the in-repo TestPlugin is the
regression check) — and `git grep` finds no N++ reproduction in `src/`.

## Phase 4 — Package + ship per platform

- **Windows:** portable zip + installer; bundle the optional `npp-bridge` + a `plugins/` folder.
- **Linux:** AppImage and/or `.deb` (GTK3 runtime dep).
- **macOS:** `.app` bundle / `.dmg` (code-sign + notarize later).
- A small **Nib plugin SDK**: `nib.h` + a template plugin + docs.

---

## Honest constraints (read these before committing to dates)

1. **Local verification is Windows-only here** (no WSL/macOS on this machine). Linux/macOS correctness is
   proven by **CI**, which needs the repo public (free Actions) or self-hosted runners. Until that runs,
   non-Windows is "structured, not verified."
2. **N++ binary-compat is inherently Windows-only.** Don't promise N++-`.dll` loading on Linux/macOS — it
   can't exist there. Those users get native Nib plugins.
3. **Phase 3 depends on Phase 2** — the bridge can't leave the core until Nib can carry HWNDs, events,
   and panels across the boundary.
4. **Licensing:** the core becomes permissive only *after* Phase 3 removes the N++ code; `npp-bridge` is
   GPL; and the remaining non-engineering gates from `FUTURE_PLANS.md` still apply (counsel review of the
   ABI approach; a clean-room audit of `src/`).

## Suggested order + the first concrete step

- Priority "ship on Linux/macOS soon" → **Phase 1 first**.
- Priority "clean permissive core" → **Phase 2 → 3**.
- Either way, the **first concrete step is the CI matrix (Phase 1.1)** — once every change is checked on
  all three OSes, every later phase rests on solid ground. That step is gated on making the repo public
  (or wiring self-hosted runners).
