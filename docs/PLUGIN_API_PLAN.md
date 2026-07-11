# Plugin API strategy: landscape analysis, our own API, and the N++ compatibility layer

*Status: living document. Last updated 2026-07-03.*

This document consolidates four things the project needs in one place:

1. the current state of our **clean-room N++ plugin ABI headers**,
2. an **analysis of the plugin APIs** of Notepad++, Visual Studio Code, Sublime Text and Pulsar,
   distilled into the capability set a modern text-editor API must offer,
3. the **plan for our own API** ("Nib") — its own thing, informed by that analysis,
4. the **plan for the GPL compatibility layer** that mediates between the Notepad++ binary ABI
   and our API — plus naming candidates for it.

---

## 1. Where the ABI headers stand today

`include/npp-compat/` contains **clean-room, Apache-2.0 redeclarations** of the four headers a
Notepad++ binary plugin is compiled against:

| Header | Carries | Provenance |
|---|---|---|
| `menuCmdID.h` | the `IDM_*` command ids | numeric values only — functional facts; no N++ comments/wording |
| `Notepad_plus_msgs.h` | `NPPM_*` / `NPPN_*` message ids + payload structs | values & layouts only, re-expressed portably |
| `PluginInterface.h` | `NppData`, `FuncItem`, plugin entry-point signatures | layouts & exported names only (binary interop requires them) |
| `Docking.h` | `tTbData` + `DWS_*`/`CONT_*` flags | layouts & values only |
| `npp_plugin_port.h` | *(ours outright)* portable Win32 type vocabulary so the above compile off-Windows | original |

No Notepad++ source text ships in this repository; struct layouts, exported symbol names and
numeric constants are reproduced because binary compatibility requires them (they are
interface facts, not creative expression). The only GPL component consuming these headers is
the optional `packages/npp-bridge` plugin; the application core is N++-ABI-free.

**Conclusion: "rewrite the ABI headers, compatible but not the same" is complete.** Follow-up
hygiene items:
- [x] add a one-page `include/npp-compat/README.md` stating the clean-room methodology and
      what a reviewer should check (no textual overlap, layout equivalence tests);
- [x] add a compile-time `static_assert` suite (`sizeof`/`offsetof` for `NppData`, `FuncItem`,
      `tTbData`, `CommunicationInfo`) so any accidental layout drift breaks the build rather
      than plugins at runtime. (`include/npp-compat/abi_layout_asserts.h`, included from
      `packages/npp-bridge/npp_bridge.cpp` - asserts each field's offset relative to the one
      before it, so it catches drift without needing a hardcoded byte-offset table.)

---

## 2. Editor plugin-API landscape

### 2.1 Notepad++ (C ABI, in-process, message-based)

**Model.** A plugin is a DLL exporting six C symbols (`setInfo`, `getName`, `getFuncsArray`,
`beNotified`, `messageProc`, `isUnicode`). The host hands over three raw `HWND`s (`NppData`);
everything else is Win32 message passing: the plugin `SendMessage`s `NPPM_*` to the main window
and raw `SCI_*` to the Scintilla views, and receives `SCNotification`s and `NPPN_*` events
through `beNotified`. Menu entries are a static `FuncItem` array. Docking panels register a
`tTbData` describing a plugin-owned child `HWND`.

**Strengths.** Zero-dependency binary simplicity; direct, full-power Scintilla access; trivially
small surface to learn; no runtime to embed; plugins are as fast as the host.

**Weaknesses.** Win32-coupled by construction (HWND/UINT/WPARAM in the contract); no
versioning, capability negotiation or feature detection (plugins probe by behaviour); no
isolation (a plugin crash kills the editor); wide implicit surface (any window message works,
so the *de facto* ABI is bigger than the documented one); UI integration limited to menu items
+ raw child windows.

### 2.2 Visual Studio Code (out-of-process, declarative + RPC)

**Model.** Extensions are Node/TypeScript packages running in a separate *extension host*
process. Static **contribution points** in `package.json` declare commands, menus, keybindings,
settings, languages and views without loading any code; **activation events** lazy-load the
extension only when first needed. Code talks to a versioned `vscode` API object: immutable
`TextDocument` snapshots, `TextEditor` views, `WorkspaceEdit` transactions, `Disposable`
lifecycle. Language smarts ride separate protocols (LSP/DAP) rather than the extension API.

**Strengths.** Crash/perf isolation; lazy activation keeps startup clean; declarative UI means
the editor can render menus/keybindings without executing plugin code; a single, semver'd,
deprecation-managed API; document/edit model is transactional and race-free by design.

**Weaknesses.** RPC latency (no direct buffer memory access); heavy runtime; the API surface is
enormous; UI extensibility is deliberately fenced (no arbitrary widgets outside webviews).

### 2.3 Sublime Text (in-process Python, command/listener classes)

**Model.** Plugins are Python files loaded by a pinned interpreter (isolated `plugin_host`
process mediating to the C++ core). The API is small and object-shaped: subclass
`TextCommand` / `WindowCommand` / `EventListener`; buffer mutation only inside a
transactional `Edit` token passed to `run()`; `Region`/`Selection` primitives; declarative
`.sublime-keymap`/`.sublime-menu`/`.sublime-settings` resources; `phantoms`/`minihtml` for
inline UI.

