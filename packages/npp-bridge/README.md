# npp-bridge — the optional Notepad++ binary-plugin compatibility bridge

`npp-bridge` lets **wxNote host real Notepad++ plugin DLLs** on Windows. It is itself a
[Nib](../../include/nib/nib.h) plugin: the core loads it from `<exe>/nib/` like any other plugin, and it
reaches the host's native handles through the Windows-only `nib.win32` capability to rebuild the
Notepad++ `NppData` environment a binary plugin expects.

**Cross-platform (Phase 1/2).** The bridge now *builds on every OS* — its Win32-only machinery (frame
subclassing via `nib.win32`) is `#ifdef _WIN32`'d out, leaving a portable dispatch spine. Hosting a real
*precompiled* plugin binary is still Windows-only, but off-Windows a plugin author can *recompile* against
the GPL shim SDK (`shim/npp_shim.{h,cpp}` → static `libnpp_shim`): the plugin links it so its own
`::SendMessage` calls route into the bridge's dispatch core. `example/example_plugin.cpp` is the CI-built
proof of that path. See CHANGELOG [0.8.5].

## Why it's a separate, GPL module

This module reproduces Notepad++'s plugin ABI (`NPPM_*` message numbers, `FuncItem`, `NppData`,
`tTbData`, …), so it is **GPL-3.0-or-later**. The wxNote **core depends on none of it** — it is
loaded only if present. Keeping the ABI reproduction confined here is exactly what lets the core stay
Apache-2.0 (see [`LICENSING.md`](../../LICENSING.md)). The core talks only the
permissive `nib.*` API; this bridge is the one place the two worlds meet.

## What it does

1. **Loads** each plugin module and calls `setInfo`/`getFuncsArray`/`getName` — on Windows
   `<exe>/plugins/<Name>/<Name>.dll` via `LoadLibrary`; on Linux/macOS a *recompiled* `<Name>.so`/`.dylib`
   via `dlopen`, bound through `npp_shim_bind` (see `shim/npp_shim.h`).
2. **Surfaces** every `FuncItem` as a Nib command, so the plugin's commands appear in the Extensions menu.
3. **Routes `NPPM_*`** — it subclasses the frame (HWND from `nib.win32`) and answers the messages plugins
   send, translating them to Nib calls.
4. **Forwards `beNotified`** — it subscribes to `nib.events` and delivers them to each plugin as
   `SCNotification`s.
5. **Hosts docking** — `NPPM_DMMREGASDCKDLG`/`SHOW`/`HIDE` host a plugin's own window in a dock pane via
   `nib.win32`'s `dock_native`/`show_dock`.

Editor (`SCI_*`) messages a plugin sends to the editor HWND are bridged to wxStyledTextCtrl by the
*core's* generic `SciHwndProc` (that bridge is not Notepad++-derived and stays in the core).

## NPPM_* coverage

**77 of Notepad++'s 118 `NPPM_*` messages are genuinely served, plus 27 `NPPN_*` notifications — and
from Phase 2 on every one of the 118 `NPPM_*` returns a documented value (zero silent drops).** Plugins
branch on return values, so the remaining 41 are *answered* with the honest constant this host can give
today (a documented no-op or interim value) rather than falling through — see the stub table below.

