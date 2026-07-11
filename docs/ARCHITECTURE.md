# wxNote Architecture

How the editor is put together. Product identity: CMake project `wxNote`,
executable/package name `wxnote`, internal code prefix `wxn` (macros, C++
identifiers, IPC/config strings). Core is Apache-2.0; the optional
`packages/npp-bridge/` module is GPL-3.0-or-later (see
[`LICENSING.md`](../LICENSING.md)).

## The one-picture version

```
                    ┌────────────────────────────────────────────────┐
 all platforms ──▶  │  wxnote  (wxWidgets + wxStyledTextCtrl core)   │  ◀── Apache-2.0
                    │  + the Nib plugin API  (include/nib/nib.h)     │
                    └───────────────┬───────────────┬────────────────┘
                       loads (dlopen)│               │ loads — Windows only
                    ┌───────────────▼──────┐   ┌─────▼──────────────────────────┐
 all platforms ──▶  │  native Nib plugins  │   │  npp-bridge  (GPL, Win-only)   │
                    │  <exe>/nib/*.dll|so| │   │  loads real Notepad++ plugin   │
                    │  *.dylib             │   │  DLLs, translates NPPM_* ⇄ Nib │
                    └──────────────────────┘   └────────────────────────────────┘
```

One portable core and one portable plugin API on every OS; the Notepad++
binary-compatibility layer is a separate, optional, Windows-only, GPL module
that is itself just another Nib plugin.

## Source layout

| Path | Role |
|---|---|
| `src/main.cpp` (~9,000 lines) | The application: app object, frame, editor views, all panels and dialogs, theme engine, dispatcher, plugin host. Apart from the two per-platform shims and the sample plugin below, everything else in `src/` is a header it includes. |
| `src/menu_model.h`, `src/menu_builder.h`, `src/menu_data_*.h`, `src/menu_labels_*.h` | The data-driven menu engine (below). |
| `src/udl.h`, `src/udl_lexer.h` | User-Defined Languages: data model + XML I/O, and the live container-lexer styling/folding engine. |
| `src/terminal_panel.h` | The integrated multi-tab terminal panel and the per-platform shell/terminal-app detection. |
| `src/gtk_native.cpp`, `src/macos_native.mm` | Small per-platform native shims for things wxWidgets doesn't expose (GTK scrollbar theming; macOS title-bar/traffic-light work). Compiled only on their platform, gated in CMake. |
| `src/app_icon_svg.h` | The app icon as an embedded SVG string, rendered at runtime via `wxBitmapBundle::FromSVG`. |
| `src/command_ids.h` | The core's own, authoritative command-id table. Values are frozen (static_asserts) so they stay identical to the plugin ABI's ids and npp-bridge's command passthrough dispatches correctly. |
| `include/nib/nib.h` | The Nib plugin API — an original, stable C ABI (below). |
| `include/npp-compat/` | Clean-room redeclarations of the Notepad++ plugin-ABI facts (ids, struct layouts). Consumed only by `packages/npp-bridge/` and `packages/test_plugin/` — the core includes nothing from here. |
| `packages/npp-bridge/` | The optional GPL Notepad++ binary-plugin bridge (Windows-only Nib plugin). |
| `packages/test_plugin/` | A minimal real-ABI Notepad++ plugin used as the bridge's regression fixture (Windows-only, GPL). |
| `src/plugins/nib_test_plugin/` | A cross-platform reference/smoke-test Nib plugin (Apache-2.0). |
| `third_party/` | Vendored: Lexilla (lexers, HPND), Scintilla headers (HPND), wxBorderlessFrame (wxWindows Licence). |
| `resources/` | Icons (3 sets), themes, default styler, fonts, locale catalogs, app icon, `app.rc`. |
| `installer/`, `.github/workflows/` | Per-platform packaging (NSIS / AppImage+deb+rpm+flatpak / dmg) and the CI/release pipelines. |
| `site/` | The project website (GitHub Pages). |

## Application core

`WxnApp : wxApp` owns startup: a hidden `--elevated-write` UAC helper mode
(Windows), the one-time settings migration from the legacy config key into
`"wxNote"`, command-line parsing (`-g/--goto`, `-e/--encoding`,
`-n/--new-instance`, `-r/--reuse-instance`, files), the single-instance
handoff, font/locale setup, and frame construction.

The main window is a template, `WxnShellFrameT<FB>`, instantiated two ways:

