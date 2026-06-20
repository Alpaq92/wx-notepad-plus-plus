# Notepad++ → wxWidgets Migration Plan

**Document owner:** Lead Architect
**Date:** 2026-06-20
**Status:** Final — for funding decision
**Scope:** 1:1 visual/behavioral migration of Notepad++ (~182K LOC native Win32 C++) to wxWidgets, preserving the HWND-based plugin ABI

> **What changed since the draft (read this first).** This revision corrects four verified factual errors in the prior draft, fills the single largest scoping gap, and hardens the risk/effort sections:
> 1. **DPI model corrected.** The app is **System-DPI-aware with an *unaware* fallback** (manifest: `dpiAware=true`, `dpiAwareness="system, unaware"`), **not** per-monitor. wxWidgets 3.2/3.3 default to **per-monitor-v2** awareness, so adopting wx *changes* the DPI model rather than merely "reconciling an owner." This is now treated as a behavioral-change risk (§9, §13 R5).
> 2. **Internal message coupling added as a first-class cost.** The app's *own* UI is HWND-message-driven: **~1,215 `SendMessage`/`PostMessage` call sites across 74 files** (verified). Converting any leaf control to a wx object breaks internal callers, not just plugins. "Leaf-first is low-risk" is therefore only *partly* true; the effort ceiling is widened accordingly (§5.6, §11, §14).
> 3. **Plugin ABI surface corrected.** There are **seven** plugin entry points, including **`messageProc` (`PMESSAGEPROC`)** and the **inter-plugin `NPPM_MSGTOPLUGIN`** relay (`relayPluginMessages`), which the draft omitted. The shim must preserve these too (§5).
> 4. **Several "verified" claims fixed:** the main `WndProc` is bound via `CREATESTRUCT.lpCreateParams` (not `GWLP_USERDATA`); `isDlgsMsg` runs **three ordered accelerator pre-passes before** the modeless loop; the winmain order is `isDlgsMsg → TranslateAccelerator → DispatchMessage`; internal `execute()` rides a **cached direct function pointer**, independent of the plugin `SendMessage` path. Source paths corrected (`PluginInterface.h` is under `MISC/PluginsManager/`, `NppDarkMode.cpp` at `src/` root).
>
> The draft's central thesis, recommendation (Path A), and Scintilla decision are **retained and reinforced** by the deeper evidence.

---

## 1. Executive Summary & Feasibility Verdict

### 1.1 The blunt verdict

A literal "1:1 migration to wxWidgets" of Notepad++ **is not achievable in the sense most people mean it**, and you should hear that clearly before any budget is committed. Here is the honest decomposition:

| What "1:1" might mean | Achievable? | Reality |
|---|---|---|
| **Pixel-identical UI on Windows** | Partially (~85–95%) | Achievable for most dialogs with meticulous `wxSizer` work + custom `wxDC` painting. Owner-drawn tabs, BabyGrid, ReBar, status-bar owner-draw, and dark-mode menu rendering will drift 5–15%. **wx-rendered text will not be byte-identical to GDI/ClearType text even when "correct" (§12).** |
| **Identical behavior on Windows** (keyboard routing, macro timing, find/replace sequencing) | Partially | The Win32 loop has strict ordering (`isDlgsMsg` → `TranslateAccelerator` → `DispatchMessage`), and `isDlgsMsg` *itself* runs three accelerator pre-passes (§3.1). wxWidgets' event model differs; subtle regressions are likely and must be hunted. |
| **DPI behavior identical** | **No** | The app is **System-DPI-aware** today; wx defaults to **per-monitor-v2**. Adopting wx *introduces* per-monitor reflow/redraw behavior that does not exist now (§9). |
| **Existing compiled plugins keep working unmodified** | **Only on Windows AND with a heavyweight HWND shim** | The plugin ABI *is* Win32. See §1.2. |
| **Cross-platform (macOS/Linux) with the same plugins** | **No** | The plugin ABI cannot be honored off-Windows. Cross-platform means a *new* plugin ABI and an ecosystem split. |
| **A clean, simpler, "modern" codebase as a side effect** | **No** | To preserve fidelity + ABI you must *add* a shim layer on top of largely-retained logic **and** carry dual HWND/wx identity on internal controls during transition. The codebase gets bigger and more complex before it gets smaller — and on a Windows-only target it may never get smaller. |

### 1.2 The plugin-ABI reality (the crux)

This is the single fact that governs the entire project. **The Notepad++ plugin ABI is not "defined in Win32 terms" — it literally *is* the Win32 message architecture.** Verified directly in source (`MISC/PluginsManager/PluginInterface.h`):

- `struct NppData { HWND _nppHandle; HWND _scintillaMainHandle; HWND _scintillaSecondHandle; }` is passed **by value** to each plugin's `__cdecl setInfo(NppData)`. Plugins **cache these three HWNDs for their entire lifetime**.
- Plugins drive Notepad++ via `SendMessage(nppData._nppHandle, NPPM_*, …)`. There are **212 `case NPPM_*` handlers** in `NppBigSwitch.cpp` (verified count), all assuming **synchronous** `SendMessage` return semantics. (Note: 212 is the number of *case labels*; the number of *distinct* `NPPM_*`/`NPPN_*` messages defined in `Notepad_plus_msgs.h` is ~118 — some single messages carry large sub-protocols.)
- Plugins read/write the editor via `SendMessage(nppData._scintillaMainHandle, SCI_*, …)` against a **real Scintilla HWND**.
- **Seven plugin entry points** are exported, not six (verified `PluginInterface.h` lines 65–72): `setInfo`, `getName`, `getFuncsArray`, `beNotified`, **`messageProc` (`PMESSAGEPROC`)**, `isUnicode` — plus the `getName` typedef. `messageProc` powers **inter-plugin messaging** via `NPPM_MSGTOPLUGIN` → `PluginsManager::relayPluginMessages` → each plugin's cached `_pMessageProc` (verified `NppBigSwitch.cpp:3133`, `PluginsManager.cpp:766`). The shim must preserve this too.
- Dockable plugin panels pass a raw `HWND hClient` inside `DockedWidgetData` (verified `Docking.h`; `DockingDlgInterface.h:55` sets `data->hClient = _hSelf`) via `NPPM_DMMREGASDCKDLG`, and that HWND must stay valid and discoverable via `NPPM_DMMGETPLUGINHWNDBYNAME`.
- Plugins receive `beNotified(SCNotification*)` callbacks whose `nmhdr.hwndFrom` is an HWND.
- External tools/launchers/installers discover the app via `FindWindow(L"Notepad++", …)` (verified `winmain.cpp:736,740`) — the **registered window-class name** is itself an ABI surface (§5.7).

**Consequence:** the moment `Notepad_plus_Window` stops being a top-level `HWND` with a classic `WndProc`, every existing plugin breaks — silently or with a crash. wxWidgets `wxFrame` *does* own a native HWND on Windows (`wxWindow::GetHandle()`), but it is owned/managed by wxWidgets, has wxWidgets' own window-class and `WndProc`, and does not implement the `NPPM_*` protocol. So "keep plugins working" reduces to: **re-implement the entire Win32 message ABI on top of wxWidgets** (a real subclassed/owned HWND that decodes 212 cases + 7 entry points and forwards to wx). That is the dominant cost and risk of this project, not the dialogs.

### 1.3 The internal-coupling reality (the second crux)

The draft scoped migration cost as "212 plugin handlers + 69 dialogs." That undercounts. **The app's own UI is wired with the same HWND-message mechanism as the plugin ABI:** **~1,215 `SendMessage`/`PostMessage` call sites across 74 files** (verified by grep), driving custom controls via `TCM_*`, `BGM_*`, `SBM_*`/`SB_*`, `TVM_*`, and bespoke `RegisterWindowMessage`/`NPPM_INTERNAL_*` messages (e.g., `preferenceDlg.cpp` ~175 sites, `FindReplaceDlg.cpp` ~103, `Notepad_plus.cpp` ~97, `TabBar.cpp` ~49, `BabyGrid.cpp` ~63, `DockingCont.cpp` ~54).

The architectural implication is decisive: **the leaves are coupled to the core by the same message mechanism as the plugins.** When a leaf control (a tab bar, a grid, a status bar) becomes a wx object, every internal caller that did `SendMessage(hTabBar, TCM_*, …)` breaks. Each leaf conversion therefore drags a chunk of core refactoring (or a dual HWND/wx identity) with it. This is why "leaf-first is low-risk/isolated" is *partly an illusion* and why the effort ceiling in §14 is wider than the draft's.

### 1.4 What we recommend (preview of §15)

