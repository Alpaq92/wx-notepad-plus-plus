# wxNotepad++

**An experimental, cross-platform (Windows / Linux / macOS) Notepad++-faithful editor built on [wxWidgets](https://www.wxwidgets.org/).**

> ⚠️ Experimental and unofficial. Not affiliated with or endorsed by the Notepad++ project.

Upstream [Notepad++](https://notepad-plus-plus.org/) is Windows-only — its UI, plugin ABI, docking,
and dialogs are welded to the Win32 API. wxNotepad++ reuses only the genuinely portable parts of it —
**Scintilla + Lexilla**, through wxWidgets' `wxStyledTextCtrl` — and reimplements the Notepad++ UI in
portable C++/wx, while keeping **binary compatibility with existing Win32 Notepad++ plugins on the
Windows build** (on Linux/macOS, plugins must be rebuilt; Windows-only plugins are unsupported there).

## Status

Experimental, under active development. The Windows build is feature-rich and closely matches native
Notepad++; the Linux/macOS builds are structured and CI-wired but still being validated.

**Implemented:** tabbed editor with per-tab Scintilla documents, syntax highlighting (Lexilla),
find/replace, Go-to-line, dark mode, auto-indent / brace-match / smart-highlight, bookmarks, a
**Document Map** (minimap), a **Function List** (symbol tree), pinned tabs, EOL detection, and — on
Windows — a **Win32 plugin host** (real `LoadLibrary` loader, broad `NPPM_*` coverage, `NPPM_DMM*`
docking panels, and a subclass bridging plugin `SCI_*` messages into wxSTC).

## Building

Requires CMake ≥ 3.20 and a C++17 compiler. wxWidgets 3.3.1 is fetched and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target wxnpp
# -> build/bin/wxnpp   (wxnpp.exe on Windows)
```

- **Windows** — Visual Studio 2022 (MSVC); the toolbar / plugin host / native dark-mode code compiles in here.
- **Linux** — needs GTK3 dev headers: `sudo apt-get install build-essential cmake ninja-build pkg-config libgtk-3-dev`
- **macOS** — needs the Xcode command-line tools.

Plugins (Windows): drop `Plugin/Plugin.dll` into `build/bin/plugins/Plugin/` next to the executable.

## Layout

```
src/            the wxNotepad++ application (+ src/plugins/test_plugin)
include/        npp-compat/ - clean-room, cross-platform plugin ABI headers
resources/      toolbar icons, themes, default styler
third_party/    scintilla (headers), lexilla (lexers) - both permissive (HPND)
docs/           FUTURE_PLANS.md (licensing roadmap), WXWIDGETS_MIGRATION_PLAN.md
```

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

- [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus) — Don Ho (GPL v3): the editor this reimplementation matches and tests against. Its plugin ABI is reimplemented **clean-room and cross-platform** in our own `include/npp-compat/` — no Notepad++ source is used (see [`LICENSING.md`](LICENSING.md)).
- [Scintilla & Lexilla](https://www.scintilla.org/) — Neil Hodgson (permissive): the editing / syntax-highlighting engine.
- [wxWidgets](https://www.wxwidgets.org/): the cross-platform UI toolkit.
- [Tabler Icons](https://tabler.io/icons) (MIT) + [Open Color](https://yeun.github.io/open-color/) (MIT): the toolbar icon set.
- Color themes: kept third-party themes are MIT (© Fabio Zendhi Nagao; Bespin © Oren Farhi); regenerated themes + the default styler use permissive palettes (GitHub Primer, Atom One, Nord, Dracula, VS Code — all MIT; canonical Zenburn/Obsidian colors). [Markdown Preview Enhanced](https://github.com/shd101wyy/vscode-markdown-preview-enhanced) (NCSA) was reviewed as a permissive palette source.