The **Phase-6** round added the **long-tail file-lifecycle notifications** over the new `nib.events`
**v4** host hooks, plus the **`-pluginMessage`** command-line flag. Eleven notifications land, all
portable (no HWND, no Windows-only path): `NPPN_FILEBEFORELOAD` (before a file's content loads),
`NPPN_READONLYCHANGED` (read-only toggled), `NPPN_DOCORDERCHANGED` (a tab drag or Sort reordered the
strip), `NPPN_SNAPSHOTDIRTYFILELOADED` (a dirty recovery backup restored at startup), the rename triple
`NPPN_FILEBEFORERENAME` / `NPPN_FILERENAMED` / `NPPN_FILERENAMECANCEL`, the delete triple
`NPPN_FILEBEFOREDELETE` / `NPPN_FILEDELETED` / `NPPN_FILEDELETEFAILED`, and `NPPN_CMDLINEPLUGINMSG` — the
host was launched with `-pluginMessage="…"`, whose text the bridge forwards to every plugin (the string
rides in `nmhdr.hwndFrom`, matching N++). Each file notification carries the affected buffer id (0 for the
app-global before-load / cmdline message). See the compatibility notes for the two documented reductions
(`NPPN_READONLYCHANGED` / `NPPN_DOCORDERCHANGED` do not carry N++'s secondary doc-status / new-index field).

The **Phase-4** round added **event fidelity**: `ADDSCNMODIFIEDFLAGS` (opt into raw `SCN_MODIFIED` flags —
the union is pushed to the host, which filters per modification *before* it crosses the ABI, the perf
gate) surfaces matching modifications as `NPPN_GLOBALMODIFIED` carrying the real `modificationType`;
`GETSHORTCUTBYCMDID` reports a command's effective binding via the new `nib.keymap` **v2** read hook
(mapped to the `ShortcutKey` ABI). Six new notifications land: `NPPN_GLOBALMODIFIED`, `NPPN_LANGCHANGED`,
`NPPN_WORDSTYLESUPDATED`, `NPPN_SHORTCUTREMAPPED`, and the shutdown-attempt/cancel pair
`NPPN_BEFORESHUTDOWN` / `NPPN_CANCELSHUTDOWN`. All portable — no HWND, no Windows-only path.

The **Phase-3** round added the per-view **buffer model** over the new `nib.documents` **v5** host hooks:
`GETCURRENTDOCINDEX` / `ACTIVATEDOC` (per-view active index / activate-by-position), `GETPOSFROMBUFFERID`
/ `GETBUFFERIDFROMPOS` (buffer-id ↔ packed `(view<<30)|index`, both directions), the per-view file
enumerators `GETOPENFILENAMES{PRIMARY,SECOND}_DEPRECATED` (and `GETNBOPENFILES` now honours the
`ALL`/`PRIMARY`/`SECOND` filter), the per-buffer properties `SETBUFFERLANGTYPE`,
`GET`/`SETBUFFERENCODING` (host encoding ↔ N++ `UniMode`), `GET`/`SETBUFFERFORMAT` (EOL mode) and
`GETTABCOLORID` (the host's 0..4 tab-colour palette slot, −1 if none), and the save/dirty/rename actions
`SAVECURRENTFILEAS` (save-as / save-a-copy), `SAVEFILE` (save an already-open file by path),
`MAKECURRENTBUFFERDIRTY` and `SETUNTITLEDNAME`. All cross-platform: buffer ids, per-view enumeration and
background-buffer inspection/save are portable — a **background (non-active) buffer** is read or written
through a doc-pointer-swap peek that saves and restores the active view's caret/scroll.

The **Phase-2** round added 15 bridge-internal getters/constants (no new host hooks): the NppExec-style
`GETCURRENTWORD` / `GETCURRENTLINESTR` / `GETFILENAMEATCURSOR`, `GETNPPFULLFILEPATH`,
`GETNPPSETTINGSDIRPATH` (the host user-data dir), `GETCURRENTNATIVELANGENCODING` (65001),
`GETWINDOWSVERSION` (real on Windows, `WV_UNKNOWN` off-Windows), `GETCURRENTCMDLINE` (Windows;
documented-empty off-Windows), `GETLANGUAGENAME` / `GETLANGUAGEDESC`, `GETBOOKMARKID` (=2),
`GETAPPDATAPLUGINSALLOWED`, `SETSMOOTHFONT`, `LAUNCHFINDINFILESDLG` (invokes the host's Find-in-Files
command; the dir/filter prefill args are dropped), and `RELOADBUFFERID`.

The Phase-1 round added: plugin **toolbar icons** (`ADDTOOLBARICON_DEPRECATED` /
`ADDTOOLBARICON_FORDARKMODE`), the **allocator family** (`ALLOCATESUPPORTED_DEPRECATED` /
`ALLOCATECMDID` / `ALLOCATEMARKER` / `ALLOCATEINDICATOR`), the **dark-mode queries**
(`ISDARKMODEENABLED`, `GETDARKMODECOLORS`, the editor default colours), **inter-plugin messaging**
(`MSGTOPLUGIN`), `SETCURRENTLANGTYPE` / `SETMENUITEMCHECK` / `RELOADFILE`, the **before-events**
(`NPPN_FILEBEFORESAVE` / `NPPN_FILEBEFOREOPEN` / `NPPN_FILEBEFORECLOSE`, plus
`NPPN_TBMODIFICATION`), and **save-event fidelity** — the id-carrying `NPPN_FILESAVED` (see the
compatibility notes below).

| Served | Stubbed / not yet |
|---|---|
| GETCURRENTSCINTILLA, GETNPPVERSION, GETCURRENTLANGTYPE, **GETCURRENTVIEW**, **GETBUFFERLANGTYPE** | richer `beNotified` (char-added, margin-click) |
| GETMENUHANDLE, MENUCOMMAND, **SWITCHTOFILE**, **SETSTATUSBAR**, **GETPLUGINHOMEPATH**, **GETCURRENTBUFFERID**, **GETFULLPATHFROMBUFFERID** | |
| GETNPPDIRECTORY, GETNPPFULLFILEPATH-ish, GETPLUGINSCONFIGDIR | |
| **GETFULLCURRENTPATH / GETCURRENTDIRECTORY / GETFILENAME / GETNAMEPART / GETEXTPART**, GETNBOPENFILES (ALL/PRIMARY/SECOND filter), **GETOPENFILENAMES{,PRIMARY,SECOND}** | |
| **DOOPEN**, **SAVECURRENTFILE**, **SAVEALLFILES**, **RELOADFILE**, **GETCURRENTLINE / GETCURRENTCOLUMN** | |
| **Phase 3 — per-view buffer model (`nib.documents` v5):** GETCURRENTDOCINDEX / ACTIVATEDOC / GETPOSFROMBUFFERID / GETBUFFERIDFROMPOS, SETBUFFERLANGTYPE, GET/SETBUFFERENCODING, GET/SETBUFFERFORMAT, GETTABCOLORID, SAVECURRENTFILEAS / SAVEFILE / MAKECURRENTBUFFERDIRTY / SETUNTITLEDNAME | |
| **Phase 4 — event fidelity:** ADDSCNMODIFIEDFLAGS (arms the host-side `nib.events` v3 modified-mask → `NPPN_GLOBALMODIFIED` with the real `modificationType`), GETSHORTCUTBYCMDID (`nib.keymap` v2 `effective_shortcut` → `ShortcutKey`) | |
| **Phase 5 — sessions / UI-chrome / lexer registry:** GET{NB,}SESSIONFILES / SAVE{,CURRENT}SESSION / LOADSESSION (`nib.session`, session XML by path — the written file is Notepad++-session-parseable), HIDETOOLBAR / ISTOOLBARHIDDEN / HIDESTATUSBAR / ISSTATUSBARHIDDEN / SHOWDOCLIST / ISDOCLISTSHOWN / SETLINENUMBERWIDTHMODE / GETLINENUMBERWIDTHMODE / ISAUTOINDENTON / GETCURRENTMACROSTATUS / GETTOOLBARICONSETCHOICE / GETNATIVELANGFILENAME (`nib.ui` v2), CREATELEXER / GETNBUSERLANG (`nib.lexer`, Lexilla — works for recompiled plugins on every OS) | HIDEMENU / ISMENUHIDDEN — the menubar is a **portable documented no-op** (a macOS global menubar can't be hidden, so it is a no-op identically on every OS rather than a per-platform detach hack); GETNATIVELANGFILENAME reports the active UI-locale name (wxNote localises via gettext, so there is no nativeLang xml file) |
| **DMMREGASDCKDLG / DMMSHOW / DMMHIDE / DMMUPDATEDISPINFO** (docking) | |
| **SETCURRENTLANGTYPE** (via the host's frozen `IDM_LANG_*` Language-menu commands), **SETMENUITEMCHECK** (via `nib.ui`) | |
| **ISDARKMODEENABLED / GETDARKMODECOLORS** (host palette laid into the `NppDarkMode::Colors` ABI), **GETEDITORDEFAULTFOREGROUNDCOLOR / -BACKGROUNDCOLOR** | |
| **ADDTOOLBARICON_DEPRECATED / ADDTOOLBARICON_FORDARKMODE** (Windows: HBITMAP/HICON → RGBA → `nib.toolbar`; POSIX: documented no-op TRUE) | |
| **ALLOCATESUPPORTED_DEPRECATED / ALLOCATECMDID / ALLOCATEMARKER / ALLOCATEINDICATOR** (via `nib.alloc`; the pre-rename numeric values shipped binaries send are the same numbers) | |
| **MSGTOPLUGIN** (delivered to the dest plugin's `messageProc`, matched by module filename or stem) | |
| `beNotified` for text-changed / selection / save (**id-carrying NPPN_FILESAVED** + NPPN_FILEBEFORESAVE) / **buffer-activated** (NPPN_BUFFERACTIVATED) / **file-opened / file-closed** (NPPN_FILEBEFOREOPEN / NPPN_FILEOPENED / NPPN_FILEBEFORECLOSE / NPPN_FILECLOSED) / **event fidelity** (NPPN_GLOBALMODIFIED masked, NPPN_LANGCHANGED, NPPN_WORDSTYLESUPDATED, NPPN_SHORTCUTREMAPPED) / **long-tail file lifecycle** (NPPN_FILEBEFORELOAD, NPPN_READONLYCHANGED, NPPN_DOCORDERCHANGED, NPPN_SNAPSHOTDIRTYFILELOADED, NPPN_FILEBEFORERENAME / NPPN_FILERENAMED / NPPN_FILERENAMECANCEL, NPPN_FILEBEFOREDELETE / NPPN_FILEDELETED / NPPN_FILEDELETEFAILED, NPPN_CMDLINEPLUGINMSG) / **app lifecycle** (NPPN_READY, NPPN_TBMODIFICATION, NPPN_BEFORESHUTDOWN / NPPN_CANCELSHUTDOWN, NPPN_SHUTDOWN) | |

Coverage grows additively as the `nib.*` interfaces grow (each new capability is a few lines here).
Note: the path family lives in the `RUNCOMMAND_USER` (`WM_USER+3000`) range, so the frame subclass
forwards everything `>= WM_USER+1000` (no upper bound).

### Documented no-op / interim stubs (the other 57 — answered, never counted as served)

From Phase 2 on, no `NPPM_*` is silently dropped. These return the honest constant below; several are
*ABI-correct* answers (marked †), not placeholders. They are grouped by *why* the host answers this way,
and each returns the **same value on every platform** — no fragile Windows-only paths (a message that is
inherently Win32, e.g. one that takes an `HWND`, is a documented no-op returning that same value
everywhere, rather than a Windows-only hack).

| Group | Messages | Return |
|---|---|---|
| Deprecated / void in this host † | `DESTROYSCINTILLAHANDLE_DEPRECATED`, `DOCLISTDISABLEEXTCOLUMN`, `DOCLISTDISABLEPATHCOLUMN`, `DISABLEAUTOUPDATE` | `TRUE` |
| Always-UTF-8 host † | `ENCODESCI`, `DECODESCI` | UTF-8 UniMode (1) |
| | `GETSETTINGSONCLOUDPATH` (no cloud), `GETENABLETHEMETEXTUREFUNC_DEPRECATED`, `GETEXTERNALLEXERAUTOINDENTMODE` / `SET…` (Scintillua replaces external lexers) | empty / `0` / `FALSE` |
| **Annex W — HWND-bound, intentionally unsupported (portable no-op)** — these take/return real payload `HWND`s a recompiled plugin can never produce, and N++ implements them with native subclassing / `IsDialogMessage` relays / `SetWindowLongPtr`. No Windows-only path: each answers the **same honest value on every OS**. | `MODELESSDIALOG` — *accept + echo the passed handle* (`lParam`), matching N++'s own return for `ADD`/`REMOVE`; no `IsDialogMessage` relay, so keyboard nav falls to the OS default pump | the passed handle |
| | `DMMVIEWOTHERTAB` (can't switch a docked container's tab), `DMMGETPLUGINHWNDBYNAME` (no plugin dialog HWND to return) | `NULL` |
| | `SETEDITORBORDEREDGE` (can't set `WS_EX_CLIENTEDGE` portably — not applied) | `FALSE` |
| Hard tail — not built | `CREATESCINTILLAHANDLE` (`NULL`), `HIDETABBAR` / `ISTABBARHIDDEN` (`FALSE`), `DARKMODESUBCLASSANDTHEME` (`0`, plugin dialogs stay light) | as noted |
| Phase 5 — deliberately not implemented | `REMOVESHORTCUTBYCMDID` (would mutate the user's *active* keymap scheme in place, which the `nib.keymap` contract forbids a plugin from doing; plugin commands carry no removable binding anyway), `TRIGGERTABBARCONTEXTMENU` (popping a context menu needs a real right-click position a message can't supply) | `FALSE` |

`RELOADBUFFERID` (and Phase-1's `RELOADFILE`) reload through the non-deduping `nib.documents` `open()`,
so reloading an already-open file leaves a duplicate tab; a switch-if-already-open seam is future work.

`GETCURRENTVIEW` / `GETCURRENTSCINTILLA` report the focused pane (0=main, 1=sub) via `nib.documents` v3, so
view-aware plugins target the right editor in a split. **Partial**: `GETBUFFERLANGTYPE` / `GETCURRENTLANGTYPE`
report the language **by file extension** (a Language-menu override isn't reflected), and `SWITCHTOFILE`
*opens* the path rather than switching to an already-open tab (needs an open-tab lookup in the host).

## Compatibility notes (documented divergences from real Notepad++)

The first two are **permanent** — settled host design decisions, not gaps waiting on a later phase.
Code to the documented behaviour.

* **Permanent — `NPPN_READY` fires before session restore** (N++: after). The host's startup order
  (frame up → plugins loaded → READY → session restored) is deliberate; files restored by the session
  arrive as ordinary `NPPN_FILEOPENED` / `NPPN_BUFFERACTIVATED` *after* READY, so a plugin must not
  assume READY means "the session's files are already open".
* **Permanent — `NPPN_SHUTDOWN` is deferred past the frame's own close/teardown dispatch**
  (`CallAfter`) — required by the plugin-unload ordering fix (a reentrant `RemoveWindowSubclass` from
  inside the subclassed window's own dispatch only *defers* the removal). Plugins still receive it
  exactly once, while fully mapped, just later in shutdown than real N++ delivers it.
* **`NPPN_TBMODIFICATION` fires immediately *after* `NPPN_READY`** (N++ fires it while rebuilding the
  toolbar, before READY). It fires on **every OS** — see the POSIX toolbar policy next.
* **POSIX toolbar policy — off-Windows, `NPPM_ADDTOOLBARICON*` is a deliberate no-op that answers
  `TRUE`.** The message's payload is GDI (`HBITMAP`/`HICON`), which a plugin recompiled for
  Linux/macOS cannot produce, so the bridge reports success without adding a button rather than
  failing — init code hanging off `NPPN_TBMODIFICATION` (or treating `FALSE` as fatal) keeps running
  unchanged. On Windows the same messages add a real button (native image → RGBA → `nib.toolbar`).
  The portable way to a real button on every OS is the host's own `nib.toolbar/1` interface, which
  speaks RGBA pixels instead of GDI handles.
* **`NPPN_FILESAVED` reports real disk writes** with the *written* buffer's id: no false fire on
  undo-to-savepoint, and during Save All each background write reports its own buffer id (on hosts
  without `nib.events` v2 the bridge falls back to the old savepoint-derived, active-id emission).
  `NPPN_FILEBEFORECLOSE` precedes `NPPN_FILECLOSED` with the same still-valid buffer id on every close path.
* **`FuncItem::_cmdID` values are host-granted dynamic ids** (the `nib.alloc` 64000+ pool, assigned at
  load like N++'s `ID_PLUGINS_CMD+n`). They dispatch through the host's wx-event command path — never a
  raw 16-bit `WM_COMMAND` — and unresolved dynamic ids (`NPPM_ALLOCATECMDID`) are relayed to every
  plugin's `messageProc` as `WM_COMMAND`, as in N++.
* **`NPPM_SETMENUITEMCHECK` answers honestly**: the host's Extensions-menu entries are not checkable
  today, so checking a plugin's own item returns `FALSE` (nothing is silently pretended).
* **`NPPM_RELOADFILE`** ignores the "with alert" flag (the host reloads silently); toolbar icons pick the
  **dark icon variant when the host chrome runs dark** (chrome darkness is fixed per process, so there is
  no `NPPN_DARKMODECHANGED` to observe yet).
* **`NPPM_SETBUFFERENCODING` is save-to-apply** (N++: immediate re-decode). The host records the new
  on-disk encoding on the buffer and writes it on the next save, rather than re-decoding the in-memory
  bytes there and then — so `GETBUFFERENCODING` reflects the pending choice immediately, but the file's
  bytes change only when it is saved. `UniMode` maps onto the host's own encoding enum (UTF-8 ± BOM,
  UTF-16 LE/BE with BOM, ANSI); a codepage buffer reads back as `uni8Bit`, and the no-BOM UTF-16 UniModes
  are written *with* a BOM. `NPPM_SETBUFFERFORMAT` converts the buffer's existing line endings and marks
  it dirty. A **background (non-active) buffer**'s encoding/EOL/format read or set — and a background
  `NPPM_SAVEFILE` — go through a doc-pointer-swap peek that restores the active view's caret and scroll,
  so the visible views are never disturbed.
* **`NPPM_ADDSCNMODIFIEDFLAGS` is a single global accumulator** (as in N++): each call ORs its flags into
  one shared mask and pushes the union to the host, which fires the raw-modification event *only* for
  modifications whose `modificationType` intersects that mask (the perf gate — with no opt-in, the
  high-volume `SCN_MODIFIED` path never crosses the ABI). Matching modifications are surfaced to every
  loaded plugin as **`NPPN_GLOBALMODIFIED`** carrying the real `modificationType` / position / length /
  linesAdded — this host routes the masked Scintilla-modification payload under that notification code.
* **`NPPN_BEFORESHUTDOWN` / `NPPN_CANCELSHUTDOWN`** bracket a close *attempt*: BEFORESHUTDOWN fires when a
  close is requested, then either the close proceeds (→ the deferred `NPPN_SHUTDOWN`) or an unsaved-changes
  prompt is cancelled and CANCELSHUTDOWN fires while the app stays open. `NPPM_GETSHORTCUTBYCMDID` reports
  a command's effective (primary, plain) accelerator via the `nib.keymap` v2 read hook; letters/digits map
  straight to their virtual-key code, common named keys via a small table, and an unbound command answers
  `FALSE` leaving the caller's `ShortcutKey` untouched.
* **Long-tail file lifecycle (Phase 6) carries the buffer id, with two documented reductions.** The rename
  triple (`FILEBEFORERENAME` → `FILERENAMED` / `FILERENAMECANCEL`) and the delete triple (`FILEBEFOREDELETE`
  → `FILEDELETED` / `FILEDELETEFAILED`) bracket the operation, and `FILEDELETED` fires *before* the tab is
  torn down so the id/path stay resolvable. `NPPN_FILEBEFORELOAD` carries id 0 (the buffer is not created
  yet, as in N++). The two reductions: `NPPN_READONLYCHANGED` and `NPPN_DOCORDERCHANGED` do **not** carry
  N++'s secondary `nmhdr.hwndFrom` field (the `DOCSTATUS_*` flags / the new tab index) — the portable event
  model has no place for it; a plugin reads the current state via `NPPM` (`SCI_GETREADONLY`,
  `GETCURRENTDOCINDEX`) instead. `NPPN_READONLYCHANGED` fires on the read-only *toggle* (not on every
  dirty-flag transition). `NPPN_DOCORDERCHANGED` fires once per reorder (a finished tab drag or a Sort).
* **`-pluginMessage="…"` → `NPPN_CMDLINEPLUGINMSG`.** The host accepts Notepad++'s single-dash
  `-pluginMessage=` (and the wx-idiomatic `--pluginMessage`) and, once plugins are loaded and the startup
  files are in, delivers the message to every plugin with the text in `nmhdr.hwndFrom` (a `const wchar_t*`)
  and `idFrom == 0`, exactly as N++ does. `--safe` (no plugins) makes it inert.

## Build

Built by the top-level CMake on **every OS** (`add_subdirectory(packages/npp-bridge)`, unconditional),
output to `<build>/bin/nib/npp_bridge.dll` on Windows (`.so`/`.dylib` elsewhere). Only Windows hosts a
real *precompiled* N++ plugin binary; off-Windows the build compiles the portable dispatch core plus
`libnpp_shim` + `npp_example_plugin` (the recompiled-plugin path). On Windows it links `comctl32` (for
`SetWindowSubclass`); off-Windows it links `${CMAKE_DL_LIBS}` (libdl, for `dlopen`) instead. Either way it
does **not** link wxWidgets — it is a thin, self-contained translation layer.