- `WxnShellFrameT<wxFrame>` — native OS chrome;
- `WxnShellFrameT<wxBorderlessFrame>` (only where `WXN_HAS_BORDERLESS` is
  defined: Windows and Linux) — the integrated top bar, where the menu
  buttons, toolbar, and custom min/max/close controls live in one row. On
  macOS the same integrated look is achieved differently: the frame stays a
  `wxFrame` and `src/macos_native.mm` makes the native title bar transparent
  and re-centres the traffic lights into the toolbar row.

Which one is built is a restart-to-apply preference; `if constexpr` on the
base type gates the integrated-bar code paths.

## Editor model: two views, one control each

An editor "view" (`ViewPane`) is a tab strip (`wxAuiNotebook`) plus **one**
persistent `wxStyledTextCtrl`. Each tab (`EditorPage`) holds a Scintilla
*document pointer* and per-tab state (path, dirty flag, language, encoding,
tab colour, monitoring state…), not its own editor control — activating a tab
swaps the document into the view's single control via `SCI_SETDOCPOINTER`.

There are two views, MAIN and SUB, in a splitter: Move/Clone to Other View
populates the second; it collapses when emptied. `m_tabs`/`m_stc` are aliases
re-pointed at whichever view has focus, so most code follows focus without
caring which view it's in; cross-view operations iterate `allPages()`.

## Panels

Everything dockable is a `wxAuiManager` pane around the central splitter:
Document Map (a second Scintilla sharing the active document), Function List
(regex-derived symbol tree for C/C++, Python, JS/TS, Java, C#, Go, Rust,
Lua), Document List, Clipboard History, Character Panel, Project Panel
(N++-compatible `<NotepadPlus>` workspace XML), Folder as Workspace, the
incremental-search bar, Find-in-Files results, the integrated Terminal
(lazy-created, multi-tab, per-platform shell detection), and — in integrated
mode — the title bar and toolbar rows themselves. Nib plugins can register
additional text panels (and, on Windows, dock native HWNDs).

## Command dispatch

One handler receives every menu/toolbar command and switches on the id.
Standard commands use the numeric `IDM_*` ids from the core's own
`src/command_ids.h` — an original table whose *values* are frozen identical
to the plugin ABI's ids (static_asserts enforce it), so commands invoked by
bridged Notepad++ plugins land correctly. Because those ids exceed 32767 and
Win32's WM_COMMAND carries a 16-bit value, the handler reads the id as
unsigned 16-bit (a sign-extension trap), and an `MSWWindowProc` override
re-dispatches native toolbar clicks that wx's signed-short lookup would
otherwise drop. App-local ids live at 60000+; Nib plugin commands at 63000+.

## The menu system

Menus are **data, not code**: each top-level menu is a `static const
MenuItemDef[]` table (`menu_data_*.h`) of `{kind, id, label-getter,
symbolic-name}` rows; labels are one-line functions containing real `_()`
calls (`menu_labels_*.h`) so gettext extraction keeps working. A ~100-line
builder walks the tables into a `wxMenuBar` once at startup and records a
`MenuRegistry` of symbolic names → menus and `DynamicSlot` insertion points
(Recent Files, the Language A–Z tree, UDL entries, macros, Open-Containing-
Folder tools, plugin commands) — no lookups by translated label text
anywhere. The hierarchy itself is an original 11-menu structure (File, Edit,
Selection, Go, View, Document, Automation, Extensions, Settings, Window,
Help) designed from research across five editors; it does not mirror
Notepad++'s.

## Theming

`WxnTheme` parses the `<NotepadPlus>` theme-XML *format* — Notepad++'s own
schema, kept so real N++ theme files load unmodified. Of the 27 themes
shipped, 14 are wxNote's own regenerated data (Apache-2.0, including both
defaults) and 13 are kept third-party themes under their original authors'
licenses (see [`docs/CREDITS.md`](CREDITS.md)). Light default is
`stylers.model.xml`; dark default is `themes/DarkModeDefault.xml`. App-wide dark/light follows the OS by default (System /
Dark / Light, restart-to-apply, relaunching through a session save). The
Style Configurator edits the active theme XML in place.

## Persistence

- **Settings** — `wxConfig` under app name `"wxNote"` (registry on Windows,
  dotfile elsewhere), with a one-time migration from the pre-rename key.