**Strengths.** The most ergonomic command model of the four (a command is a class; undo grouping
is automatic via the `Edit` token); tiny, learnable API; resources-as-data like VS Code;
settings layering is excellent.

**Weaknesses.** Pinned Python version constrains packages; threading rules are subtle
(API mostly main-thread); UI extensibility limited; closed-source host makes deep integration
impossible.

### 2.4 Pulsar / Atom (in-process JavaScript, services + subscriptions)

**Model.** Packages are Node modules with `package.json` metadata, CSON menus/keymaps, and a
live DOM (the editor *is* a web page). Core objects: `TextBuffer`/`TextEditor` with **markers**
and **decorations**; `CompositeDisposable` subscriptions; and the standout idea — **versioned
services**: a package *provides* `autocomplete.provider@2.0.0` and another *consumes*
`^2.0.0`, letting packages depend on each other through semver'd contracts instead of
hard-coupling.

**Strengths.** Services = real inter-plugin dependency management with versioning; markers/
decorations are a great annotation model; total UI freedom (DOM).

**Weaknesses.** Everything in-process on the UI thread (perf, crash blast radius); total UI
freedom (fragile, style-breaking packages); startup cost of eager JS loading.

### 2.5 Synthesis — what a text-editor plugin API must cover

| Capability | N++ | VS Code | Sublime | Pulsar | Verdict for us |
|---|---|---|---|---|---|
| Commands (register/invoke) | FuncItem | contributes.commands | *Command classes | commands registry | **must**, data-declared + runtime-registered |
| Typed, versioned events | beNotified passthrough | events per object | EventListener | subscriptions | **must**, typed structs + per-interface version |
| Document model & transactional edits | raw SCI_* | WorkspaceEdit | Edit token | TextBuffer transactions | **must** — transaction token, undo-grouped |
| Views & selection | raw SCI_* | TextEditor | View/Region | TextEditor/markers | **must** |
| UI surfaces (menu/status/panels) | menu + raw HWND | declarative + webview | menus as data | DOM | **must**, declarative-first |
| Configuration | ini next to dll | contributes.configuration | .sublime-settings | config schema | **must** (schema'd, layered) |
| Lifecycle / lazy activation | DllMain-era | activation events | on-demand load | activationHooks | **should** |
| Capability negotiation / versioning | none | engines + semver API | none | services semver | **must** (query per interface — we already do this) |
| Isolation (out-of-proc) | none | yes | plugin_host | none | **later**, design so it's possible |
| Inter-plugin services | none | extension exports | none | provided/consumed services | **should** (Pulsar's best idea) |
| Language/lexing providers | via Scintilla | LSP | syntaxes as data | grammars | **should** (Lexilla passthrough first) |

---

## 3. Our own API: Nib — current state and evolution plan

Nib already exists (`include/nib/nib.h`, ABI 1.0) and is deliberately *not* a clone of any of
the four: a **C ABI of versioned, queryable interface vtables** — the stability of N++'s
approach without its Win32 coupling, plus VS Code-style capability discipline.

**Today (v1.0, shipped & verified):** `nib.host/1` (log, version), `nib.editor/1` (portable
SCI message bridge), `nib.documents/1` (count/active/open/save), `nib.commands/1` (menu-backed
commands), `nib.events/1` (typed structs: text-changed, saved, selection, activated),
`nib.panels/1` (dockable text panels), `nib.win32/1` (capability-gated native handles —
returns NULL off-Windows; this is what the GPL bridge rides).

**Design principles (keep):**
1. C ABI, no host runtime imposed — bindings for higher-level languages sit on top.
2. Every interface is `"name/major"`-queryable; absence is a feature signal, not an error.
3. Nothing platform-specific in the portable contract; platform escape hatches are separate,
   gated capabilities (`nib.win32`, someday `nib.gtk`).
4. Events are typed structs with `struct_size` for forward compatibility.

**Planned evolution (v1.x — each lands as a new queryable interface, no breaking changes):**

- `nib.edit/1` — **transactional edits** (Sublime's best idea): `begin() -> NibEdit*`,
  batched replace/insert/annotate, `commit()` = one undo unit. Removes the need for plugins
  to reach for raw `SCI_BEGINUNDOACTION`.
- `nib.selection/1` — multi-selection/caret model as data (list of ranges), set/get.
- `nib.config/1` — schema-declared settings (name, type, default, description) persisted by
  the host; layered defaults<user like Sublime/VS Code.
- `nib.status/1` — status-bar fields; `nib.ui/1` — declarative menu subtrees & separators
  (data, not code — VS Code's best idea) replacing ad-hoc `FuncItem`-style registration.
- `nib.keymap/1` — declarative accelerators with conflict reporting.
- `nib.services/1` — **inter-plugin provided/consumed contracts with semver** (Pulsar's best
  idea): `provide("fmt.provider/1", vtable)` / `consume("fmt.provider/^1")`.
- `nib.lang/1` — lexer/indent/fold providers layered over Lexilla; declarative keyword-set
  registration first, custom lexer callbacks later.
- **Manifest & lazy activation** (v2 horizon): a small JSON/TOML manifest next to the plugin
  (`activates_on = ["command:...", "language:..."]`) so the host can build menus without
  loading code — prerequisite for an out-of-process host, which the C-ABI design keeps
  possible (vtables become RPC stubs) but which is explicitly *not* a v1.x goal.

---

## 4. The GPL compatibility layer (N++ ABI ⇄ Nib)

The layer **exists and works**: `packages/npp-bridge` is itself a Nib plugin (GPL v3,
Windows-only) that recreates a Notepad++-shaped world for real N++ binary plugins:

```
N++ plugin DLL  ⇄  npp-bridge (GPL)  ⇄  Nib C ABI  ⇄  wxNote core (permissive-ready)
   NPPM_* / FuncItem / beNotified        nib.win32 / nib.documents / nib.events / nib.commands
```

Mechanics: it rebuilds `NppData` from `nib.win32` handles, `LoadLibrary`s the plugin,
surfaces each `FuncItem` as a Nib command, forwards typed Nib events back as
`SCNotification`s, subclasses the frame `HWND` to own the `NPPM_*` message router, and maps
docking registrations onto the host's `dock_native`. Verified end-to-end with genuine N++
binaries (MIME Tools, Converter).

**Roadmap (coverage tiers; each tier = a milestone with a named test plugin):**

- **Tier 1 — buffers & files** *(target: NppExec-class plugins)*: `NPPM_GETBUFFERIDFROMPOS`,
  `GETCURRENTDOCINDEX`, `RELOADBUFFERID`, `GETFILENAMEATCURSOR`, buffer-id table instead of
  `EditorPage*` leakage; `NPPN_FILEBEFORE*`/`FILEOPENED`/`FILESAVED` notification breadth.
- **Tier 2 — UI** *(target: ComparePlus-class)*: `NPPM_SETSTATUSBAR` (via `nib.status`),
  toolbar icon registration (`NPPM_ADDTOOLICON` → future `nib.ui`), shortcut registration
  honoring `ShortcutKey`.
- **Tier 3 — config & session**: `GETPLUGINSCONFIGDIR` (done), `NPPM_GETNPPFULLFILEPATH`
  family (done), session queries, `NPPM_MSGTOPLUGIN` inter-plugin relay.
- **Tier 4 — long tail**: dark-mode queries (`NPPM_ISDARKMODEENABLED` + colour struct),
  `NPPM_ALLOCATECMDID`, menu checkmarks, doc-switcher integration.
- **Compatibility matrix**: a `docs/npp-bridge-compat.md` table (plugin × works/partial/why)
  regenerated each tier; candidates: MIME Tools ✅, Converter ✅, NppExec, ComparePlus,
  JSON Viewer, XML Tools, DSpellCheck.

Rule that keeps the licensing clean: **N++-derived knowledge flows only downward into the
bridge; the core only ever grows generic Nib capabilities** (a new NPPM need is met by adding
a *generic* Nib interface the bridge then adapts — never by teaching the core N++ semantics).

---

## 5. Name candidates for the compatibility layer

Current working name: `npp-bridge`. Ten candidates (fountain-pen names play on **Nib**):

| # | Name | Why it fits |
|---|---|---|
| 1 | **Inkfeed** | the feed is the pen part that supplies ink to the *nib* — literally the mediator to Nib |
| 2 | **Penpal** | keeps two distant parties corresponding; pen ties into Nib |
| 3 | **Rosetta** | the classic "same meaning, two languages" translation stone |
| 4 | **Gangway** | the boarding bridge between two vessels that were never built together |
| 5 | **Trestle** | a sturdy, unglamorous bridge that exists to carry someone else's traffic |
| 6 | **Causeway** | a raised road across foreign terrain — N++ plugins crossing into wx land |
| 7 | **Drawbridge** | optional and lowered on demand — exactly how the bridge plugin loads |
| 8 | **Liaison** | the diplomatic officer who speaks both protocols |
| 9 | **Babelfish** | drop it in and suddenly everyone understands each other |
| 10 | **Viaduct** | spans the valley between the N++ ABI and the Nib API in the open |

Front-runners: **Inkfeed** (thematically perfect with Nib, unique, greppable) and
**Rosetta** (instantly communicates "translator" to anyone).

---

## 6. Licensing posture recap

- Core (`src/`, `include/nib/`): permissive-ready, zero N++-derived code (audited).
- ABI headers (`include/npp-compat/`): clean-room Apache-2.0 redeclarations (facts only).
- Compatibility layer (`packages/npp-bridge`): **stays GPL v3** — it is the one component
  whose ancestry argues for it, and confining it there is the whole strategy.
- Theme/styler assets: re-authored files; as of 2026-07-03 the palettes are **our own values**
  (systematically derived, verified disjoint from every colour N++ ships) — see commit history.
- The repository as a whole remains GPL v3 until the maintainer deliberately decides otherwise
  (`docs/FUTURE_PLANS.md`).