Adopt a **Windows-only, hybrid, incremental migration with a mandatory HWND/ABI compatibility shim**, sequenced **leaf-first, ABI-core last**, and **keep Scintilla as a native HWND** (do *not* adopt `wxStyledTextCtrl`'s bundled Scintilla). Treat cross-platform as an explicitly deferred, separate program that requires a **plugin ABI v2** and an accepted ecosystem split. **Measure internal-message coupling per subsystem in Phase 0** and re-baseline effort on the result. Be prepared, after an honest options review, to conclude that **not migrating the core message loop at all** (Option C / the wrap-only terminal state) is the rational stopping point.

---

## 2. Goals & Non-Goals

### 2.1 Goals

1. **G1 — Visual fidelity on Windows:** match the current UI to within a small, agreed pixel tolerance (target ≤3% screenshot diff, with an explicit text-rendering carve-out per §12) across the supported DPI scales and light/dark modes.
2. **G2 — Behavioral fidelity on Windows:** preserve editing, search/replace, macros, session/document management, docking, accelerator routing, and keyboard semantics — including message ordering.
3. **G3 — Plugin ABI preservation:** existing compiled plugins (the long tail in the wild) continue to load and function **without recompilation** on Windows, including docking, menus, toolbar buttons, plugin Scintilla, and **inter-plugin messaging**.
4. **G4 — Shippability at every phase:** the app must build, run, and pass regression at the end of each phase; no multi-month "big bang" branch.
5. **G5 — Architectural optionality:** leave the door open (not necessarily walk through it) to a future cross-platform effort.

### 2.2 Non-Goals

1. **NG1 — Feature changes / redesign.** This is a port, not a product revision.
2. **NG2 — Cross-platform delivery in this program.** macOS/Linux is out of scope here (see §5.5 and §14).
3. **NG3 — Replacing Scintilla with `wxStyledTextCtrl`'s bundled engine.** Rejected for ABI + Boost.Regex + version-pinning reasons (§6).
4. **NG4 — Rewriting plugin-facing semantics** (no async-by-default `NPPM_*`; we preserve synchronous behavior and ordering).
5. **NG5 — A "cleaner codebase" as a deliverable.** Net LOC will rise during migration.
6. **NG6 — Changing the DPI-awareness model as a feature.** Any move from System to per-monitor awareness is an unavoidable *side effect* of wx to be contained and tested, not a goal (§9).

---

## 3. Current Architecture (Evidence-Based)

Notepad++ is a single-process, single-UI-thread Win32 application. Everything funnels through one message loop and one giant message processor.

### 3.1 The spine

- **Entry / message loop** — `winmain.cpp` (893 LOC): `wWinMain` sets up `Win32Exception` + `MiniDumper`, registers the `"Notepad++"` window class (used by plugins/installers via `FindWindow`, verified lines 736/740), creates `Notepad_plus_Window`, then runs the classic loop. **Verified order (lines 830–835):**
  ```
  while (GetMessage(&msg)) {
      if (!notepad_plus_plus.isDlgsMsg(&msg))          // (1) accel pre-passes + modeless
          if (TranslateAccelerator(hSelf, accTable, &msg) == 0)  // (2) global HACCEL
              DispatchMessageW(&msg);                  // (3) normal dispatch
  }
  ```
  It also handles `WM_COPYDATA` to forward files to an existing instance, and `ChangeWindowMessageFilterEx` for UAC.
- **Main window** — `Notepad_plus_Window.cpp/.h`: static `WndProc` (`Notepad_plus_Proc`) bound by passing `this` as `CreateWindowEx` **`lpCreateParams`** and retrieving it from **`CREATESTRUCT.lpCreateParams`** in `WM_NCCREATE`/`WM_CREATE` (verified lines 99–109) — *not* via `GWLP_USERDATA`. The frame is created with `WS_EX_ACCEPTFILES` (native file drop) and optional `WS_EX_LAYOUTRTL` (verified line 100). `WM_CREATE` calls `Notepad_plus::init(hwnd)`.
- **`isDlgsMsg` is not just a modeless loop** — verified (`Notepad_plus_Window.cpp:446–463`): it runs **three ordered accelerator pre-passes** — `processTabSwitchAccel`, `processIncrFindAccel`, `processFindAccel` — **before** iterating `_hModelessDlgs` and calling `IsDialogMessageW`. Reproducing this precedence under wx's own accelerator/dialog-navigation processing is a known-hard ordering problem (§5.4, §13 R2).
- **Core processor** — `Notepad_plus.cpp` (9,334 LOC) + `NppBigSwitch.cpp` (4,324 LOC, **212 `NPPM_*` cases**) + `NppCommands.cpp` (4,643 LOC, `WM_COMMAND`/`IDM_*`) + `NppNotification.cpp` (Scintilla `SCN_*`) + `NppIO.cpp` (2,987 LOC). This is the hub every subsystem touches.

### 3.2 Win32 coupling — the numbers that matter

| Coupling vector | Magnitude | Source |
|---|---|---|
| **Internal `SendMessage`/`PostMessage` call sites** | **~1,215 across 74 files** | grep (verified) — the internal-coupling crux |
| `NPPM_*` message handlers (plugin ABI surface) | **212 cases** | `NppBigSwitch.cpp` (verified) |
| Distinct `NPPM_*`/`NPPN_*` defined | ~118 messages / 1,288 LOC | `Notepad_plus_msgs.h` |
| Plugin entry points (C exports) | **7** (incl. `messageProc`/inter-plugin) | `PluginInterface.h` (verified) |
| Internal editor calls via cached direct pointer (`execute()`) | ~524 in editor view alone | `ScintillaEditView.cpp` — **independent of HWND path** (cached `_pScintillaFunc`, lines 379–380) |
| HWNDs exposed to plugins in `NppData` | 3 (nppHandle + 2 Scintilla) | `PluginInterface.h` (verified) |
| `GetDlgItem`/`SetDlgItemText`/`GetDlgItemText` call sites | ~728 | across `.cpp` |
| DIALOGEX templates | **69** (~838 control statements, 26 `.rc` files, 3,475 LOC) | resources |
| Dark-mode subclass / dark-scrollbar sites | **~25** call sites, multiple distinct subclass procs | `NppDarkMode.cpp` (verified `src/` root) |
| `uxtheme.dll` ordinal hooking + IAT hook (scrollbars) | yes | `DarkMode/DarkMode.cpp`, `DarkMode/IatHook.h` (verified) |
| Custom docking via HWND subclassing | 4 fixed zones, ~6,087 LOC framework + ~12,452 LOC panels | `DockingWnd/*` |
| DPI model | **System-aware, unaware fallback** (NOT per-monitor) | `notepad++.exe.manifest:49–50` (verified) |

### 3.3 Subsystem map (LOC by area, from inventory)

| Area | Approx LOC |
|---|---|
| Main window + message loop + IPC + ABI dispatch | 19,657 |
| Scintilla hosting + editor view + buffer + printer | 20,979 |
| Preferences / Shortcut Mapper / base dialogs | 19,572 |
| Find/Replace / UDL / Autocompletion dialogs | 14,648 |
| Dockable panels (built-in 8 + infra) | 12,452 |
| Common controls & Win32 utilities | 12,200 |
| Dark mode / theming / DPI | 6,328 |
| Docking & splitter framework | 6,087 |
| TabBar / ToolBar / StatusBar / icons | 4,024 |
| Resources / dialog templates / localization | 3,475 |
| Plugin subsystem & ABI | 2,428 |
| Build / dependency integration | ~200 |

*(Areas overlap — e.g., docking framework vs. dockable panels — so these do not sum cleanly to 182K; treat as relative weight, not an additive total.)*

### 3.4 Architectural characteristics that fight migration

1. **One synchronous UI thread, one message loop.** `SendMessage` is in-order and blocking; the entire app and all plugins assume it. Plugins call back **re-entrantly** from inside `beNotified` and **from worker threads**; internal subsystems (ReadDirectoryChanges/FileBrowser watch, FunctionList parsing) marshal to the UI via `PostMessage`/`SendMessage` (§5.8).
2. **Resources compiled into the binary** (`.rc` → IDs in `resource.h`/`menuCmdID.h`) and consumed by plugins (e.g., `ID_PLUGINS_CMD` 22000–22999, `MARKER_PLUGINS`, `INDICATOR_PLUGINS`).
3. **Pervasive `HWND` as identity** — for **both** plugins and internal callers. Buffers, views, tabs, panels, dialogs, menus — all are HWND/HMENU at the seams.
4. **Native theming hooks** that have no wx equivalent (uxtheme ordinals, UAH menu drawing, IAT scrollbar patch).
5. **System-DPI awareness** with manual scaling on top (`dpiManagerV2`), which wx's per-monitor default will perturb.

---

## 4. Target wxWidgets Architecture

The target is **not** "pure wxWidgets." It is **wxWidgets-shell + native Win32 core preserved behind a shim**, converging UI components to wx leaf-by-leaf — while carrying **dual HWND/wx identity** on internal controls during their transition.

```
┌──────────────────────────────────────────────────────────────────────┐
│ wxApp (replaces wWinMain bootstrap; wraps GetMessage loop)             │
│  • retains Win32Exception + MiniDumper integration                     │
│  • overrides FilterEvent / MSWTranslateMessage to reproduce            │
│    isDlgsMsg's 3 accel pre-passes + modeless loop BEFORE wx accel      │
│  • reconciles DPI-awareness ownership (manifest vs wx; see §9)         │
├──────────────────────────────────────────────────────────────────────┤
│ NppHostFrame : wxFrame                                                  │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │ ABI Compatibility Layer (the crux — §5)                          │   │
│  │  • Real HWND exposed as nppData._nppHandle (subclassed wxFrame   │   │
│  │    HWND) with a WndProc that decodes 212 NPPM_* + WM_COPYDATA +  │   │
│  │    WM_TASKBARCREATED and forwards to wx core (SYNCHRONOUS)       │   │
│  │  • Inter-plugin messageProc relay (NPPM_MSGTOPLUGIN) preserved   │   │
│  │  • HWND⇄wxObject registries: panels, plugin Scintilla, menus     │   │
│  │  • isDlgsMsg-equivalent (3 accel pre-passes + modeless tracking) │   │
│  │  • runtime HACCEL coexistence with wxAcceleratorTable (§5.9)     │   │
│  │  • thread→UI marshaling bridge (PostMessage ⇄ CallAfter) (§5.8)  │   │
│  └────────────────────────────────────────────────────────────────┘   │
│  • Docking: PRIMARY = retain native DockingManager; wxAUI = R&D (§9)   │
│  • wxAuiNotebook / custom (DocTabView) — dual HWND identity            │
│  • wxScintillaCompat (native Scintilla HWND hosted in a wxWindow) (§6) │
│  • wxMenuBar/wxMenu (+ native HMENU bridge for plugins) (§5.4)         │
│  • wxToolBar(+ReBar surrogate), wxStatusBar (owner-draw via wxDC)      │
│  • wx dialogs (69 ports) — wxDialog/wxPanel + wxSizer                  │
├──────────────────────────────────────────────────────────────────────┤
│ Notepad_plus core (LARGELY RETAINED)                                    │
│  • process()/command dispatch refactored to be callable from both      │
│    the shim WndProc and wx event handlers                              │
│  • Internal SendMessage(control,…) sites rewired to dual identity      │
│  • Buffer/FileManager, Parameters, NativeLangSpeaker unchanged         │
├──────────────────────────────────────────────────────────────────────┤
│ Vendored libscintilla.lib + liblexilla.lib + boostregex (UNCHANGED)    │
│  • internal execute() keeps using cached SCI_GETDIRECTFUNCTION ptr     │
└──────────────────────────────────────────────────────────────────────┘
```

**Key design decisions baked into the target:**

- **D1 (now a *research bet*, not settled).** The plugin-facing `HWND` is a *first-class, stable, long-lived* handle exposed as the subclassed `wxFrame` HWND. **This is the project's largest unproven technical bet** and is treated as such (§5.2, Phase-0 exit criteria, §13 R1b): `wxWindow::MSWWindowProc` is deeply involved in sizing/painting/focus/erase-background/DPI and assumes it owns the frame's message stream; inserting our subclass ahead of it without disturbing wx's handling is delicate and **version-fragile across wx 3.0/3.2/3.3**.
- **D2.** `Notepad_plus::process()` becomes **dual-callable**: a thin Win32 `WndProc` path (for plugins/legacy messages) and direct C++ calls (for wx-native UI events). Both reach the same handlers; we preserve synchronous semantics and ordering.
- **D3.** Scintilla stays a **native HWND** (`wxScintillaCompat`), so `_scintillaMainHandle`/`_scintillaSecondHandle` remain real and plugin `SCI_*` calls work unchanged. Internal `execute()` keeps its cached direct pointer (no message round-trip) — so internal editor performance is **unaffected** by the wx shell.
- **D4.** Dark mode/DPI stay **largely native** on Windows (NppDarkMode retained and applied to underlying HWNDs) — **but only for controls still backed by native common controls.** Once a control becomes a wx-drawn control, NppDarkMode's per-class subclassing no longer applies and dark mode must be reimplemented in wx for that control (§9, §13 R5). "Retain native dark mode" and "convert controls to wx" are in **direct tension**; you cannot fully have both.

This is deliberately **not** the "clean" wx architecture that the inventory's optimistic `wxTargetClasses` columns imply. Those columns describe the *destination if you were rewriting for cross-platform*; they are not compatible with G3 (ABI preservation) on a realistic budget.

---

## 5. The Plugin-ABI Compatibility Strategy (THE CRUX)

### 5.1 Statement of the problem

Four things must remain simultaneously true for existing plugins:

1. `nppData._nppHandle` is a valid HWND that responds **synchronously** to 212 `NPPM_*` messages with correct `LRESULT`s, and supports `WM_COPYDATA`/`WM_TASKBARCREATED`/`FindWindow` discovery.
2. `nppData._scintillaMainHandle` / `_scintillaSecondHandle` are valid Scintilla HWNDs that respond to `SCI_*`.
3. Plugin dialogs/panels registered with raw HWNDs (`NPPM_MODELESSDIALOG`, `NPPM_DMMREGASDCKDLG`) keep working for keyboard routing, show/hide, dock/float, and `NPPM_DMMGETPLUGINHWNDBYNAME` lookups; plugin menus (`NPPM_GETMENUHANDLE` → `HMENU`) and toolbar buttons (`registerDynBtn`, `HBITMAP`/`HICON`) keep working.
4. **Inter-plugin messaging** (`messageProc`/`PMESSAGEPROC` via `NPPM_MSGTOPLUGIN`) keeps routing to the correct destination plugin's cached `_pMessageProc`.

### 5.2 The HWND-shim approach (recommended — but Phase-0-gated)

**Tier 1 — The `_nppHandle` boundary.** Keep a real, stable HWND as `_nppHandle`. Two implementation options were considered:

- **(a) Subclass the `wxFrame` HWND.** Use `wxWindow`'s native handle + `SetWindowSubclass` so our procedure sees messages first, handles `NPPM_*`/`WM_COPYDATA`/`WM_TASKBARCREATED`, and chains the rest to wx. **Pro:** `_nppHandle` == the visible frame, matching plugins that reparent dialogs onto it, query its client rect, enumerate children, and find it via `FindWindow`. **Con:** tied to wx frame lifecycle and wx's own message-handling order; **fragile across wx versions.**
- **(b) Dedicated native window as `_nppHandle`.** Decouples ABI from wx; cleanest lifetime control. **Con:** plugins that treat `_nppHandle` as a real visible window break.

**Recommendation: (a), but explicitly as a *research bet to be proven or killed in Phase 0*** — because the inventory shows plugins do treat `_nppHandle` as the main visible window (docking reparents, `GetMenuHandle`, child enumeration, `FindWindow` discovery). A dedicated message-only window fails those. **Honest caveat (corrected from draft):** even a `wxFrame`'s HWND is owned by wx and *can* be recreated/wrapped on some reparent or extended-style-change paths, and wx may create helper/companion HWNDs; the stability of `GetHandle()` across the app's lifetime is **not guaranteed by the wx contract** and must be empirically validated. Mitigation if (a) proves unstable: fall back to (b) plus a small "frame-find" shim that answers `FindWindow` and reparent requests.

The shim WndProc decodes each `NPPM_*` and calls into `Notepad_plus` **directly (synchronous)**, returning the real `LRESULT`. **Because we call C++ handlers inline (not via posted wx events), synchronous semantics and ordering are preserved** — this is the critical correctness property and the reason we do *not* naively map `NPPM_*` to async `wxEvent`s.

**Tier 2 — Translation registries.** Maintain bidirectional maps, populated/cleared deterministically at create/destroy:
- `_hwndToPanel : HWND → DockingCont/wxPanel*`
- `_hwndToScintilla : HWND → wxScintillaCompat*`
- `_hwndToMenu : HMENU → wxMenu*` (see 5.4 for the menu strategy)
- `_modelessDlgs : { HWND, wxWindow* }` for the `isDlgsMsg` equivalent.

**Tier 3 — Scintilla for plugins.** `NPPM_CREATESCINTILLAHANDLE` returns a **real** Scintilla HWND (via `wxScintillaCompat`/`ScintillaCtrls`), registered in `_hwndToScintilla`. No proxying of `SCI_*` is needed because the handle is genuinely native. This is the single biggest reason to keep native Scintilla (§6).

**Tier 4 — Modeless/keyboard routing.** Replace the `winmain.cpp` `isDlgsMsg` loop with a `wxApp::FilterEvent` + `MSWTranslateMessage` override that **reproduces the exact three-pass precedence** (`processTabSwitchAccel` → `processIncrFindAccel` → `processFindAccel` → modeless `IsDialogMessageW`) **before** wx's own accelerator handling, preserving TAB/ESC/Ctrl+C and incremental-find semantics (§5.4 hazard).

**Tier 5 — Docking ABI.** `NPPM_DMMREGASDCKDLG` unpacks `DockedWidgetData.hClient`, hosts it, and keeps `hClient` **stable across dock/float** so `NPPM_DMMGETPLUGINHWNDBYNAME` keeps returning the same handle. (Under native docking — the primary plan, §9 — this is preserved by construction; under wxAUI it is an R&D risk.)

**Tier 6 — Inter-plugin relay.** Preserve `NPPM_MSGTOPLUGIN` → `relayPluginMessages` → destination `_pMessageProc`. This rides the same shim WndProc and must be in the Phase-0 ABI smoke test.

### 5.3 Estimated achievable compatibility

With the full shim: **~90–98% of well-behaved plugins** keep working *if* D1 proves stable. The residual breakage:
- Plugins using `SCI_GETDIRECTFUNCTION` against the editor and caching the function pointer — works *because* native Scintilla is retained, but must be tested.
- Plugins that **enumerate child HWNDs** of `_nppHandle` to find toolbar/status/tab handles directly (rather than via `NPPM_GET*HANDLE`) — these see wx's child window tree, which differs. Low-but-nonzero share, and **growing** as internal controls convert to wx (their HWND class names change, e.g., `SysListView32` → wx's class).
- Plugins assuming a specific **floating-window chrome** (POPUP styles) for docked dialogs — visual, not fatal.

