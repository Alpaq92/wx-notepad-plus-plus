# Changelog

All notable changes to wxNotepad++ are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.3.0] - 2026-07-06

### Added
- Preferences > General: a "Theme" combobox (System / Dark / Light) replaces the old "Dark Mode"
  menu checkbox.
- Preferences > Editing: a "Font" combobox — JetBrains Mono first, then every installed system
  font — lets you change the editor font; falls back to JetBrains Mono automatically if a
  previously-chosen font is later uninstalled.
- The default editor font is now JetBrains Mono (SIL Open Font License 1.1, bundled) instead of
  the proprietary, Windows-only Consolas, so every platform gets the same font out of the box.
- Preferences > Editing: an optional custom colour for the line-number/bookmark/fold gutter.
- Preferences > General: "Ask before closing unsaved changes" (off by default, matching
  Notepad++). When off, closing an unsaved document without prompting now backs it up
  automatically and offers it back as a recovered, unsaved tab the next time wxNotepad++ launches.
- Help menu links now point to this project's own GitHub repo instead of Notepad++'s.

### Fixed
- The toolbar's hover highlight and the editor's selected-text colour looked wrong in dark mode.
- Preferences > General "Theme: System" wasn't actually detecting the OS's dark/light setting and
  always rendered light regardless of it.
- Closing a single unsaved tab (as opposed to exiting the whole app) no longer leaves behind a
  permanent "ghost" recovery entry that keeps resurfacing on every future launch.
- The tab pin icon now uses the app's accent green instead of a flat grey, matching the toolbar.

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

[0.3.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.3.0
[0.2.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.2.0
[0.1.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.1.0
