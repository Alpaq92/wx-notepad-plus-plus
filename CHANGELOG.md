# Changelog

All notable changes to wxNotepad++ are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.2.0] - 2026-07-05

### Added
- Linux `.rpm` and Flatpak packages, alongside the existing AppImage/`.deb` (see
  [`installer/linux`](installer/linux)) — every push now builds all 4 Linux formats.
- Settings > Edit Popup ContextMenu now does something: it opens a user-editable
  `contextMenu.xml` next to the executable, and the editor's right-click menu is built from it
  (falling back to the previous built-in list if the file is missing or invalid).

### Fixed
- The popup (right-click) editor context menu's own labels were hardcoded English and never went
  through translation, even on a non-English UI; they're now pulled live from the real menu bar so
  they follow the current UI language.

## [0.1.0] - 2026-07-05

Initial tagged release. wxNotepad++ is an experimental, cross-platform (Windows / Linux / macOS)
reimplementation of Notepad++ built on wxWidgets + wxStyledTextCtrl (Scintilla/Lexilla), plus an
original permissive plugin API with optional Notepad++-ABI plugin compatibility on Windows.

### Added
- Tabbed editor with per-tab Scintilla documents and a split second view (MAIN | SUB, Move/Clone
  to Other View), with Lexilla syntax highlighting.
- A full User-Defined Language system (multi-tab dialog, per-style Styler popups,
  `userDefineLang.xml`-compatible persistence).
- Find/replace and find/replace-in-files, an incremental search bar (match-case / whole-word /
  regex, live highlight-all, n-of-m match counter), and find-driven multi-cursor (Select All
  Occurrences / Add Next).
- A Function List (symbol tree) with rules derived independently from each language's own grammar
  — C++, Python, JS/TS, Java, C#, Go, Rust, Lua.
- A Document Map (minimap), a Clipboard History panel, and a Project Panel (workspace tree of
  folders/files, saved as `.xml`) with folder-as-workspace support.
- Dark mode, auto-indent / brace-match / smart-highlight, bookmarks, pinned tabs, Restore Recent
  Closed File (Ctrl+Shift+T), and MRU Ctrl+Tab switching.
- An interactive status bar (double-click to go-to-line, convert EOL/encoding, toggle INS/OVR),
  EOL detection, and session restore.
- Print + print preview, macro recording/playback, and Monitoring (tail -f: reload on external
  change).
- Three selectable toolbar icon sets (Tabler, Solar, IconPark).
- Full UI localization into 8 languages: Polish, German, French, Spanish, Russian, Japanese,
  Chinese, Korean.
- A plugin host: the project's own permissive, cross-platform Nib API, plus an optional GPL
  bridge (`packages/npp-bridge`) for real Notepad++-ABI plugin binaries on Windows.
- Packaging for all 3 platforms: an NSIS installer (Windows), AppImage + `.deb` (Linux), and a
  `.dmg` (macOS), wired into CI on every push.

[0.2.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.2.0
[0.1.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.1.0