### 5.4 Menu & accelerator identity (was a hand-wave; now a concrete sub-plan)

**Menus.** `NPPM_GETMENUHANDLE` returns an `HMENU`; plugins insert items, set checkmarks/state, and add submenus on it, and the app uses `HMENU` + `IDM_*`/`ID_PLUGINS_CMD` (22000–22999) throughout `NppCommands.cpp`. `wxMenuBar`/`wxMenu` own an underlying `HMENU` on Windows but **rebuild/own it** and route via `wxEVT_MENU`, not `WM_COMMAND`. Decision: **keep the plugins menu (and the dynamic plugin command range) as a native `HMENU` owned by the shim**, attached to the wx menu bar's native `HMENU`, and route `WM_COMMAND` for `ID_PLUGINS_CMD`/dynamic IDs through the shim WndProc into `runPluginCommand`. wx-native menus are used only for the fully-ported, non-plugin menus. This avoids reimplementing dynamic plugin menu manipulation in wx and keeps `WM_COMMAND` semantics for plugin commands.

**Runtime accelerators (distinct from the Shortcut Mapper *dialog*).** The Shortcut Mapper persists user-editable accelerators (and plugin-contributed `ShortcutKey` from `FuncItem`) into a **single runtime `HACCEL`** consumed by `TranslateAccelerator` in the loop (verified `getAccTable()`/`winmain.cpp:832`). Decision: **retain the runtime `HACCEL` + `TranslateAccelerator`** in the shim's translate path (so plugin/user shortcuts keep working identically), and **do not** scatter shortcuts into per-window `wxAcceleratorTable`s. wx's own accelerator routing must be suppressed/ordered so it does not double-handle or swallow keys. This coexistence (native `HACCEL` first, wx accel disabled for these keys) is a behavioral-1:1 hazard with its own test matrix (§12).

