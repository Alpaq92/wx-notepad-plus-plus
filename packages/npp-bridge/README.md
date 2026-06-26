# npp-bridge — the optional Notepad++ binary-plugin compatibility bridge

`npp-bridge` lets **wxNotepad++ host real Notepad++ plugin DLLs** on Windows. It is itself a
[Nib](../../include/nib/nib.h) plugin: the core loads it from `<exe>/nib/` like any other plugin, and it
reaches the host's native handles through the Windows-only `nib.win32` capability to rebuild the
Notepad++ `NppData` environment a binary plugin expects.

## Why it's a separate, GPL module

This module reproduces Notepad++'s plugin ABI (`NPPM_*` message numbers, `FuncItem`, `NppData`,
`tTbData`, …), so it is **GPL-3.0-or-later**. The wxNotepad++ **core depends on none of it** — it is
loaded only if present. Keeping the ABI reproduction confined here is exactly what lets the core stay
permissive-ready (see [`docs/FUTURE_PLANS.md`](../../docs/FUTURE_PLANS.md)). The core talks only the
permissive `nib.*` API; this bridge is the one place the two worlds meet.

## What it does

1. **Loads** each `<exe>/plugins/<Name>/<Name>.dll` (`LoadLibrary` + `setInfo`/`getFuncsArray`/`getName`).
2. **Surfaces** every `FuncItem` as a Nib command, so the plugin's commands appear in the Plugins menu.
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
| GETCURRENTSCINTILLA, GETNPPVERSION, GETCURRENTLANGTYPE, **GETCURRENTVIEW**, **GETBUFFERLANGTYPE** | GETCURRENTBUFFERID (per-buffer tracking) |
| GETMENUHANDLE, MENUCOMMAND, **SWITCHTOFILE**, **SETSTATUSBAR**, **GETPLUGINHOMEPATH** | ACTIVATEDOC (doc tracking) |
| GETNPPDIRECTORY, GETNPPFULLFILEPATH-ish, GETPLUGINSCONFIGDIR | richer `beNotified` (char-added, margin-click, buffer-activated) |
| **GETFULLCURRENTPATH / GETCURRENTDIRECTORY / GETFILENAME / GETNAMEPART / GETEXTPART**, GETNBOPENFILES | RELOADFILE, MAKECURRENTBUFFERDIRTY |
| **DOOPEN**, **SAVECURRENTFILE** | |
| **DMMREGASDCKDLG / DMMSHOW / DMMHIDE / DMMUPDATEDISPINFO** (docking) | |
| `beNotified` for text-changed / selection / save | |

Stubbed messages fall through and return 0; coverage grows additively as the `nib.*` interfaces grow
(each new capability is a few lines here). Note: the path family lives in the `RUNCOMMAND_USER`
(`WM_USER+3000`) range, so the frame subclass forwards everything `>= WM_USER+1000` (no upper bound).

## Build

Built by the top-level CMake on Windows (`if(WIN32) add_subdirectory(packages/npp-bridge)`), output to
`<build>/bin/nib/npp_bridge.dll`. It links only `comctl32` (for `SetWindowSubclass`); it does **not**
link wxWidgets — it is a thin, self-contained translation layer.
