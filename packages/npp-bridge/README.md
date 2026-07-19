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

**Roughly 44 of Notepad++'s 118 `NPPM_*` messages are served, plus 10 `NPPN_*` notifications.**
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
| GETCURRENTSCINTILLA, GETNPPVERSION, GETCURRENTLANGTYPE, **GETCURRENTVIEW**, **GETBUFFERLANGTYPE** | ACTIVATEDOC (doc tracking) |
| GETMENUHANDLE, MENUCOMMAND, **SWITCHTOFILE**, **SETSTATUSBAR**, **GETPLUGINHOMEPATH**, **GETCURRENTBUFFERID**, **GETFULLPATHFROMBUFFERID** | |
| GETNPPDIRECTORY, GETNPPFULLFILEPATH-ish, GETPLUGINSCONFIGDIR | richer `beNotified` (char-added, margin-click) |
| **GETFULLCURRENTPATH / GETCURRENTDIRECTORY / GETFILENAME / GETNAMEPART / GETEXTPART**, GETNBOPENFILES, **GETOPENFILENAMES** | MAKECURRENTBUFFERDIRTY |
| **DOOPEN**, **SAVECURRENTFILE**, **SAVEALLFILES**, **RELOADFILE**, **GETCURRENTLINE / GETCURRENTCOLUMN** | |
| **DMMREGASDCKDLG / DMMSHOW / DMMHIDE / DMMUPDATEDISPINFO** (docking) | |
| **SETCURRENTLANGTYPE** (via the host's frozen `IDM_LANG_*` Language-menu commands), **SETMENUITEMCHECK** (via `nib.ui`) | |
| **ISDARKMODEENABLED / GETDARKMODECOLORS** (host palette laid into the `NppDarkMode::Colors` ABI), **GETEDITORDEFAULTFOREGROUNDCOLOR / -BACKGROUNDCOLOR** | |
| **ADDTOOLBARICON_DEPRECATED / ADDTOOLBARICON_FORDARKMODE** (Windows: HBITMAP/HICON → RGBA → `nib.toolbar`; POSIX: documented no-op TRUE) | |
| **ALLOCATESUPPORTED_DEPRECATED / ALLOCATECMDID / ALLOCATEMARKER / ALLOCATEINDICATOR** (via `nib.alloc`; the pre-rename numeric values shipped binaries send are the same numbers) | |
| **MSGTOPLUGIN** (delivered to the dest plugin's `messageProc`, matched by module filename or stem) | |
| `beNotified` for text-changed / selection / save (**id-carrying NPPN_FILESAVED** + NPPN_FILEBEFORESAVE) / **buffer-activated** (NPPN_BUFFERACTIVATED) / **file-opened / file-closed** (NPPN_FILEBEFOREOPEN / NPPN_FILEOPENED / NPPN_FILEBEFORECLOSE / NPPN_FILECLOSED) / **app lifecycle** (NPPN_READY, NPPN_TBMODIFICATION, NPPN_SHUTDOWN) | |

Stubbed messages fall through and return 0; coverage grows additively as the `nib.*` interfaces grow
(each new capability is a few lines here). Note: the path family lives in the `RUNCOMMAND_USER`
(`WM_USER+3000`) range, so the frame subclass forwards everything `>= WM_USER+1000` (no upper bound).

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

## Build

Built by the top-level CMake on **every OS** (`add_subdirectory(packages/npp-bridge)`, unconditional),
output to `<build>/bin/nib/npp_bridge.dll` on Windows (`.so`/`.dylib` elsewhere). Only Windows hosts a
real *precompiled* N++ plugin binary; off-Windows the build compiles the portable dispatch core plus
`libnpp_shim` + `npp_example_plugin` (the recompiled-plugin path). On Windows it links `comctl32` (for
`SetWindowSubclass`); off-Windows it links `${CMAKE_DL_LIBS}` (libdl, for `dlopen`) instead. Either way it
does **not** link wxWidgets — it is a thin, self-contained translation layer.