### 5.5 The "Windows-only vs. break-ABI" decision

| | **Path A: Preserve ABI (Windows-only)** | **Path B: Break ABI (enables cross-platform)** |
|---|---|---|
| Plugins in the wild | Keep working (~90–98%, if D1 holds) | **All break** until recompiled against a new wx-native API |
| Cross-platform | Not delivered (deferred) | Possible (eventually) |
| Engineering cost | Very high (shim + internal rewiring is the bulk) | Very high (new API + porting + ecosystem shepherding) + plugin-ecosystem fallout |
| User perception | Continuity | "Notepad++ broke my plugins" |
| Honest framing | A faithful Windows port | A new product ("Notepad++ 2.0") |

**Recommendation: Path A (Windows-only, ABI preserved).** Path B's value (cross-platform) is real but cannot be obtained *and* keep the existing plugin base; choosing B is choosing to relaunch the ecosystem. A pragmatic **hybrid sub-option** within Path A: ship the ABI shim *and* publish an opt-in **wx-native plugin API v2** (new `NPPM_GET*OBJECT` messages returning `wxWindow*`/`wxStyledTextCtrl*`) so new plugins can be portable later — extra cost, but a runway without breaking anyone today.

### 5.6 Internal-message coupling strategy (NEW — the second crux)

Because ~1,215 internal `SendMessage`/`PostMessage` sites bind the core to its own controls, each leaf conversion needs a deliberate strategy:

- **Dual identity during transition.** A converted control exposes both a wx object *and* a stable native HWND (Scintilla already does this; tab bar, status bar, grids must too) so existing internal `SendMessage(hControl, X_*, …)` keep working until their call sites are migrated.
- **Per-subsystem rewiring.** Replace `SendMessage(hControl, CUSTOM_MSG, …)` with direct C++ method calls on the wx object, subsystem by subsystem, gated by tests. Custom `RegisterWindowMessage`/`NPPM_INTERNAL_*` flows (95 `RegisterWindowMessage`/`hwndFrom` sites across 15 files, verified) are inventoried and migrated explicitly.
- **Phase-0 metric.** Phase 0 must **measure coupling per subsystem** (count of inbound `SendMessage` sites per control HWND) to size each leaf realistically. This metric feeds the §14 re-baseline.

This work is **not captured** by per-dialog hour estimates; it is budgeted separately in §11/§14.

### 5.7 External-discovery contract

Keep the registered window-class name `"Notepad++"` and a discoverable top-level HWND so `FindWindow(L"Notepad++", …)` (launcher, external tools, installers — verified `winmain.cpp:736,740`) and the `NPPM_INTERNAL_RESTOREFROMMINIMIZED` handshake keep working. Under D1(a) this is automatic; under D1(b) the dedicated `_nppHandle` window must register that class.

### 5.8 Plugin/thread reentrancy under wx modal loops (NEW)

`wxDialog::ShowModal` runs a **nested wx event loop** that disables top-level windows and pumps events differently than a raw `DispatchMessage` loop. Two real scenarios the draft never analyzed:

1. **A plugin sends `NPPM_*` re-entrantly** (from `beNotified`, or while a wx modal is active). Because the shim dispatches inline/synchronously, an `NPPM_*` arriving during a modal loop executes a core handler **while a nested loop is on the stack** — risk of unexpected reentrancy/reordering, or of touching disabled windows. Mitigation: classify `NPPM_*` handlers as modal-safe vs. modal-defer; queue the unsafe ones; add reentrancy guards; test with a plugin that hammers `NPPM_*` during a modal dialog.
2. **A plugin or worker thread `PostMessage`/`SendMessage`s to `_nppHandle`** from a non-UI thread. Internal worker threads (ReadDirectoryChanges/FileBrowser watch, FunctionList parsing) already marshal via `PostMessage`. Under wx the idiom is `wxQueueEvent`/`CallAfter`. Decision: **keep `PostMessage` to the native `_nppHandle`** for ABI (the shim drains them in order) **and** provide an internal `PostMessage⇄CallAfter` bridge so wx-side and Win32-side cross-thread notifications preserve identical ordering. A cross-thread `SendMessage` from a plugin must not deadlock against the wx loop — verified in Phase 0.

### 5.9 Drag-and-drop, clipboard, IME (NEW)

