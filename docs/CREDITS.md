# Credits

Everything wxNote used, consulted, or built on during development — ordered
roughly by magnitude of influence. Per-directory `CREDITS.md` files
(`resources/icons*/`, `resources/fonts/`, `site/`) carry the detailed,
asset-level records; [`LICENSING.md`](../LICENSING.md) and [`NOTICE`](../NOTICE)
carry the legal per-component license table. This file is the human-readable
"who helped, and how" record.

## Notepad++ — the reference

[Notepad++](https://notepad-plus-plus.org/) (Don Ho, GPL v3) is the editor
wxNote reimplements the *experience* of, and it towers over everything else on
this list. It was used throughout development as:

- **a behavioral cross-check and test target** — features were built by
  studying what Notepad++ *does* (command semantics, defaults, layout
  conventions) and comparing the result against a real installation;
- **a source of inspiration** — the feature set itself (Function List,
  Document Map, custom/user-defined languages, session handling, the Mark
  styles, macro recording, and much more) is Notepad++'s feature set, rebuilt;
- **a file-format compatibility target** — wxNote reads Notepad++'s own
  formats and writes compatible ones under its own root tag: the
  `<NotepadPlus>` theme/styler XML (real N++ theme files load unmodified;
  wxNote only reads these, it never writes a theme file), session XML (file
  list, scroll position and bookmarks interchange; caret position uses a
  wxNote-specific attribute and N++ second-view files are not restored; read
  from a `<wxNote>` or `<NotepadPlus>` root and written as `<wxNote>`), and
  `contextMenu.xml` (same schema, id-based entries; N++'s
  name-based/folder/plugin entries are not interpreted); legacy
  `userDefineLang.xml` UDL files are no longer read by the core — the optional
  GPL `packages/udl-compat/` plugin translates them into Scintillua lexers (see
  the native-language-engine section above);
- **a plugin-ABI fact source** — the numeric `IDM_*`/`NPPM_*` ids, struct
  layouts, and entry-point names a compiled Notepad++ plugin expects are
  reproduced clean-room (from public documentation) in `include/npp-compat/`,
  consumed by the optional GPL `packages/npp-bridge/` module so real
  Notepad++ plugin binaries still load on Windows.

**Not a single line of Notepad++'s implementation was migrated into the
wxNote editor** — `src/` is an independent, ground-up reimplementation, and
no Notepad++ source code survives anywhere in the project today. For the
record's sake: the repository's *history* began as a CMake build port of the
real Notepad++ source, and the early tree carried a few Notepad++-derived
files (the verbatim plugin-ABI headers, three GPL lexers that came with the
vendored Lexilla tree); all of those were removed or clean-room-reimplemented
well before the Apache relicense, as verified by a full provenance audit. See
[`NOTICE`](../NOTICE) and [`LICENSING.md`](../LICENSING.md) for the precise
present-day statements.

Notepad++'s **NppExec** plugin also inspired the integrated terminal's v1
pipe-console design.

## wxWidgets — the foundation

[wxWidgets](https://www.wxwidgets.org/) 3.3.1 (wxWindows Library Licence:
LGPL + binary-distribution exception) is the entire cross-platform UI layer —
windows, menus, AUI docking, printing, config, i18n, IPC — and supplies the
editor widget itself (`wxStyledTextCtrl`, which embeds Scintilla). Fetched and
built from source at build time, statically linked. Nothing in wxNote renders
without it.

## Scintilla & Lexilla — the editing engine

[Scintilla and Lexilla](https://www.scintilla.org/) (Neil Hodgson, HPND
permissive license) do the actual text editing and syntax highlighting.
Scintilla runs inside wxWidgets' `wxStyledTextCtrl`; Lexilla is vendored in
`third_party/lexilla/` and compiled in for its full lexer collection. Four of
those vendored lexers (Dart, Nix, TOML, Zig) are themselves based on **Zufu
Liu's Notepad4** lexers, adapted for Scintilla by Jiri Techet — see below.

## Scintillua, Lua & LPeg — the native language engine

wxNote's own cross-platform custom-language engine (`src/scintillua_engine.{h,cpp}`,
Apache-2.0) embeds three MIT-licensed components, fetched at build time and statically
linked as a `lua_lpeg` library:

- **[Scintillua](https://github.com/orbitalquark/scintillua)** (Mitchell / orbitalquark,
  MIT) — its `lexer.lua` and Lua/LPeg lexer grammars are wxNote's language-definition
  mechanism; wxNote runs them through a Scintilla container lexer and maps their tags to
  styles.
- **[Lua](https://www.lua.org/)** 5.4.7 (PUC-Rio, MIT) — the embedded scripting runtime
  the lexers execute in.
- **[LPeg](http://www.inf.puc-rio.br/~roberto/lpeg/)** 1.1.0 (Roberto Ierusalimschy, MIT) —
  the PEG pattern-matching library Scintillua's lexers are written against.

Legacy Notepad++ `userDefineLang.xml` files are handled *outside* the core by the optional
GPL `packages/udl-compat/` plugin, which translates each UDL into a Scintillua lexer and
registers it via the `nib.langdef` API — see [`ARCHITECTURE.md`](ARCHITECTURE.md).

## CMake & Ninja — the build

[CMake](https://cmake.org/) (≥ 3.20) with the [Ninja](https://ninja-build.org/)
generator builds everything on all three platforms, including fetching and
building wxWidgets from source. Compilers: MSVC (Windows), GCC (Linux),
AppleClang (macOS).

## Editors studied as design references

No code was taken from any of these; each contributed ideas, structure, or
"how does everyone else do it" grounding:

- **[VS Code](https://code.visualstudio.com/)** — the most-consulted non-N++
  editor: the multi-cursor UX (Ctrl+D "add next occurrence" / skip-occurrence),
  the light/dark selection colours, the menu-hierarchy research behind
  wxNote's own 11-menu structure, plugin-API research (lazy-activation
  manifests), and (with Visual Studio) the integrated title-bar layout.
- **[NotepadNext](https://github.com/dail8859/NotepadNext)** (Qt) — which
  session fields to persist (caret, first-visible line, bookmarks), the
  word-completion approach, and coalescing keystrokes during macro recording.
- **[Pulsar Edit](https://pulsar-edit.dev/)** (Atom's successor) — menu
  research, and plugin-API research (inter-plugin provided/consumed services
  with semver informed the Nib API's design).
- **[Notepad4](https://github.com/zufuliu/notepad4)** / Notepad2 (Zufu Liu) —
  the command-line `-g`/`-e` semantics (apply to the file just opened), and
  the upstream source of four Lexilla lexers wxNote ships (Dart/Nix/TOML/Zig).
- **[Sublime Text](https://www.sublimetext.com/)** — plugin-API research
  (transactional edits).
- **[notepad--](https://github.com/cxasm/notepad--)** — the
  keyword-completion idea (merge the language keyword list into completion).
- **[notepadqq](https://notepadqq.com/)** (Qt) — studied alongside
  NotepadNext as prior art.
- **Android Studio / IntelliJ** and **Visual Studio** — menu-hierarchy
  research (the dedicated Navigate/Automation-style menus; VS's menu UX
  rules), and VS's integrated top-bar look.
- **Atom** — the One Dark / One Light palettes (see themes below); Pulsar's
  ancestor.
- **Electron** — the macOS integrated-top-bar technique
  (`src/macos_native.mm` implements the same transparent-titlebar +
  re-centred traffic-lights approach Electron ships).
- **DjvuNet / DjVuLibre** — the model for the clean-reimplementation
  methodology itself (reference implementation informs and validates
  behavior, never a code dependency).

## Icons & color

- **[Tabler Icons](https://tabler.io/icons)** (Paweł Kuna, MIT) — the default
  toolbar icon set.
- **[Open Color](https://yeun.github.io/open-color/)** (Heeyeun Jeong, MIT) —
  the project's entire color language, including the wxNote brand green and
  the recoloring targets for all three alternative icon sets.
- **[Solar Icons](https://icon-sets.iconify.design/solar/)** (480 Design,
  CC BY 4.0) — the "Solar (green)" toolbar option, recolored to Open Color
  green-8/green-3; obtained via [Iconify](https://iconify.design/).
- **[IconPark](https://github.com/bytedance/IconPark)** (ByteDance,
  Apache-2.0) — the "IconPark (teal/lime)" toolbar option, recolored to Open
  Color teal-7/lime-5; also via Iconify.
- **Free icons from [Streamline](https://streamlinehq.com)** (Core flat set,
  CC BY 4.0) — the "Streamline (green/teal)" toolbar option, recolored to Open
  Color green-4/teal-8; obtained from the first-party
  [streamline-vectors](https://github.com/webalys-hq/streamline-vectors) repo
  at a pinned commit.

Full modification records: `resources/icons*/CREDITS.md`.

## Fonts

Two monospace families are bundled, both SIL OFL 1.1 and both unmodified,
chosen to replace the non-redistributable Consolas:

- **[Cascadia Mono](https://github.com/microsoft/cascadia-code)** (© Microsoft)
  — the **default** editor font, and the face the editor falls back to when the
  configured font is missing. Its OFL notice carries the Reserved Font Name
  `Cascadia Code`, so it may be redistributed as-is but not renamed or patched
  under that name — see `LICENSING.md`.
- **[JetBrains Mono](https://github.com/JetBrains/JetBrainsMono)** (© the
  JetBrains Mono Project Authors) — the second bundled choice, pinned in the
  font picker. No Reserved Font Name.

Details and the rationale for Cascadia *Mono* over Cascadia *Code*:
`resources/fonts/CREDITS.md`.

## Themes & palettes

Kept third-party themes (shipped with their original authors' headers):
**Fabio Zendhi Nagao** (Monokai, Black board, Choco, Mono Industrial, Plastic
Code Wrap, Twilight — MIT; his Monokai styler's per-lexer style *structure*
also seeded wxNote's own regenerated stylers), **Oren Farhi** (Bespin, MIT),
**Renato Silva** (Twilight co-author, MIT), and **Paul Neubauer**
(HotFudgeSundae, MossyLawn, Navajo, khaki, Solarized, Solarized-light —
CC BY 3.0; Solarized based on **Ethan Schoonover**'s). **Franck Albaret**'s
DansLeRuSH-Dark theme shipped in early versions but was removed once its
upstream license was confirmed as CC BY-NC-SA 3.0 (NonCommercial) — see
[`LICENSING.md`](../LICENSING.md).

Regenerated first-party themes credit their palette sources: **GitHub
Primer** (MIT), **Atom One Dark/Light** (MIT), **Nord** (MIT), **Dracula**
(MIT), and the canonical **Zenburn** and **Obsidian** palettes. **VS Code**'s
selection colours (MIT) are used in the default editor styling
(`src/main.cpp`). Markdown Preview Enhanced (shd101wyy, NCSA) was reviewed as
a palette source.

## Vendored libraries

- **[wxBorderlessFrame](https://github.com/swiszczoo/wxBorderlessFrame)**
  (Łukasz Świszcz, wxWindows Library Licence) — `third_party/wxbf/`; powers
  the optional integrated/borderless title bar on Windows and Linux.

## Project site

- **[vCard – Personal Portfolio](https://github.com/codewithsadee/vcard-personal-portfolio)**
  (codewithsadee, MIT) — the template `site/` was adapted from.
- **[Ionicons](https://ionic.io/ionicons)** (MIT) — the site's icon font
  (CDN-loaded).
- **[Poppins](https://fonts.google.com/specimen/Poppins)** (SIL OFL 1.1) —
  the site's web font (Google Fonts).
- **[img-previewer](https://github.com/yue1123/img-previewer)** (dh, MIT) —
  the screenshot lightbox, vendored in `site/assets/vendor/`.

Details: `site/CREDITS.md` and `site/assets/vendor/img-previewer/CREDITS.md`.

## Packaging, CI & tooling

- **[NSIS](https://nsis.sourceforge.io/)** (zlib) — the Windows installer
  (chosen over Inno Setup for its open-source license).
- **[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy)** (MIT) — the
  AppImage; **dpkg-deb** — the `.deb`; **rpmbuild** — the `.rpm`;
  **flatpak-builder** + the **GNOME Platform 46** runtime — the `.flatpak`.
- **librsvg / rsvg-convert** (LGPL) — icon rasterization for Linux/macOS
  packaging; Apple's **sips / iconutil / hdiutil** — the `.icns` and `.dmg`.
- **GitHub Actions** — the 3-platform CI matrix and tag-driven releases
  (`actions/*`, `ilammy/msvc-dev-cmd`).
- **GTK3** (LGPL) — the Linux toolkit backend wxWidgets builds against.
- **CPython's `Tools/i18n/msgfmt.py`** — the format reference for
  `resources/locale/po2mo.py`, this repo's own dependency-free `.po` → `.mo`
  compiler (the GNU gettext catalog *format* is used; GNU gettext tooling is
  never invoked or linked).
