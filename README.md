# wxNotepad++

**An experimental, cross-platform (Windows / Linux / macOS) Notepad++-faithful editor built on [wxWidgets](https://www.wxwidgets.org/).**

> ⚠️ Experimental and unofficial. Not affiliated with or endorsed by the Notepad++ project.

Upstream [Notepad++](https://notepad-plus-plus.org/) is Windows-only — its UI, plugin ABI, docking,
and dialogs are welded to the Win32 API. wxNotepad++ reuses only the genuinely portable parts of it —
**Scintilla + Lexilla**, through wxWidgets' `wxStyledTextCtrl` — and reimplements the Notepad++ UI in
portable C++/wx. Plugins are first-class via the project's own permissive, cross-platform **Nib API**;
real Win32 Notepad++-ABI plugin binaries are additionally supported on Windows through an optional GPL
bridge plugin (see [Plugins](#plugins) below).

## Status

Experimental, under active development. The Windows build is feature-rich and closely matches native
Notepad++; the Linux/macOS builds are structured and CI-wired but still being validated.

**Implemented:** tabbed editor with per-tab Scintilla documents, a **split second view** (Notepad++'s
MAIN | SUB — Move/Clone to Other View, with the split collapsing when a pane empties), syntax
highlighting (Lexilla), a full **User-Defined Language** system (multi-tab dialog, per-style Styler
popups, `userDefineLangs/` persistence, `userDefineLang.xml`-compatible), find/replace and
find/replace-in-files, an **incremental search bar** (match-case / whole-word / regex toggles, live
highlight-all, and an n-of-m match counter), **find-driven multi-cursor** (Select All Occurrences / Add
Next), Go-to-line, dark mode, auto-indent / brace-match / smart-highlight, bookmarks, a **Document Map**
(minimap), a **Function List** (symbol tree, with rules derived independently from each language's own
grammar — C++, Python, JS/TS, Java, C#, Go, Rust, Lua), a **Clipboard History** panel, a **Project
Panel** (workspace tree of folders + files, saved as `.xml`) and folder-as-workspace, pinned tabs,
**Restore Recent Closed File** (Ctrl+Shift+T) + MRU Ctrl+Tab switching, an **interactive status bar**
(double-click to go-to-line, convert EOL or encoding, or toggle INS/OVR), EOL detection, session
restore, print + print preview, macro recording/playback, **Monitoring** (tail -f: reload on external
change), three selectable **toolbar icon sets** (Tabler, Solar, IconPark — see Credits), full UI
**localization** into 8 languages (pl, de, fr, es, ru, ja, zh, ko), and a **plugin host** — see
[Plugins](#plugins).

## Plugins

wxNotepad++'s core hosts its own original, permissive, cross-platform plugin API, **Nib**
(`include/nib/nib.h`) — a `wxDynamicLibrary`-based loader (`.dll` / `.so` / `.dylib`) with commands,
event subscriptions, dockable panels, and document/editor access. The core reproduces **no** Notepad++
plugin ABI itself.

Real, compiled **Notepad++-ABI plugin binaries** (`NPPM_*` messages, `FuncItem`, `NppData`, …) are
additionally supported on **Windows only**, through an optional bridge, `packages/npp-bridge`. The
bridge is itself just a Nib plugin: it loads Notepad++-ABI DLLs, surfaces their commands in the Plugins
menu, and translates `NPPM_*` / `FuncItem` / `SCNotification` to and from Nib on their behalf — see
[`packages/npp-bridge/README.md`](packages/npp-bridge/README.md) for exact `NPPM_*` coverage. Because
this bridge reproduces Notepad++'s ABI, it is licensed **GPL-3.0-or-later**, kept isolated from the
otherwise permissive-ready core (see [`docs/FUTURE_PLANS.md`](docs/FUTURE_PLANS.md)).

## Building

Requires CMake ≥ 3.20 and a C++17 compiler. wxWidgets 3.3.1 is fetched and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target wxnpp
# -> build/bin/wxnpp   (wxnpp.exe on Windows)
```

- **Windows** — Visual Studio 2022 (MSVC); the native dark-mode code and the optional `packages/npp-bridge` (Notepad++-ABI plugin bridge) compile in here.
- **Linux** — needs GTK3 dev headers: `sudo apt-get install build-essential cmake ninja-build pkg-config libgtk-3-dev`
- **macOS** — needs the Xcode command-line tools.

Plugins:

- **Nib plugins** (native, cross-platform): drop `Plugin.dll` / `Plugin.so` / `Plugin.dylib` into
  `<exe>/nib/` next to the executable.
- **Notepad++-ABI plugins** (Windows only): drop `Plugin/Plugin.dll` into `<exe>/plugins/Plugin/`. This
  additionally requires `npp_bridge.dll` to be present in `<exe>/nib/` (built automatically alongside
  `wxnpp` on Windows) — the bridge is what actually loads and translates for these plugins.

## Layout

```
src/                 the wxNotepad++ application (+ src/plugins/nib_test_plugin, a Nib-native test plugin)
packages/            npp-bridge (optional GPL Notepad++-ABI bridge, Windows-only, itself a Nib plugin),
                     test_plugin (a Notepad++-ABI test fixture, Windows-only)
include/nib/         the project's own permissive, cross-platform plugin API (nib.h)
include/npp-compat/  clean-room, permissive Notepad++-ABI headers (consumed only by packages/npp-bridge)
resources/           toolbar icons (icons/ = Tabler default, icons-solar/, icons-iconpark/), themes,
                     default styler, locale/ (8-language i18n catalogs)
third_party/         scintilla + lexilla (both permissive, HPND), wxbf (wxBorderlessFrame, wxWindows Licence)
installer/           packaging scripts: windows/ (NSIS), linux/ (AppImage, .deb, .rpm, Flatpak), macos/ (.dmg)
docs/                CROSS_PLATFORM_PLAN.md, FUTURE_PLANS.md (licensing roadmap), LICENSE_AUDIT.md,
                     PLUGIN_API_PLAN.md, WXWIDGETS_MIGRATION_PLAN.md
```

## Installing

Grab the latest build from [Releases](https://github.com/Alpaq92/wx-notepad-plus-plus/releases):

- **Windows** — the NSIS installer (`wxNotepadPlusPlus-<version>-Setup.exe`)
- **Linux** — an AppImage, `.deb`, `.rpm`, or `.flatpak` (`flatpak install wxNotepadPlusPlus-<version>.flatpak`)
- **macOS** — a `.dmg`

Every push also builds all of these as CI artifacts (see `.github/workflows/build.yml` and
`installer/{windows,linux,macos}/`); pushing a version tag (`v*`) runs
`.github/workflows/release.yml`, which rebuilds everything and attaches it to a new GitHub Release.

## License

**GPL v3 today — with a committed plan to go permissive.** wxNotepad++ is an **independent
reimplementation** — it copies no Notepad++ source code; Notepad++ is used only as a behavioral
reference and a test target (see [`NOTICE`](NOTICE)). It ships under the GNU GPL v3 for now — the honest,
conservative position given its heritage and the still-unsettled status of its plugin-ABI compatibility.
We've already clean-room-reimplemented or replaced nearly every Notepad++-derived file, and our
committed goal is to relicense permissively — **purely to give users and downstream developers more
freedom, with no commercial agenda.** See [`docs/FUTURE_PLANS.md`](docs/FUTURE_PLANS.md) for the roadmap
and [`LICENSING.md`](LICENSING.md) for the per-component record.

## Credits

- [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus) — Don Ho (GPL v3): the editor this reimplementation matches and tests against. Its plugin ABI is reimplemented **clean-room and cross-platform** in our own `include/npp-compat/`, consumed only by the optional `packages/npp-bridge` bridge — no Notepad++ source is used (see [`LICENSING.md`](LICENSING.md)).
- [Scintilla & Lexilla](https://www.scintilla.org/) — Neil Hodgson (permissive): the editing / syntax-highlighting engine.
- [wxWidgets](https://www.wxwidgets.org/): the cross-platform UI toolkit.
- Toolbar icon sets (Settings > Preferences > General > Toolbar icon style — see each set's own CREDITS.md for exact modifications):
  - [Tabler Icons](https://tabler.io/icons) (MIT) + [Open Color](https://yeun.github.io/open-color/) (MIT) — the default line-icon set (`resources/icons/CREDITS.md`).
  - [Solar Icons](https://icon-sets.iconify.design/solar/) (Bold Duotone) — © 480 Design (CC BY 4.0, attribution required; `resources/icons-solar/CREDITS.md`).
  - [IconPark](https://github.com/bytedance/IconPark) — © ByteDance (Apache-2.0; `resources/icons-iconpark/CREDITS.md`).
- Color themes: kept third-party themes are MIT (© Fabio Zendhi Nagao; Bespin © Oren Farhi); regenerated themes + the default styler use permissive palettes (GitHub Primer, Atom One, Nord, Dracula, VS Code — all MIT; canonical Zenburn/Obsidian colors). [Markdown Preview Enhanced](https://github.com/shd101wyy/vscode-markdown-preview-enhanced) (NCSA) was reviewed as a permissive palette source.