- **DnD.** The frame uses `WS_EX_ACCEPTFILES` for file drop (verified line 100), and the embedded native Scintilla has its **own OLE drag/drop**. Hosting native Scintilla inside a `wxWindow` means **two DnD stacks coexist** (wx installs its own drop targets). Decision: do **not** attach a wx drop target to the Scintilla host (let Scintilla's OLE DnD own the editor); keep file-drop via the native `WS_EX_ACCEPTFILES`/`WM_DROPFILES` path on the shim; reconcile only where wx panels need their own drop targets. This is a common embedding-regression source and gets explicit tests.
- **Clipboard.** Editor clipboard stays Scintilla-native; wx clipboard is used only for wx-native controls. Avoid double-ownership.
- **IME.** Scintilla manages its own IME composition window; ensure wx focus/positioning of the host does not relocate or fight the composition window. Treated as a design item in Phase 6, not just a test line.

### 5.10 Cross-platform honesty

Even with a perfect shim, the shim is **100% Win32** (`HWND`, `SendMessage`, `HACCEL`, `HMENU`, OLE DnD, uxtheme). It does **not** run on macOS/Linux. Cross-platform therefore requires Path B's v2 ABI *plus* native re-implementations of dark mode, IPC (`WM_COPYDATA` → sockets/pipes), file dialogs, tray, accelerators, and DnD. Budget it as a separate multi-year effort *after* a stable Windows wx build, if at all.

---

## 6. Scintilla Hosting Decision

**Decision: Option 2 — `wxScintillaCompat`, a `wxWindow` that hosts the existing native Scintilla HWND**, built from the **vendored `libscintilla.lib` + `liblexilla.lib` + `boostregex`**. **Reject** `wxStyledTextCtrl` (Option 1). **Reject** the hybrid (Option 3).

### 6.1 Why not `wxStyledTextCtrl` (Option 1)

1. **Breaks the ABI.** `wxStyledTextCtrl` wraps Scintilla internally and does not surface a raw Scintilla HWND that answers `SendMessage(handle, SCI_*, …)` from plugins. `_scintillaMainHandle` would no longer be honorable → mass plugin breakage on the most common operation (read/write buffer text).
2. **Regex divergence.** Notepad++ builds Scintilla with Boost.Regex (`BOOST_REGEX_STANDALONE`). The bundled wxSTC Scintilla does not carry this; search semantics diverge — a behavioral 1:1 failure that also breaks plugins relying on Boost behavior.
3. **Version drift.** wxSTC pins its own Scintilla/Lexilla versions; Notepad++ pins specific versions. `SCI_*` messages newer than wxSTC's snapshot won't be exposed as methods → feature loss.
4. **Performance.** Internal `execute()` depends on the **cached direct function pointer** (`_pScintillaFunc`/`_pScintillaPtr` from `SCI_GETDIRECTFUNCTION`/`SCI_GETDIRECTPOINTER`, verified `ScintillaEditView.cpp:379–380`), called directly — *not* via `SendMessage`. wxSTC's wrapper bypasses that fast path. **Precise distinction (corrected from draft):** only *plugin* `SCI_*` traffic uses `SendMessage` to the Scintilla HWND; *internal* editor traffic already uses the direct pointer and is therefore independent of the HWND message path. This strengthens the native-Scintilla case: keeping native Scintilla preserves **both** the plugin HWND path **and** the internal direct-pointer fast path.

### 6.2 Why Option 2

- `_scintillaMainHandle`/`_scintillaSecondHandle` remain **real native HWNDs** → plugin `SCI_*` works unchanged, `NPPM_CREATESCINTILLAHANDLE` returns a genuine handle.
- Internal `execute()` keeps its direct-pointer fast path untouched.
- Boost.Regex, lexer config, markers/indicators, printing (`SCI_FORMATRANGE`), CJK/RTL/IME all keep their current, proven behavior.
- wx only manages layout/sizing/focus of the hosting `wxWindow`; the editor internals are untouched.

### 6.3 Risks to manage (Option 2)

- **HWND lifecycle across dock/float reparents** — ensure the native child isn't destroyed; use detach/reattach not recreate.
- **Focus translation** — map `wxEVT_SET_FOCUS`/`KILL_FOCUS` to the child HWND so keyboard never gets lost; plugins also call `SetFocus(scintilla_hwnd)` directly, which must continue to work.
- **DnD/IME coexistence** — let Scintilla own editor OLE DnD and IME; do not overlay a wx drop target (§5.9).
- **DPI sizing** — `wxScintillaCompat::OnSize` must position the native child to match wx's computed rect at fractional DPI, and behave under wx's per-monitor model (§9).
- **Dark mode/scrollbars** — keep applying `NppDarkMode::setDarkScrollBar(hSelf)` to the native child; expose `GetHandle()` consistently.

---

## 7. Subsystem-by-Subsystem Migration Table

Difficulty and 1:1 risk are on a 5-point scale (Low → Very High). "wx target" reflects the **Path A (ABI-preserving, Windows-only)** decision, which sometimes means *retain native* rather than *port to wx*. An explicit **internal-coupling** column captures the §5.6 cost.

| # | Subsystem | LOC | Difficulty | wx Target (Path A) | Internal coupling | 1:1 Risk | Notes |
|---|---|---:|---|---|---|---|---|
| 1 | Main window + message loop + IPC | 19,657 | Very High | `wxApp`+`wxFrame` shell; **retain** `process()` core; shim `WndProc` | Very High | **Severe** | Ordering (`isDlgsMsg`→accel→dispatch), synchronous `NPPM_*`, `messageProc` relay, `WM_COPYDATA`, reentrancy under modal, exception/dump |
| 2 | Plugin subsystem & ABI | 2,428 | Very High | HWND shim + registries + HMENU/HACCEL bridges (§5) | Very High | **Severe** | The crux; small LOC, dominant risk; 7 entry points; inter-plugin relay |
| 3 | Scintilla hosting + editor | 20,979 | High | `wxScintillaCompat` (native Scintilla) | Med | High | Native retained; focus/DPI/DnD/IME/lifecycle the real work; direct-pointer fast path preserved |
| 4 | Docking & splitter framework | 6,087 | Very High | **PRIMARY: retain native**; wxAUI as R&D (§9) | High | **Severe** | hClient HWND stability across dock/float; multi-monitor XOR overlay; % splitters |
| 5 | Dockable panels (8 built-in + infra) | 12,452 | High | `wxPanel`+`wxTreeCtrl`/`wxListCtrl`; `wxScintillaCompat` for DocumentMap | High | High | Plugin `hClient` stability; owner-draw lists; folder-watch worker threads (§5.8) |
| 6 | Find/Replace / UDL / Autocompletion | 14,648 | High | `wxDialog`/`wxAuiNotebook` + native Scintilla for Finder; **keep** SCI-layer AC/calltips | High | High | ReBar incremental-search; the 3 accel pre-passes; AC/highlight unchanged |
| 7 | Preferences / Shortcut Mapper / base dlgs | 19,572 | High | `wxDialog`/`wxNotebook`; `wxGrid` for BabyGrid; runtime `HACCEL` retained | High | High | BabyGrid 55+ `BGM_*` (≈63 internal sites); WinMgr `TOFIT`; `WH_GETMESSAGE` hook; **runtime accel coexistence (§5.4)** |
| 8 | TabBar / ToolBar / StatusBar / icons | 4,024 | High | `wxAuiNotebook`(custom art)/`wxToolBar`/`wxStatusBar`/`wxImageList`; **ReBar surrogate**; dual HWND identity | **Very High** | High | Owner-draw tabs (TabBar ≈49 internal sites), ReBar bands, `SBT_OWNERDRAW`, dual icon sets; **owner-draw reproduced in `wxDC`** |
| 9 | Dark mode / theming / DPI | 6,328 | Very High | **Retain native** NppDarkMode on *native* HWNDs; reimplement in wx per converted control; reconcile DPI model | Med | **Severe** | uxtheme ordinals, IAT scrollbar hook, UAH menus — no wx 1:1; **System→per-monitor DPI change**; tension with wx-converting controls |
| 10 | Common controls & Win32 utilities | 12,200 | High | `wxWindow` base; `wxFileDialog`(±custom), keep ColourPopup, `wxToolTip`; native DnD/clipboard for editor | High | High | `IFileDialog` customization, `TrackPopupMenu`, editor context menu (plugin-extensible), DoubleBuffer, WinMgr |
| 11 | Resources / dialog templates / l10n | 3,475 | High | Hand-port 69 DIALOGEX → `wxSizer`/XRC; keep nativeLang XML; **preserve IDs** | Med | High | Preserve `resource.h`/`menuCmdID.h` IDs (plugin ABI + 728 `GetDlgItem` sites); 94 lang files (§8) |
| 12 | Build / dependency integration | ~200 | High | wx via FetchContent, `/MT`, link vendored Scintilla/Lexilla; **manifest ownership** | Med | High | CRT match, no wxSTC, DPI/comctl manifest conflict (§10), post-build model XML copy |

---

## 8. Dialog Migration Methodology (the 69 DIALOGEX templates)

### 8.1 Approach: tooling-assisted hand-port (not pure-auto, not pure-manual)

1. **Seed** each `.rc` template with a semi-automated `RC→XRC` pass (e.g., wxFormBuilder import) to capture ~60–70% of layout structure.
2. **Refine** by hand to hit pixel targets, tab order, fonts, and DPI behavior. Pure auto-converters lose `EXSTYLE`, tab order, and owner-draw; pure-manual is 2–3× slower across 69 templates.
3. **Preserve control IDs** verbatim from `resource.h`/`menuCmdID.h`. **Non-negotiable**: plugins send `WM_COMMAND`/`NPPM_EXECUTECOMMAND` with these IDs, and ~728 `GetDlgItem`/`Set/GetDlgItemText` sites key off them. Add a CI check that no ID drifts or collides.
4. **Inject localization** post-creation: extend `NativeLangSpeaker` with a `changeDialogLang(wxWindow*)` that walks children by ID and calls `SetLabel`, reusing the existing 94 `nativeLang/*.xml` files and `MenuPosition` tables unchanged.
5. **Apply dark mode** by keeping native `NppDarkMode::autoSubclassAndThemeChildControls` on the underlying HWNDs of wx dialogs **only while those children remain native common controls**; for any child reimplemented as a wx-drawn control, dark mode is reimplemented in wx (§9).
6. **RTL** via `SetLayoutDirection(wxLayout_RightToLeft)` driven by the nativeLang RTL flag, replacing in-memory `DLGTEMPLATE` patching.

### 8.2 Complexity tiers & per-dialog effort

| Tier | Count | Examples | Effort/dialog |
|---|---:|---|---|
| Simple | ~21 | GoToLine, About, ColumnEditor, FindCharsInRange, lesDlgs | 8–16 h |
| Medium | ~28 | 18 Preference sub-panels, RunDlg, ShortcutMapper, WindowsDlg | 16–48 h |
| Complex | ~20 | FindReplace+Finder, UserDefineDialog (5 sub-dlgs), the 8 dockable panels, BabyGrid | 32–80 h |

**Total dialog-port effort: ~1,700–3,000 engineer-hours** sequential (~210–380 engineer-days), parallelizable across ~6–8 engineers to a ~12–18 week critical path, **plus 20–30%** for screenshot-regression hardening.

> **Scope caveat (NEW):** these are *dialog-layout* hours. They **do not** include (a) reproducing **owner-draw controls** pixel-for-pixel in `wxDC` (TabBar, status-bar `SBT_OWNERDRAW`, BabyGrid, ReBar, dark-mode owner-drawn lists/trees), nor (b) the **internal-message rewiring** (§5.6) each embedded custom control drags in. Both are budgeted separately in §11/§14.

### 8.3 Known fidelity hazards

- **Dialog units ≠ pixels.** `wxSizer` is proportional; pixel-exact spacing needs `FromDIP` and manual tuning per dialog — and must be re-validated under wx's per-monitor model.
- **WinMgr `WRCT_TOFIT`** (size-to-fit whole dialog) has no `wxSizer` analog. Keep WinMgr as a bridge for `WindowsDlg`/`SizeableDlg`, or write a custom layout class.
- **BabyGrid** (3,031 LOC, 55+ `BGM_*`, ≈63 internal `SendMessage` sites): port to `wxGrid` with custom `wxGridCellRenderer`/`wxGridCellEditor`; expect visual differences; rewire internal callers.
- **`WH_GETMESSAGE` hook** in `WindowsDlg`: reimplement via wx event filtering.
- **Modal return codes:** normalize to `wxID_OK`/`wxID_CANCEL` while preserving any `INT_PTR` values plugins/internal callers expect.

---

## 9. Docking / Layout / DPI Strategy (wxAUI gaps + DPI change)

### 9.1 Decision (revised: native docking is now the PRIMARY plan)

**Primary plan: retain the native `DockingManager`/`DockingCont`/`Gripper`/splitter framework, hosted under the wx frame**, because it is the only approach that *guarantees* plugin `hClient` HWND stability across dock/float and reproduces the multi-monitor overlay exactly. **`wxAuiManager` is pursued as a parallel R&D track**, not the committed path. (The draft had this backwards — wxAUI primary, native fallback. The evidence below shows the parity gap is closer to a research effort, so native should be the default and wxAUI must *earn* its way in.)

### 9.2 Concrete gaps vs. the current custom docking (why wxAUI is R&D, not committed)

1. **Four fixed zones** (`CONT_LEFT/RIGHT/TOP/BOTTOM`) — wxAUI is free-form; emulating fixed zones is constraint-fighting.
2. **Caption/tab metrics** — current `HIGH_CAPTION=18px`, `CAPTION_GAP=2px`, close button at (3,3), 12×12. Requires a `wxAuiDefaultTabArt` subclass overriding `GetTabSize`/`DrawTab`; validate per DPI.
3. **Gripper drag overlay** — multi-monitor XOR overlay spanning the virtual screen. Override `wxAuiManager::DrawHint()`; wx default hints clip/draw differently.
4. **Plugin `hClient` HWND identity** — **wxAUI reparents/recreates floating frames on dock↔float**, which can invalidate `hClient`. Keeping `GetHandle()` stable here is the single hardest wxAUI item and the main reason native docking is primary.
5. **% splitters + fixed modes** (`DM_GETSPLITTER_X/Y`) — wxAUI sashes are pixel-based; ratios must be stored/restored in an override.
6. **Floating chrome** — `WS_POPUP|WS_THICKFRAME` minimal chrome vs. `wxAuiFloatingFrame` system chrome.
7. **Focus/active-caption** — current `WH_CALLWNDPROC` hook drives active/inactive caption color; replace with focus bindings + manual state.
8. **Config persistence** — `DockingManagerData` (zone sizes, float rects, tab lists) vs. opaque `SavePerspective` XML. Under native docking this round-trips unchanged; under wxAUI both formats must be serialized.

### 9.3 DPI behavioral change (NEW — corrected characterization)

The app today is **System-DPI-aware with an unaware fallback** (`manifest:49–50`), with manual scaling via `dpiManagerV2`. **wxWidgets 3.2/3.3 default to per-monitor-v2 awareness.** Adopting wx therefore **changes the DPI model from System to per-monitor**, introducing live per-monitor reflow/redraw and DPI-change events that **do not exist today**. Options:

- **(i) Force System awareness** by owning the manifest and configuring wx to not opt into per-monitor (verify wx honors this) — closest to current behavior, lowest fidelity risk, but fights wx's defaults and may break wx's own scaling.
- **(ii) Embrace per-monitor** — modernizes DPI but is a *behavioral change* (NG6) requiring re-validation of every dialog, the native Scintilla child sizing, and `dpiManagerV2`'s role.

Decision: **default to (i)** to preserve 1:1, treat (ii) as out of scope unless explicitly chosen. Either way this is a **behavioral-change risk** (§13 R5), not a no-op "reconcile an owner."

### 9.4 Dark-mode tension (NEW)

`NppDarkMode` works by subclassing **native** common controls (`SysListView32`, `SysTreeView32`, etc.) plus uxtheme ordinal + IAT scrollbar hooks (≈25 sites, verified). **Once a control becomes a wx-drawn control, NppDarkMode's per-class subclassing no longer applies**, and dark mode must be reimplemented in wx for that control. So "retain native dark mode" and "convert controls to wx" are in **direct tension** — every wx conversion adds a dark-mode reimplementation task. Budget dark mode as an *ongoing tax on every leaf conversion*, not a one-time Phase-9 item.

---

## 10. Build / Dependency Changes

### 10.1 Current build (verified shape)

Top-level CMake orchestrates three subprojects: `scintilla/` (libscintilla.lib, Boost.Regex via vendored `boostregex/`), `lexilla/` (liblexilla.lib), and `PowerEditor/` (app, 26 `.rc`, links both + Win32 SDK libs). `CMAKE_MSVC_RUNTIME_LIBRARY` is hardcoded to **`/MT`** (static CRT), MSVC-only, with a post-build copy of `langs.model.xml`/`stylers.model.xml` to `bin/`.

### 10.2 Required changes

1. **Add wxWidgets via `FetchContent`** (not prebuilt binaries, not vcpkg-default) to guarantee it is compiled with the **same `/MT` static CRT**. A CRT mismatch is the #1 cause of link/crash failures and is non-negotiable.
2. **Do NOT enable `wxUSE_STC` / link wxSTC.** Continue building the vendored `libscintilla.lib`/`liblexilla.lib`; `wxScintillaCompat` consumes them (§6).
3. **Enforce build flags on the wx target:** static CRT, target Windows version, Unicode, disable unused wx modules to limit size.
4. **Manifest ownership is a concrete conflict (NEW).** wx wants to emit its **own** manifest fragment (DPI awareness + common-controls v6); the app ships a hand-tuned `notepad++.exe.manifest` declaring **System** DPI awareness. **Pick one source of truth** — keep the app's manifest and suppress wx's manifest emission (`wxUSE_RC_MANIFEST=0` / linker `/MANIFEST:NO` as appropriate) — to avoid a DPI-awareness conflict that silently flips the app to per-monitor (§9.3). Verify the resulting binary's effective awareness.
5. **Preserve the post-build model-XML copy.**
6. **Resource strategy:** as dialogs migrate, their `.rc` templates retire, but `resource.h`/`menuCmdID.h` ID definitions **stay** (plugin ABI + command IDs). Keep `Notepad_plus.rc` for icons/version/menu/manifest until those are fully ported.
7. **CI:** Windows runners at `/MT`; a screenshot-diff harness; a plugin-ABI smoke test (load N reference plugins, exercise `NPPM_*` incl. `messageProc`); and an **effective-DPI-awareness assertion** on the built binary.

### 10.3 Dependency risk (rated higher than draft)

- **DLL/CRT hell with plugins:** plugins are separate DLLs built by third parties against the documented SDK; keep the app's CRT linkage consistent. Static-link wx to avoid shipping wx DLLs that could collide. This is a **multi-front** compatibility exercise (CRT + manifest + Boost.Regex + plugin DLL ABI) → R7 is **High**, not Med (§13).
- **Boost.Regex must remain** the Scintilla regex backend; never inherit wxSTC's.

---

## 11. Phased Roadmap

Principle: **leaf/low-risk first, ABI-core last** — tempered by the fact that "leaf" still carries internal-rewiring cost (§5.6). Every phase is independently buildable, shippable, and regression-gated. Effort ranges are engineer-months (EM) for a team that has completed Phase 0.

### Phase 0 — Foundations, coupling census & **hard** crux spike (de-risk early)
- **Scope:** Add wx via FetchContent at `/MT`; stand up an empty `wxApp`+`wxFrame` that boots Notepad++ with the **existing native UI intact**. Build the **ABI shim skeleton** (subclass frame HWND; route `WM_COPYDATA` + a representative `NPPM_*` set + `messageProc`/`NPPM_MSGTOPLUGIN`). **Measure internal-message coupling per subsystem** (§5.6). Verify **effective DPI awareness** of the wx build. Build the screenshot-diff + plugin-smoke CI.
- **Exit criteria (hardened — must prove the *hard* parts, not just easy round-trips):**
  1. ≥3 reference plugins load and exercise `NPPM_GETCURRENTSCINTILLA`, buffer read/write, and **inter-plugin `NPPM_MSGTOPLUGIN`**.
  2. **A docked plugin panel survives dock→float→dock without its `hClient` HWND changing.**
  3. **A plugin adds a menu item (HMENU) and a toolbar button**, both functional.
  4. **A plugin calls `SendMessage` to `_nppHandle` from a worker thread** without deadlock, and `PostMessage` ordering is preserved.
  5. **Correct Tab/Esc/incremental-find routing with a plugin modeless dialog open** (the 3 accel pre-passes reproduced).
  6. **`FindWindow(L"Notepad++")` discovery works**, and the binary's **effective DPI awareness is the intended one** (System, per §9.3).
  7. Coupling census delivered; effort re-baselined.
- **Effort:** **4–6 EM.** *(This phase proves or kills the project. The soft "round-trip a handful of NPPM_" gate in the draft would have given false confidence; the above are the real risks.)*

### Phase 1 — Leaf dialogs (simple tier)
- **Scope:** Port ~21 simple modal dialogs to `wxDialog`+`wxSizer`. Establish methodology, ID-preservation CI, l10n hook, dark-mode-via-native-subclass pattern (and the wx-reimplement fallback for any non-native child).
- **Exit:** All simple dialogs pass screenshot diff at the supported DPI scales × 2 themes; l10n verified in ≥3 languages; no ID drift.
- **Effort:** **2–3 EM.**

### Phase 2 — Status bar, toolbar, tray, image lists (first owner-draw + coupling hit)
- **Scope:** `wxStatusBar` (owner-draw panes via custom `wxDC` paint), `wxToolBar` + **ReBar surrogate**, `wxTaskBarIcon`, `wxImageList` (light/dark/normal/reduced). Preserve `registerDynBtn`/`registerDynBtnDM`. **Rewire internal callers** of these controls (dual HWND identity).
- **Exit:** Plugin dynamic toolbar buttons register/enable/disable; dual-icon dark mode correct; status-bar parts match within tolerance; internal status/toolbar updates work via new path.
- **Effort:** **3–4 EM.** (ReBar surrogate + owner-draw + rewiring are the risks.)

### Phase 3 — Preferences & base-dialog framework
- **Scope:** `StaticDialog`→wx base; `SizeableDlg`/WinMgr bridge; PreferenceDlg (18 sub-panels) on `wxNotebook`; RunDlg; WindowsDlg (incl. `WH_GETMESSAGE` hook reimplementation).
- **Exit:** All preference panels functional + pixel-checked; apply/save round-trips; WindowsDlg resize behaves.
- **Effort:** **3–5 EM.**

### Phase 4 — Shortcut Mapper, BabyGrid & runtime accelerator coexistence
- **Scope:** `wxGrid`-based BabyGrid with custom renderers (+ rewire ≈63 `BGM_*` sites); ShortcutMapper 5 tabs; **and the runtime `HACCEL`/`TranslateAccelerator` coexistence with wx accel (§5.4)** — distinct from the dialog UI.
- **Exit:** Shortcut editing, conflict detection, plugin shortcut persistence verified; **runtime accelerators (user + plugin `ShortcutKey`) fire identically with wx accel suppressed for those keys**; grid visual within tolerance.
- **Effort:** **3–4 EM.**

### Phase 5 — Built-in dockable panels (still on native docking)
- **Scope:** Port the 8 built-in panels (DocumentMap [native Scintilla preview], FunctionList, FileBrowser, ClipboardHistory, AnsiChar, VerticalFileSwitcher, ProjectPanel, FindCharsInRange) onto wx panels — **hosted in the existing native docking** to isolate panel work from docking work. Preserve folder-watch/parsing **worker-thread marshaling** via the bridge (§5.8).
- **Exit:** Each panel functional + themed; FileBrowser folder-watch threads intact and ordered; DocumentMap sync correct.
- **Effort:** **4–6 EM.**

### Phase 6 — Editor hosting: `wxScintillaCompat` + dual view + DocTab
- **Scope:** Wrap native Scintilla in `wxScintillaCompat`; migrate `_mainEditView`/`_subEditView` (verified members) and the dual-pane container; DocTab → `wxAuiNotebook`/custom with owner-draw parity (rewire ≈11 DocTab + tab sites). Resolve **focus, DnD, and IME coexistence** (§5.9). Keep AC/calltips/highlighters at the SCI layer.
- **Exit:** Editing, split view, large-file perf (≥ baseline via direct-pointer path), focus routing, DnD, IME, DPI sizing, printing, macro record/replay all verified.
- **Effort:** **5–8 EM.** (Hottest path; perf-gate strictly.)

### Phase 7 — Docking: confirm native, evaluate wxAUI
- **Scope:** Re-host Phase-5 panels and the editor in the **native docking under the wx frame** (primary). In parallel, build the wxAUI R&D spike (custom `wxAuiTabArt`, % splitter, overlay-hint override, **`hClient` stability test**); adopt wxAUI **only if** it passes the hClient-stability + multi-monitor-overlay bar.
- **Exit:** Dock/float/reorder/multi-monitor drag/RTL/focus-caption parity; config round-trip (`DockingManagerData` preserved); plugin `hClient` stable. Go/no-go on wxAUI recorded.
- **Effort:** **4–6 EM** (native-primary) / **+3–5 EM** if wxAUI is adopted.

### Phase 8 — Find/Replace + UDL + incremental search
- **Scope:** FindReplaceDlg + Finder (native Scintilla in a wx pane), UserDefineDialog (5 sub-dialogs), ColumnEditor; ReBar incremental-search → wx surrogate; ensure the 3 accel pre-passes still route incremental-find correctly with these dialogs open.
- **Exit:** Find/replace/mark/find-in-files, Finder docking, UDL editing, incremental search all behavior-verified; internal `NPPM_INTERNAL_*` routing intact.
- **Effort:** **3–5 EM.**

### Phase 9 — Dark mode / DPI consolidation
- **Scope:** Ensure native NppDarkMode applies to all still-native HWNDs; **reimplement dark mode in wx for every control already converted** (the §9.4 tax, collected here); finalize DPI-awareness ownership (manifest vs wx, §9.3); UAH menu + scrollbar hooks verified end-to-end.
- **Exit:** Theme toggle + DPI transitions correct across all surfaces and reference plugins; `NPPN_DARKMODECHANGED`/`NPPM_GETDARKMODECOLORS` ABI intact; effective DPI awareness unchanged from baseline.
- **Effort:** **4–6 EM.** (Higher than draft due to per-control wx dark-mode reimplementation.)

### Phase 10 — ABI hardening, reentrancy, IPC, and cutover (LAST)
- **Scope:** Complete all **212 `NPPM_*`** handlers + **7 entry points** + **inter-plugin relay** through the shim with verified synchronous semantics + `LRESULT` correctness; finalize `isDlgsMsg` equivalent (3 pre-passes), modeless tracking, plugin `HMENU`, runtime `HACCEL`, `NPPM_CREATESCINTILLAHANDLE`, dock ABI; **prove plugin/thread reentrancy safety under wx modal loops (§5.8)**; keep Win32 `WM_COPYDATA` IPC (do **not** replace with pipes on Path A); integrate `Win32Exception`/`MiniDumper` with `wxApp`. Then retire native UI scaffolding.
- **Exit:** Full plugin compatibility suite (20–30 popular plugins) passes; message-ordering, reentrancy, and macro/find regression suites pass; crash-dump path verified.
- **Effort:** **7–10 EM.** (Highest-risk; gated by a large plugin corpus.)

### Phase 11 — Stabilization, performance, beta
- **Scope:** Full regression, perf profiling (message-dispatch latency, large files), visual diff sign-off, public beta with telemetry on plugin failures.
- **Exit:** Diff ≤ agreed tolerance; perf ≥ baseline; plugin failure rate ≤ agreed threshold.
- **Effort:** **4–6 EM.**

**Roadmap effort subtotal: ≈ 50–75 EM** (native-docking primary; +3–5 EM if wxAUI adopted; excludes management overhead and contingency — see §14).

---

## 12. Regression-Testing Strategy

Because "1:1" is the goal, **testing is a first-class deliverable, not an afterthought.**

1. **Visual regression (the bar for G1).** Automated screenshot capture of every dialog/panel/menu/toolbar/tab/status state at the supported DPI scales × light/dark. Pixel-diff with a small tolerance. **Honest caveat (NEW):** wx-rendered text is **not** pixel-identical to GDI/ClearType text even when correct; font-hinting/AA/theme-version differences will routinely exceed tight tolerances. Mitigation: **structural/region-based diffing with a text-rendering carve-out**, perceptual-diff thresholds, and per-control baselines — *not* a single global pixel %. Budget the harness as a real build-out with ongoing calibration (§13 R6/R-test).
2. **Plugin compatibility suite (the bar for G3).** A corpus of 20–30 real, popular plugins (compare, NppExec/PythonScript, NppFTP, JSON/XML tools, dockable-panel plugins). Automated load + scripted exercise of `NPPM_*`, **inter-plugin `NPPM_MSGTOPLUGIN`**, docking register/show/hide/float **with `hClient`-stability assertions**, Scintilla read/write, toolbar buttons, menus, and **worker-thread/reentrant `SendMessage`**. **This is the acceptance test for the whole program.**
3. **Behavioral/sequencing tests.** Macro record→replay byte-equality; find/replace and find-in-files equivalence; **the 3 accel pre-passes** (Tab-switch, incremental-find, find) with modeless + docked dialogs open; **runtime `HACCEL` vs wx accel coexistence**; session save/restore. These catch the ordering regressions §5 warns about.
4. **Reentrancy/threading tests (NEW).** `NPPM_*` arriving during a wx modal loop; plugin `SendMessage`/`PostMessage` from worker threads; internal folder-watch/FunctionList marshaling ordering.
5. **DnD/clipboard/IME (NEW design-level tests).** File drop on the frame; Scintilla OLE DnD inside the editor (no double drop target); CJK + Hebrew/Arabic round-trips; IME composition positioned correctly over the embedded Scintilla.
6. **DPI-awareness assertion (NEW).** CI assertion that the built binary's effective DPI awareness matches the intended (System) model; per-monitor transition tests if (ii) is ever chosen.
7. **Editor parity & performance.** Golden-file lexing/styling across languages; large-file open/scroll/undo benchmarks gated to **≥ current performance** (Phase 6 hard gate); confirm internal `execute()` still uses the direct pointer.
8. **Crash/robustness.** Verify `MiniDumper` still produces dumps under `wxApp`; fault-injection in plugin handlers (shim must not deadlock the loop).
9. **Manual exploratory** per phase on Win10 + Win11, multi-monitor mixed-DPI.

---

## 13. Risk Register

Scale: Likelihood / Impact ∈ {Low, Med, High}.

| ID | Risk | L | I | Mitigation |
|---|---|---|---|---|
| R1 | **Plugin ABI breakage** (HWND/synchronous `NPPM_*`, menus, dock, inter-plugin) | High | High | Mandatory shim (§5); synchronous inline dispatch; HMENU/HACCEL bridges; 212-case + 7-entry-point tests; 20–30 plugin corpus; hardened Phase-0 gate |
| **R1b** | **`wxFrame`-HWND subclassing is an unproven, version-fragile bet** (the recommended D1) | High | High | **Promoted out of R1.** Prove in Phase 0 across wx 3.2/3.3; have D1(b) dedicated-window + frame-find fallback ready; pin a wx version and re-test on upgrade |
| R2 | **Message-ordering regressions** incl. the **3 accel pre-passes** and `HACCEL` vs wx accel | High | High | Reproduce `isDlgsMsg` precedence in `FilterEvent`/`MSWTranslateMessage`; suppress wx accel for runtime-`HACCEL` keys; record/replay byte-equality + dedicated key-routing matrix |
| **R-int** | **Internal-message coupling** (~1,215 sites) makes leaf work non-isolated | High | High | **NEW.** Dual HWND identity; per-subsystem rewiring; Phase-0 coupling census; effort re-baseline; treat each leaf as "control + its inbound `SendMessage` callers" |
| R3 | **Scintilla strategy wrong** (if wxSTC chosen) | Low | High | Decision locked to native `wxScintillaCompat` (§6); keep Boost.Regex + version pins + direct-pointer path |
| R4 | **Docking parity / `hClient` stability shortfall** (wxAUI) | High | High | **Native docking is PRIMARY**; wxAUI only if it passes hClient-stability + overlay bar (§9) |
| R5 | **DPI model change (System→per-monitor) + dark-mode tension** | High | High | **Reframed (corrected).** Own the manifest, force System awareness (§9.3); CI awareness assertion; budget per-control wx dark-mode reimplementation as a leaf tax (§9.4) |
| R6 | **Dialog/text pixel drift; screenshot harness flakiness** | High | Med | Tooling-assisted port + `FromDIP`; **region-based/perceptual diff with text carve-out** (§12); per-control baselines; calibration budget |
| R7 | **Build/CRT/manifest mismatch** (wx vs `/MT`, plugin DLLs, DPI/comctl manifest) | **High** | High | **Raised to High.** FetchContent wx at `/MT`; static-link wx; **own the manifest, suppress wx's** (§10.2); CI build matrix + awareness assertion |
| R8 | **Scope/cost overrun** (182K LOC + internal coupling) | High | High | Phase gates; **hardened** go/no-go after Phase 0; second gate after Phase 6; wider effort band (§14) |
| R9 | **Performance regression on large files** | Med | High | Native Scintilla + direct function pointer retained; Phase 6 perf gate ≥ baseline |
| R10 | **Reentrancy/threading under wx modal loops** | Med | High | **NEW.** Modal-safe/defer classification; reentrancy guards; `PostMessage⇄CallAfter` bridge; Phase-0 worker-thread test |
| R11 | **DnD/clipboard/IME double-stack regressions** | Med | Med | **NEW.** Let Scintilla own editor OLE DnD/IME; no wx drop target on host; native file-drop on shim (§5.9) |
| R12 | **Edge-case plugins** (child-HWND enumeration, POPUP chrome) — *worsens as controls convert to wx* | Med | Med | Document unsupported patterns; provide `NPPM_GET*HANDLE` alternatives; chrome-matching float frame |
| R13 | **IPC / external-discovery regression** (`WM_COPYDATA`, `FindWindow` class) | Low | Med | Keep native `WM_COPYDATA`; preserve registered class name + discoverable top-level HWND (§5.7) |
| R14 | **Team wxWidgets inexperience** | Med | Med | Phase 0 ramp; pair experienced wx engineer with Win32 domain experts |
| R15 | **"1:1 expectation gap"** with stakeholders | High | High | This document's §1 verdict + numeric tolerances signed off **before** funding |

---

## 14. Effort & Timeline Estimate, Team Shape

### 14.1 Effort

Bottom-up from §11: **≈ 50–75 EM** of direct phase work (native-docking primary; +3–5 EM if wxAUI adopted). Adding standard overhead (integration, management, contingency at ~40–55% for a very-high-complexity port with **two** crux problems — the ABI shim *and* internal-message coupling — plus a research-grade D1 bet):

- **Realistic total: ~85–130 engineer-months (≈ 7–11 engineer-years).**
- This is a **wider band than the draft's ~70–100 EM**, deliberately: the draft's midpoint assumed leaf work was largely self-contained, but the ~1,215 internal-message sites mean each converted control tends to drag core refactors with it. The **floor (~85 EM)** is plausible only if D1 proves stable early and coupling per subsystem is lower than feared; the **ceiling (~130 EM)** reflects D1 instability, wxAUI adoption, and high coupling. Use **~100–105 EM** as the planning midpoint, **but treat the Phase-0 coupling census as the trigger to re-baseline.**
- An independent cross-check (18–24 months wall-clock at 4–5 senior engineers ≈ 72–120 EM) is consistent with this band.

This is **Path A (Windows-only, ABI preserved)**. **Path B (break ABI + cross-platform)** adds, conservatively, **+30–60 EM** for a v2 plugin API, native dark-mode/IPC/file-dialog/tray/DnD re-implementations, macOS/Linux build/QA, and ecosystem shepherding — and is better scoped as a *separate* program.

### 14.2 Timeline

With a team of **4–5 senior engineers**, wall-clock **~20–28 months** to a stable Windows beta, assuming Phase 0 succeeds. Phases 1–5 parallelize reasonably (dialogs/panels), but **internal rewiring serializes more than dialog layout does**; Phases 6, 7, 10 are largely serial on the critical path.

### 14.3 Team shape

- **1 Lead architect** (owns the ABI shim + message-loop + D1 bet; this is where the project lives or dies).
- **2 senior C++/Win32 engineers** (shim, Scintilla hosting, native docking, dark mode/DPI, internal rewiring).
- **2 senior C++/wx engineers** (dialogs, panels, toolbars; **one must be genuinely experienced with wxWidgets internals**, including `MSWWindowProc`/event-loop behavior across versions).
- **1 QA/automation engineer** (screenshot-diff + plugin-corpus + reentrancy harness; full-time given §12).
- **Part-time:** build/release engineer (CMake/CI/CRT/manifest), and a plugin-ecosystem liaison if Path B's v2 API is pursued.

---

## 15. Recommendation & Decision Points

### 15.1 Recommendation

1. **Reframe "1:1."** Adopt the §1 verdict as the contract: 1:1 means *Windows visual/behavioral fidelity within agreed tolerances + preserved plugin ABI*, **not** a clean cross-platform rewrite, **not** byte-identical behavior everywhere, and **not** identical text rendering or DPI model.
2. **Choose Path A** (Windows-only, ABI-preserving, hybrid).
3. **Keep Scintilla native** (`wxScintillaCompat`); reject wxSTC.
4. **Make native docking the PRIMARY plan**; treat wxAUI as R&D that must pass the `hClient`-stability bar.
5. **Own the manifest and force System DPI awareness** to avoid an unintended per-monitor behavioral change.
6. **Treat D1 (frame subclassing) as an explicit research bet** with a dedicated-window fallback, proven in Phase 0.
7. **Gate hard after Phase 0** with the *hardened* exit criteria (dock/float `hClient` stability, menu+toolbar from a plugin, worker-thread `SendMessage`, the 3 accel pre-passes, coupling census). Fund only Phase 0 (4–6 EM) initially. If the spike fails any hard criterion, **stop or pivot to Option C** (wrap-only, native UI retained).
8. **Re-baseline effort on the Phase-0 coupling census** before funding beyond Phase 1.
9. **Be willing to conclude "don't migrate the core."** If portability is not a hard requirement, the honest engineering answer may be: wrap the bootstrap in `wxApp` for future optionality, migrate *leaf* dialogs opportunistically, and **leave the message loop, docking, ABI, dark mode, and DPI native indefinitely.** That captures most maintainability upside at a fraction of the risk.

### 15.2 Explicit decision points for you

- **DP1 — Strategic intent:** Cross-platform (→ Path B + ecosystem split + separate program) or a faithful Windows port that future-proofs toward portability (→ Path A)? *Everything downstream depends on this.*
- **DP2 — Plugin ABI:** Preserve at all costs (Path A) vs. break with a v2 API (Path B)? Are you willing to tell the plugin community their plugins break?
- **DP3 — Fidelity tolerance:** Agree the numeric bar now — screenshot-diff % **with the text-rendering carve-out**, and acceptable plugin-failure rate. Without numbers, "1:1" is unfalsifiable.
- **DP4 — Funding model:** Approve **Phase 0 only** as a go/no-go gate, then re-baseline on the coupling census? (Strongly recommended.)
- **DP5 — Scintilla:** Confirm native-hosting decision (locks out wxSTC and its bundled regex/version).
- **DP6 — Docking:** Confirm **native-docking-primary**; accept wxAUI only if it earns the `hClient`-stability bar.
- **DP7 — DPI model:** Confirm **force System awareness** (preserve current behavior) vs. accept per-monitor as a behavioral change.
- **DP8 — Stopping point:** Pre-agree that "wrap-only + leaf dialogs, native core retained" is an acceptable *terminal* state if the crux, the D1 bet, or budget resists — so the team can stop honestly rather than over-invest.

---

### Appendix — Evidence anchors (verified in-repo this revision)

- `MISC/PluginsManager/PluginInterface.h`: `NppData{ HWND _nppHandle; HWND _scintillaMainHandle; HWND _scintillaSecondHandle; }`; **7 exports** incl. `messageProc`/`PMESSAGEPROC` (lines 40, 65–72). **Verified.** *(Path corrected from draft's `WinControls/...`.)*
- `MISC/PluginsManager/PluginsManager.h`: `_pMessageProc` (line 67), `relayPluginMessages` (line 114), `ID_PLUGINS_CMD_DYNAMIC` allocator (line 91), `getMenuHandle()` (line 116). **Verified.**
- `NppBigSwitch.cpp`: **212** `case NPPM_*`; `NPPM_MSGTOPLUGIN → relayPluginMessages` (lines 3133–3135); broadcast relay (line 4321). **Verified.**
- `WinControls/DockingWnd/Docking.h` + `DockingDlgInterface.h:55`: `DockedWidgetData{ HWND hClient; … }`, `data->hClient = _hSelf`, four-zone defines. **Verified.**
- `winmain.cpp`: loop order `isDlgsMsg → TranslateAccelerator → DispatchMessageW` (830–835); `FindWindow(getClassName())` discovery (736, 740); `Win32Exception`/`MiniDumper`, `WM_COPYDATA` IPC. **Verified.**
- `Notepad_plus_Window.cpp`: `WndProc` bound via `CreateWindowEx` `lpCreateParams`/`CREATESTRUCT` (99–109, **not** `GWLP_USERDATA`); `WS_EX_ACCEPTFILES` (100); `isDlgsMsg` = 3 accel pre-passes + modeless `IsDialogMessageW` (446–463). **Verified.**
- `ScintillaComponent/ScintillaEditView.cpp`: internal `execute()` uses cached `_pScintillaFunc`/`_pScintillaPtr` from `SCI_GETDIRECTFUNCTION`/`SCI_GETDIRECTPOINTER` (379–392) — independent of the plugin `SendMessage` path. **Verified.**
- `Notepad_plus.h`: `_mainEditView`/`_subEditView`/`_pEditView` dual-view (297–301). **Verified.**
- `notepad++.exe.manifest`: `dpiAware=true` + `dpiAwareness="system, unaware"` (49–50) — **System** DPI aware, **not** per-monitor; common-controls v6 dependency (11–22). **Verified.**
- `NppDarkMode.cpp` (at `src/` root) + `DarkMode/DarkMode.cpp` + `DarkMode/IatHook.h`: ~25 subclass/dark-scrollbar sites; uxtheme ordinal + IAT scrollbar hooks. **Verified.**
- **Internal coupling:** ~**1,215** `SendMessage`/`PostMessage` across **74** files; ~**95** `RegisterWindowMessage`/`nmhdr.hwndFrom` across 15 files (grep). **Verified.**