- **Session** — reopened automatically from config on launch; File > Save/
  Load Session additionally reads/writes Notepad++-style
  `<NotepadPlus><Session>` XML (caret, scroll, bookmarks included).
- **Recovery** — unsaved changes discarded at exit are backed up to
  `<user-data-dir>/RecoveryBackups/` and offered back on next launch.
- **User data vs. install dir** — everything the app *writes* (recovery,
  UDLs, edited `contextMenu.xml`) goes to `wxStandardPaths::GetUserDataDir()`;
  the install dir (Program Files, `/opt/wxnote`, the `.app` bundle) is
  treated as read-only and holds only shipped resources.

## i18n

`wxLocale` with catalog `wxn`, loaded from `<exe>/locale/`. Eight languages
ship (pl, de, fr, es, ru, ja, zh, ko); English is the source text.
`resources/locale/po2mo.py` is a self-contained `.po` → `.mo` compiler (the
GNU gettext *format*, no gettext tooling dependency); compiled catalogs are
committed and deployed by the build's resource-copy step.

## User-Defined Languages

`udl.h` models a UDL and round-trips Notepad++'s real `userDefineLang.xml`
schema field-for-field, so definitions interchange with real N++ installs
(one `<name>.xml` per language under the user-data dir). Since no Lexilla
lexer can exist for a user-defined language, `udl_lexer.h` makes the app
itself the lexer: buffers in a UDL use Scintilla's container-lexing mode, and
the app styles/folds on `wxEVT_STC_STYLENEEDED`.

## Plugins: Nib, and the Notepad++ bridge

**Nib** (`include/nib/nib.h`) is wxNote's own plugin API — an original,
cross-platform, stable C ABI that borrows nothing from Notepad++ (no
`NPPM_*`, `FuncItem`, `NppData`, `HWND` in the contract). A plugin is a
shared library exporting one entry point; it queries the host for versioned,
length-prefixed interface tables by string id (`nib.host`, `nib.editor`,
`nib.documents`, `nib.commands`, `nib.events`, `nib.panels`, and the
capability-gated, Windows-only `nib.win32`). Interfaces grow additively;
plugins load from `<exe>/nib/`.

**npp-bridge** (`packages/npp-bridge/`, GPL, Windows-only) makes real
compiled Notepad++ plugins work: it is itself a Nib plugin that uses
`nib.win32` to obtain native handles, rebuilds the `NppData` environment,
loads `<exe>/plugins/<Name>/<Name>.dll` via `LoadLibrary`, surfaces each
`FuncItem` as a Nib command, answers `NPPM_*` messages by translating them to
Nib calls, and forwards Nib events back as `SCNotification`s. The dependency
rule that keeps the license boundary clean: the core knows nothing about the
N++ ABI — it includes nothing from `include/npp-compat/`, and its own
`src/command_ids.h` merely keeps its id *values* aligned with the ABI's; all
N++-derived knowledge flows downward into the bridge, and new plugin needs
are met by adding *generic* Nib capabilities the bridge then adapts. The
bridge is slated to move into its own repository.

## Platform shims

- `src/gtk_native.cpp` (Linux) — installs a maximum-priority GTK CSS provider
  that neutral-themes scrollbars and scrolled-window decoration nodes (so
  desktop accent themes don't paint over the editor edge) and compacts
  toolbar metrics. Notable constraint: GTK3's CSS parser has no `!important`;
  provider priority is the only lever.
- `src/macos_native.mm` (macOS) — hides the native window title, implements
  the integrated top bar (transparent titlebar + full-size content view +
  re-centred traffic lights, re-applied after resize/restore), and starts
  native window drags from the toolbar row.

## Build & packaging

CMake fetches and statically builds wxWidgets 3.3.1 at configure time, builds
vendored Lexilla and (Windows/Linux) wxBorderlessFrame as static libs, and
produces the single `wxnote` executable plus the plugin modules. A POST_BUILD
step copies all runtime resources (icons, themes, fonts, locale, styler,
context menu) next to the executable — the layout every installer then ships
as-is (no FHS split). CI builds and packages on all three OSes — with
separate x64 and ARM64 legs for Windows and Linux (native arm64 runners; the
packaging scripts detect the build host's architecture themselves) — for
every pull request and for source-affecting pushes to master; pushing a `v*`
tag assembles a GitHub Release with the NSIS installers (x64 + ARM64), the
four Linux package formats in both x86_64 and aarch64, and both macOS
`.dmg`s.
