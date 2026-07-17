# wxNote

**An experimental, cross-platform (Windows / Linux / macOS) text editor built on [wxWidgets](https://www.wxwidgets.org/).**

> ⚠️ Experimental software, under active development.

wxNote is built on wxWidgets' `wxStyledTextCtrl` (**Scintilla + Lexilla**) and runs natively on
Windows, Linux, and macOS from one codebase: tabbed editing, split views, theming, a native
custom-language engine (Scintillua), macros, an integrated terminal, session restore, and full UI
localization. Plugins are
first-class via the project's own permissive, cross-platform **Nib API**; legacy Win32 Notepad++-ABI
plugin binaries are additionally supported on Windows through an optional GPL bridge plugin (see
[Plugins](#plugins) below).

**[Project site & downloads →](https://alpaq92.github.io/wx-notepad-plus-plus/)**

Why the project exists, and why it's a from-scratch editor rather than a port, is told in
[`docs/GOALS.md`](docs/GOALS.md); how it's put together is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Status

Experimental, under active development. The Windows build is the most mature and feature-rich; the
Linux/macOS builds are structured and CI-wired but still being validated.

**Implemented:** tabbed editor with per-tab Scintilla documents, a **split second view**
(MAIN | SUB — Move/Clone to Other View, with the split collapsing when a pane empties), syntax
highlighting (Lexilla), a **native custom-language engine** — **Scintillua** (Lua 5.4.7 + LPeg 1.1.0
lexer grammars run through a Scintilla container lexer; see
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)), with legacy Notepad++ `userDefineLang.xml`
files importable via the optional GPL `udl-compat` plugin (see [Plugins](#plugins)), find/replace and
find/replace-in-files, an **incremental search bar** (match-case / whole-word / regex toggles, live
highlight-all, and an n-of-m match counter), **find-driven multi-cursor** (Select All Occurrences / Add
Next), Go-to-line, dark mode, auto-indent / brace-match / smart-highlight, **code folding** (built-in
lexers and custom Scintillua languages), bookmarks, a **Document Map**
(minimap), a **Function List** (symbol tree, with rules derived independently from each language's own
grammar — C++, Python, JS/TS, Java, C#, Go, Rust, Lua), an **integrated terminal** (multi-tab, with a
per-platform shell picker), a **Clipboard History** panel, a **Project Panel** (workspace tree of
folders + files, saved as `.xml`) and folder-as-workspace, pinned tabs, **Restore Recent Closed File**
(Ctrl+Shift+T) + MRU Ctrl+Tab switching, an **interactive status bar** (double-click to go-to-line,
convert EOL or encoding, or toggle INS/OVR), EOL detection, session restore, print + print preview,
macro recording/playback, **Monitoring** (tail -f: reload on external change), three selectable
**toolbar icon sets** (Tabler, Solar, IconPark — see Credits), full UI **localization** into 8
languages (pl, de, fr, es, ru, ja, zh, ko), and a **plugin host** — see [Plugins](#plugins).

## Plugins

wxNote's core hosts its own original, permissive, cross-platform plugin API, **Nib**
(`include/nib/nib.h`) — a `wxDynamicLibrary`-based loader (`.dll` / `.so` / `.dylib`) with commands,
event subscriptions, dockable panels, and document/editor access. The core reproduces **no** Notepad++
plugin ABI itself — it includes nothing from `include/npp-compat/`; its command ids live in the core's
own `src/command_ids.h`.

Real **Notepad++-ABI plugins** (`NPPM_*` messages, `FuncItem`, `NppData`, …) are additionally
supported through an optional **cross-platform** bridge, `packages/npp-bridge`: on Windows it
`LoadLibrary`s prebuilt N++ `.dll` binaries, and on Linux/macOS it `dlopen`s *recompiled* N++
plugins (`.so` / `.dylib`). Only the prebuilt Win32 `.dll` binaries themselves stay Windows-only. The
bridge is itself just a Nib plugin: it loads Notepad++-ABI DLLs, surfaces their commands in the Extensions
menu, and translates `NPPM_*` / `FuncItem` / `SCNotification` to and from Nib on their behalf — see
[`packages/npp-bridge/README.md`](packages/npp-bridge/README.md) for exact `NPPM_*` coverage. Because
this bridge reproduces Notepad++'s ABI, it is licensed **GPL-3.0-or-later**, kept isolated from the
otherwise Apache-2.0 core (see [`LICENSING.md`](LICENSING.md)).

Legacy Notepad++ **User-Defined Languages** (`userDefineLang.xml`) are supported through a second
optional plugin, `packages/udl-compat`: it parses your existing UDL files, translates each into a
Scintillua Lua/LPeg lexer, and registers it with the core via the new `nib.langdef` API (it also
ships a standalone `udl2scintillua` converter CLI). Because it knows the Notepad++ UDL format it is
licensed **GPL-3.0-or-later** and kept isolated from the Apache-2.0 core, and is built as
`bin/nib/udl_compat.dll` (see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)).

## Building

Requires CMake ≥ 3.20, a C++17 compiler, and Ninja. wxWidgets 3.3.1 is fetched and built
automatically on first configure (expect that one to take a while; builds are incremental after).
The native language engine's runtime — Lua 5.4.7 + LPeg 1.1.0 (built as a `lua_lpeg` static
library) plus Scintillua's `lexer.lua` — is likewise fetched automatically on first configure, so
the initial configure needs network access.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target wxnote
# -> build/bin/wxnote   (wxnote.exe on Windows)
```

- **Windows** — the MSVC compiler from Visual Studio 2022 Build Tools (the build itself is CMake + Ninja; MSBuild and solution files are not used); the native dark-mode code and the optional `packages/npp-bridge` (Notepad++-ABI plugin bridge) compile in here.
- **Linux** — needs GTK3 dev headers: `sudo apt-get install build-essential cmake ninja-build pkg-config libgtk-3-dev`
- **macOS** — needs the Xcode command-line tools.

Plugins:

- **Nib plugins** (native, cross-platform): drop `Plugin.dll` / `Plugin.so` / `Plugin.dylib` into
  `<exe>/nib/` next to the executable.
- **Notepad++-ABI plugins** (Windows only): drop `Plugin/Plugin.dll` into `<exe>/plugins/Plugin/`. This
  additionally requires `npp_bridge.dll` to be present in `<exe>/nib/` (built automatically alongside
  `wxnote` on Windows) — the bridge is what actually loads and translates for these plugins.

## Layout

```
src/                 the wxNote application (main.cpp + the data-driven menu engine, the Scintillua
                     language engine (scintillua_engine.{h,cpp}), terminal panel, command_ids.h,
                     platform shims; src/plugins/nib_test_plugin is a Nib-native test plugin)
packages/            npp-bridge (optional GPL Notepad++-ABI bridge, builds on every OS — loads real
                     Notepad++ plugin DLLs on Windows, recompiled npp_shim plugins on Linux/macOS; itself a Nib plugin),
                     udl-compat (optional GPL Nib plugin: imports legacy Notepad++ UDLs as Scintillua lexers),
                     test_plugin (a Notepad++-ABI test fixture, Windows-only, never shipped)
include/nib/         the project's own permissive, cross-platform plugin API (nib.h)
include/npp-compat/  clean-room Notepad++-ABI headers (consumed only by packages/npp-bridge and
                     packages/test_plugin — the core includes nothing from here)
resources/           toolbar icons (icons/ = Tabler default, icons-solar/, icons-iconpark/), themes,
                     default styler, fonts, locale/ (8-language i18n catalogs)
third_party/         scintilla + lexilla (both permissive, HPND), lua + lpeg + scintillua (the custom-language engine, all MIT), wxbf (wxBorderlessFrame, wxWindows Licence)
installer/           packaging scripts: windows/ (NSIS), linux/ (AppImage, .deb, .rpm, Flatpak), macos/ (.dmg)
docs/                GOALS.md (why the project exists), ARCHITECTURE.md (how the editor is put
                     together), CREDITS.md (everything used or consulted during development)
```

## Installing

Grab the latest build from the [project site's Download page](https://alpaq92.github.io/wx-notepad-plus-plus/)
(picks the right asset for you) or straight from
[Releases](https://github.com/Alpaq92/wx-notepad-plus-plus/releases):

- **Windows** — the NSIS installer, for x64 (`wxNote-<version>-Setup.exe`) or Windows-on-ARM
  (`wxNote-<version>-arm64-Setup.exe`)
- **Linux** — an AppImage, `.deb`, `.rpm`, or `.flatpak` (`flatpak install wxNote-<version>.flatpak`),
  each available for x86_64 and ARM (aarch64/arm64-suffixed assets), plus a `.deb` for `riscv64`
- **macOS** — a `.dmg`, built separately for Apple Silicon (`wxNote-<version>-arm64.dmg`) and
  Intel (`wxNote-<version>-x86_64.dmg`) — pick the one matching your Mac's chip

CI builds all of these as artifacts for every PR and for source-affecting pushes to master (see
`.github/workflows/build.yml` and `installer/{windows,linux,macos}/`); pushing a version tag (`v*`)
runs `.github/workflows/release.yml`, which rebuilds everything and attaches it to a new GitHub
Release. See [`CHANGELOG.md`](CHANGELOG.md) for release history.

## Command line

```
wxnote [options] [files...]

-g, --goto <line[,col]>   go to this line (and column) in the last file opened
-e, --encoding <name>     force encoding: ansi|utf8|utf8bom|utf16le|utf16be
-n, --new-instance        always open a new window
-r, --reuse-instance      reuse an already-running window
```

Files given on the command line are opened in tabs. By default every launch opens its own window;
turning on Preferences > General > "Reuse an existing window" makes a second launch hand its files to
the already-running window instead (over a local IPC connection) and exit — `-n`/`-r` override that
setting for a single launch either way.

## Contributing

Bug reports, code, translations, and themes are all welcome — see
[`CONTRIBUTING.md`](CONTRIBUTING.md) for how to build, the PR workflow, and the project's
code/licensing ground rules.

## License

**Apache License 2.0**, with two exceptions in what ships: the optional `packages/npp-bridge/` plugin
(which lets real compiled Notepad++ plugins load) and the optional `packages/udl-compat/` plugin
(which imports legacy Notepad++ `userDefineLang.xml` files by translating them to Scintillua lexers)
both stay **GPL-3.0-or-later** — npp-bridge because it reproduces Notepad++'s plugin ABI, udl-compat
because it knows the Notepad++ UDL format — kept isolated from the otherwise Apache-2.0 core; the
never-shipped, Windows-only test fixture `packages/test_plugin/` tracks the same GPL license for the
same reason. wxNote is an
**independent project** — it copies no Notepad++ source
code; Notepad++ was used only as a behavioral reference and a test target, and wxNote is not
affiliated with or endorsed by it (see [`NOTICE`](NOTICE)). The project started on GPL v3 and
relicensed once every Notepad++-derived file had been replaced or clean-room-reimplemented and the
plugin-ABI reproduction fully isolated into that one optional module — **purely to give users and
downstream developers more freedom, with no commercial agenda.** See [`LICENSING.md`](LICENSING.md)
for the per-component record.

## Credits

The full record of everything used or consulted during development — including the editors studied as
design references — is in [`docs/CREDITS.md`](docs/CREDITS.md). The headliners:

- [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus) — Don Ho (GPL v3): the editor whose behavior served as the original reference and test target. Its plugin ABI is reimplemented **clean-room and cross-platform** in our own `include/npp-compat/`, consumed only by the optional `packages/npp-bridge` bridge — no Notepad++ source is used (see [`LICENSING.md`](LICENSING.md)).
- [Scintilla & Lexilla](https://www.scintilla.org/) — Neil Hodgson (permissive): the editing / syntax-highlighting engine.
- [Lua](https://www.lua.org/) 5.4.7 (MIT), [LPeg](http://www.inf.puc-rio.br/~roberto/lpeg/) 1.1.0 (MIT), and [Scintillua](https://github.com/orbitalquark/scintillua) — © Mitchell (MIT): embedded to power wxNote's native custom-language engine.
- [wxWidgets](https://www.wxwidgets.org/): the cross-platform UI toolkit.
- Toolbar icon sets (Settings > Preferences > General > Toolbar icon style — see each set's own CREDITS.md for exact modifications):
  - [Tabler Icons](https://tabler.io/icons) (MIT) + [Open Color](https://yeun.github.io/open-color/) (MIT) — the default line-icon set (`resources/icons/CREDITS.md`).
  - [Solar Icons](https://icon-sets.iconify.design/solar/) (Bold Duotone) — © 480 Design (CC BY 4.0, attribution required; `resources/icons-solar/CREDITS.md`).
  - [IconPark](https://github.com/bytedance/IconPark) — © ByteDance (Apache-2.0; `resources/icons-iconpark/CREDITS.md`).
- [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) (SIL OFL 1.1) — the default editor font, bundled in place of the proprietary Consolas (`resources/fonts/CREDITS.md`).
- Color themes: kept third-party themes are MIT (© Fabio Zendhi Nagao; Bespin © Oren Farhi) or CC BY 3.0 (© Paul Neubauer); regenerated themes + the default styler use permissive palettes (GitHub Primer, Atom One, Nord, Dracula, VS Code — all MIT; canonical Zenburn/Obsidian colors). [Markdown Preview Enhanced](https://github.com/shd101wyy/vscode-markdown-preview-enhanced) (NCSA) was reviewed as a permissive palette source.
