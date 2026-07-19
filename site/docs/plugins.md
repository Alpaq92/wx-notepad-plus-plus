# Plugins

wxNote has **its own** plugin API — codename **Nib** — and, separately, an **optional** compatibility
bridge for Notepad++ binary plugins. The split is deliberate and matters for both licensing and
portability.

| | Nib | npp-bridge |
| --- | --- | --- |
| Licence | Apache-2.0, same as the core | GPL-3.0-or-later |
| Platforms | Windows, Linux, macOS | precompiled plugin binaries: **Windows only**; recompiled plugins: all platforms |
| Part of the core? | yes — the API the core speaks | no — an optional module, loaded only if present |
| Loaded from | `<exe>/nib/` | itself a Nib plugin in `<exe>/nib/`; it then loads N++ plugins |

The core talks **only** the `nib.*` API. Keeping the Notepad++ ABI reproduction confined to the bridge
is exactly what lets the core stay permissively licensed.

## Managing plugins

- **Extensions&nbsp;&rsaquo; Open Plugins Folder…** opens the plugin directory, creating it on demand.
  On Windows that is `<exe>/plugins`; elsewhere it is `plugins` inside the per-user data directory. If
  neither can be created it falls back to `<exe>/nib`.
- **Settings&nbsp;&rsaquo; Import&nbsp;&rsaquo; Import plugin(s)…** copies plugin binaries into place.
- Commands registered by loaded plugins appear in the **Extensions** menu.
- Launching with `--safe` starts the editor **with no plugins loaded at all** — the first thing to try
  if a plugin is misbehaving. See [Command Line](command-line.md).

## The Nib API

Nib is a stable **C ABI**. It borrows nothing from Notepad++: no `NPPM_*` numbers, no `FuncItem`, no
`SCNotification`, no `WM_USER`, and no platform handles in the contract. Current ABI version is **1.0**;
the host accepts any plugin whose *major* version matches.

A plugin is a shared library (`.dll` / `.so` / `.dylib`) placed in `<exe>/nib/`, exporting exactly one
symbol:

```c
extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap*);
```

It returns a lifecycle vtable with an `id`, an `activate(host, query)` and an optional
`deactivate(host)`. Everything else is negotiated at runtime: the plugin asks the host for a capability
by **stable string id + minimum version**, and gets back a typed function table — or `NULL` if the host
cannot satisfy it.

```c
const NibEditorApi* ed = query(host, NIB_IFACE_EDITOR, 1);
if (ed) ed->replace_selection(host, "// hello from a plugin\n");
```

Because unknown ids simply return `NULL`, there is no global number space to collide on, and new
interfaces can be added without breaking existing plugins. Every struct is length-prefixed and every
interface independently versioned, so structs grow only at the end.

### Capabilities

| Interface | What it gives you |
| --- | --- |
| `nib.host/1` | product name, host ABI version |
| `nib.editor/1` | length, insert text, replace selection, selection bounds, read a range |
| `nib.documents/1` | document count, active path, open, save; **v2** buffer ids and path-from-id; **v3** active view (main/sub); **v4** enumerate every open document |
| `nib.commands/1` | register a command (surfaced in the menu); **v2** invoke one of the host's own commands by id |
| `nib.events/1` | subscribe to text changed, selection changed, document saved / activated / opened / closed; **v2** id-carrying save events that fire per real disk write (before and after), plus before-open |
| `nib.toolbar/1` | add a main-toolbar button that fires a command id — the icon crosses as portable RGBA pixels, no native image types |
| `nib.ui/1` | host chrome state: check/uncheck a menu item, ask whether the chrome runs dark, read the host's dark palette |
| `nib.alloc/1` | process-lifetime grants of command-id / marker / indicator ranges, so plugins never collide — plus a sink for fired allocated ids |
| `nib.langdef/1` | register a language as a Scintillua Lua lexer — see [Languages &amp; Syntax](languages.md) |
| `nib.keymap/1` | contribute keyboard bindings as a named, switchable keymap scheme: begin a scheme, bind by command id / symbolic name / Scintilla editor key, commit — see [Customizing Shortcuts](custom-shortcuts.md) |
| `nib.paths/1` | the per-user data directory |
| `nib.panels/1` | register a dockable text panel (bottom / left / right / top), set or append its text, show or hide it |
| `nib.sci/1` | raw Scintilla message passthrough (`view`: 0 = main, 1 = sub, −1 = active) — offered on **every** OS, since Scintilla message numbers are cross-platform |
| `nib.win32/1` | **Windows only** — native handles (frame, editors, plugins menu) and native-child docking. Returns `NULL` on Linux and macOS |

