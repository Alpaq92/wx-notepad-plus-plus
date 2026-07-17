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

| Served | Stubbed / not yet |
|---|---|
| GETCURRENTSCINTILLA, GETNPPVERSION, GETCURRENTLANGTYPE, **GETCURRENTVIEW**, **GETBUFFERLANGTYPE** | ACTIVATEDOC (doc tracking) |
| GETMENUHANDLE, MENUCOMMAND, **SWITCHTOFILE**, **SETSTATUSBAR**, **GETPLUGINHOMEPATH**, **GETCURRENTBUFFERID**, **GETFULLPATHFROMBUFFERID** | |
| GETNPPDIRECTORY, GETNPPFULLFILEPATH-ish, GETPLUGINSCONFIGDIR | richer `beNotified` (char-added, margin-click) |
| **GETFULLCURRENTPATH / GETCURRENTDIRECTORY / GETFILENAME / GETNAMEPART / GETEXTPART**, GETNBOPENFILES, **GETOPENFILENAMES** | RELOADFILE, MAKECURRENTBUFFERDIRTY |
| **DOOPEN**, **SAVECURRENTFILE**, **SAVEALLFILES**, **GETCURRENTLINE / GETCURRENTCOLUMN** | |
| **DMMREGASDCKDLG / DMMSHOW / DMMHIDE / DMMUPDATEDISPINFO** (docking) | |
| `beNotified` for text-changed / selection / save (NPPN_FILESAVED) / **buffer-activated** (NPPN_BUFFERACTIVATED) / **file-opened / file-closed** (NPPN_FILEOPENED / NPPN_FILECLOSED) / **app lifecycle** (NPPN_READY, NPPN_SHUTDOWN) | |

Stubbed messages fall through and return 0; coverage grows additively as the `nib.*` interfaces grow
(each new capability is a few lines here). Note: the path family lives in the `RUNCOMMAND_USER`
(`WM_USER+3000`) range, so the frame subclass forwards everything `>= WM_USER+1000` (no upper bound).

`GETCURRENTVIEW` / `GETCURRENTSCINTILLA` report the focused pane (0=main, 1=sub) via `nib.documents` v3, so
view-aware plugins target the right editor in a split. **Partial**: `GETBUFFERLANGTYPE` / `GETCURRENTLANGTYPE`
report the language **by file extension** (a Language-menu override isn't reflected), and `SWITCHTOFILE`
*opens* the path rather than switching to an already-open tab (needs an open-tab lookup in the host).

## Build

Built by the top-level CMake on **every OS** (`add_subdirectory(packages/npp-bridge)`, unconditional),
output to `<build>/bin/nib/npp_bridge.dll` on Windows (`.so`/`.dylib` elsewhere). Only Windows hosts a
real *precompiled* N++ plugin binary; off-Windows the build compiles the portable dispatch core plus
`libnpp_shim` + `npp_example_plugin` (the recompiled-plugin path). On Windows it links `comctl32` (for
`SetWindowSubclass`); off-Windows it links `${CMAKE_DL_LIBS}` (libdl, for `dlopen`) instead. Either way it
does **not** link wxWidgets — it is a thin, self-contained translation layer.