A reference plugin exercising commands, editor access, events and panels ships in the source tree at
`src/plugins/nib_test_plugin/`.

### Lifecycle note for plugin authors

`nib.events/1` has **no unsubscribe**. Subscribe from `activate()` and expect the host to clear
subscriptions before it unloads you.

## The Notepad++ bridge

`npp-bridge` is itself a Nib plugin. It reaches the host's native handles through the Windows-only
`nib.win32` capability, rebuilds the `NppData` environment a Notepad++ plugin expects, and translates
the `NPPM_*` messages that plugin sends into Nib calls.

- **Windows** — loads `<exe>/plugins/<Name>/<Name>.dll`, calls `setInfo` / `getFuncsArray` / `getName`,
  and surfaces every `FuncItem` as a command in the Extensions menu.
- **Linux and macOS** — hosting a *precompiled* Windows plugin binary is not possible. A plugin author
  can instead **recompile** against the bridge's GPL shim SDK, which routes the plugin's own
  `::SendMessage` calls into the bridge's portable dispatch core.

Message coverage is partial and grows additively — today roughly **44 of the 118 `NPPM_*` messages,
plus 10 `NPPN_*` notifications**. Served: the current-Scintilla / version / language / view queries,
menu handle and `MENUCOMMAND`, the path family (full path, directory, filename, name part, ext
part), open-file enumeration, `DOOPEN`, save current / save all / reload, current line and column,
status-bar text, plugin config and home paths, buffer ids, docking (`DMM*`), plugin toolbar icons,
the command-id / marker / indicator allocators, dark-mode and editor-colour queries, menu-item
checks, language setting, inter-plugin messaging (`MSGTOPLUGIN`), and notifications for text
changed, selection, save (before and after, carrying the written buffer's id), buffer activated,
file opened / closed (before and after), toolbar modification and app lifecycle. Unserved messages
fall through and return 0. The bridge's own README documents each divergence from real Notepad++ —
two are permanent by design (`NPPN_READY` fires before session restore; `NPPN_SHUTDOWN` is
deferred past the frame's close dispatch) — and the POSIX toolbar policy: off-Windows,
`NPPM_ADDTOOLBARICON*` is a documented no-op returning `TRUE`, since a recompiled plugin cannot
produce the message's native `HBITMAP`/`HICON` payload.

Two known partials worth planning around: language-type queries report the language **by file
extension** (a manual Language-menu override is not reflected), and `SWITCHTOFILE` *opens* the path
rather than switching to an already-open tab.

## Bundled optional modules

| Module | Licence | Purpose |
| --- | --- | --- |
| `npp-bridge` | GPL-3.0-or-later | host Notepad++ plugins |
| `udl-compat` | GPL-3.0-or-later | translate Notepad++ `userDefineLang.xml` into Scintillua lexers |
| `npp-shortcuts-compat` | GPL-3.0-or-later | import a Notepad++ `shortcuts.xml` as a keymap scheme — see [Customizing Shortcuts](custom-shortcuts.md#importing-notepad-shortcuts) |

All are optional. Removing any of them leaves the core fully functional — you lose only that module's
interoperability feature.
