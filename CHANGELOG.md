# Changelog

All notable changes to wxNotepad++ are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.5.9] - 2026-07-10

### Fixed
- On Linux (GTK) the v0.5.8 dark-scrollbar theming didn't actually take effect under desktop themes with a
  coloured accent (e.g. Linux Mint's dark green): the editor's native `GtkScrollbar` kept the accent and,
  on an empty document, its full-height thumb read as a green strip down the right edge. Root cause was a
  GTK CSS **priority war** - our provider sat at `APPLICATION` priority, below the accent (`USER` priority,
  from `~/.config/gtk-3.0/gtk.css`). The provider now installs at the maximum priority with `!important`
  rules, so it wins the cascade regardless of how the theme injects the accent.
- On Linux the integrated toolbar showed a **different-shade gap to the right** of the icons instead of
  spanning the full window width. The AUI dock area was already the chrome colour; the seam was the native
  `GtkToolbar`'s own themed background (which `SetBackgroundColour` couldn't override) not matching it. The
  same top-priority GTK CSS provider now forces the `toolbar` node to the chrome colour, so the toolbar row
  is a seamless full width.
- On Linux the **"+ / v / x" tab caption buttons** overflowed the tab strip: `wxBitmapButton` carried the
  GTK theme's button min-height/padding. They now use the app's own custom-painted `TitleBarBtn` (no native
  button chrome), sized exactly to the strip height, on GTK only (Windows/macOS unchanged).

## [0.5.8] - 2026-07-10

### Fixed
- Closing an unsaved document could pop a **"can't open file … RecoveryBackups/…bak" error** on Linux
  (and silently fail on installed Windows/macOS): the unsaved-changes recovery backups were written to a
  `RecoveryBackups/` folder next to the executable, which isn't user-writable on an installed build
  (`/opt/wxnpp`, `Program Files`, inside the `.app` bundle). Backups now live under the per-user data dir
  (`wxStandardPaths::GetUserDataDir()`), and the whole recovery subsystem is best-effort (`wxLogNull`), so
  a failed backup/restore can never surface an error dialog.
- **User-Defined Languages and the context-menu file** (`contextMenu.xml`) were likewise stored next to
  the executable, so creating/editing them silently failed to persist on installed builds. They now use
  the same per-user data dir.
- On Linux (GTK), a **green/teal vertical strip ran down the right edge** of the window under desktop
  themes with a coloured accent (e.g. Linux Mint's dark green): the editor's native `GtkScrollbar` was
  left raw-native and took the theme accent, and on an empty document its thumb spans the full track. A
  small GTK shim (`src/gtk_native.cpp`) now dark-themes the native scrollbars via a `GtkCssProvider` — the
  GTK analogue of the `DarkMode_Explorer` scrollbar theming already done on Windows.

## [0.5.6] - 2026-07-10

### Added
- Preferences > General **"Toolbar icon size"** - pick 16, 20, 24, or 32 px for the toolbar icons
  (restart-to-apply, like the icon-style option next to it). The icons are SVG, so every size stays
  crisp. Fully localized in all 8 languages.

### Fixed
- On macOS the toolbar had three linked problems - oversized icons, a large empty gap before the first
  icon, and an over-tall bar - all rooted in the native `NSToolbar`, which sizes icons by a coarse size
  *mode* (not the bitmap size), reserves a wide leading band, and turns each group separator into a fat
  space item. macOS now builds the toolbar the same way Windows/Linux do: an ordinary child toolbar wx
  lays out itself (real icon-sized slots, thin separators, no leading gap), docked as a top pane, instead
  of the frame's native `NSToolbar`. Icons now honour the chosen size and the bar matches the other
  platforms.
- On macOS the title bar still showed "new 1 - wxNotepad++" despite the 0.5.5 blank-title change: wx's
  `SetTitle("")` clears the text, but nothing kept it blank against later title reassertions. A small
  Objective-C++ shim (`src/macos_native.mm`) now sets `NSWindow.titleVisibility = Hidden`, which blanks
  the native title bar unconditionally.
- On macOS the Preferences dialog's category list ran as a squashed horizontal strip across the *top*
  instead of down the left side (cramping the page content). wx's default listbook alignment
  (`wxLB_DEFAULT`) resolves to *top* on macOS but *left* on Windows/Linux; the dialog now passes
  `wxLB_LEFT` explicitly so the list is a left column everywhere (no change on Windows/Linux, whose
  default already resolved to left).
- On macOS the main window opened at 1100×720 even on laptops whose usable width is narrower (~1016 px),
  so it spilled off-screen. The initial size is now clamped to the display's usable area (and re-centred)
  on macOS; Windows/Linux keep the 1100×720 default unchanged.
- On Windows the integrated top bar's menu items (File, Edit, …) were spaced too far apart - a regression
  from the 0.5.5 Linux fix, which added a 4px border on *every* platform. MSW's native buttons already
  reserve internal horizontal margin, so the extra border there over-spaced the bar. The 4px border is
  now applied only on GTK/macOS (where `wxBU_EXACTFIT` buttons genuinely need it); Windows gets none.
- On Linux/GTK the Preferences dialog's options still rendered too slim (clipped checkbox labels,
  mispositioned combos) even after the 0.5.5 `Layout()` fix. Root cause: the left nav list is created
  with `wxLC_NO_HEADER`, which makes `wxListCtrlBase::DoGetBestClientSize()` ignore the real column width
  and report an arbitrary ~250px; the book control then handed the nav list that inflated width and
  pushed the page content off to the right. Setting only `SetMinSize` didn't cap it - `GetBestSize()`
  clamps to the *max* size too - so the nav list now pins `SetMaxSize` to the intended width as well.
- On Linux/GTK the persistent "blue tint/glow in the bottom-right corner" was the native status-bar
  size grip (`wxSTB_SIZEGRIP`), which GTK paints in the desktop theme's accent - a blue diagonal under
  Linux Mint's dark theme - clashing sharply with our dark chrome. The grip is now dropped on GTK too
  (as it already was on Windows); the window still resizes from its borders. macOS keeps its native grip.

## [0.5.5] - 2026-07-10

### Added
- Preferences > General **"Auto-hide toolbar in full screen"** (off by default - the toolbar now stays
  visible in full-screen mode; turn this on for the old behaviour of hiding it).

### Changed
- Preferences and the other configuration items (Style Configurator, Shortcut Mapper, Import, Edit
  Popup ContextMenu, Localization) moved out of the Window menu into a restored top-level **Settings**
  menu - the Phase B reshape had merged Settings into Window to hit a round 10 top-level menus, but
  Preferences under "Window" was an unintuitive home. Window is now genuine window management only
  (Sort By / Windows / Recent Window). Back to 11 top-level menus. Every `IDM_*` id is unchanged; the
  "Se&ttings" label was already translated in every locale from the original port, so no catalog change.
- On macOS the window title bar is now left blank instead of showing "<document> - wxNotepad++"
  (the document name is already in the tab; a clean native title bar is the macOS convention).

### Fixed
- On Linux/GTK, the integrated top bar's menu items (File, Edit, Selection, …) were crammed together
  with no spacing - `wxBU_EXACTFIT` collapses each menu button to its bare text extent, and the sizer
  added no border between them, so on GTK (which, unlike MSW, keeps no internal button margin) the
  labels abutted. Added a 4px border each side.
- On Linux/GTK, the "+/v/x" caption buttons at the right of the tab strip stood out as a distinct
  block - their panel used a hardcoded chrome colour, but `wxAuiFlatTabArt` paints the strip itself
  from a system-derived colour (`wxAuiDimColour(wxSYS_COLOUR_WINDOW, 5)`) that differs from it on GTK.
  The caption bar now replicates that exact formula on non-Windows platforms so it matches the strip.
- On Linux/GTK, the Preferences dialog was badly broken (clipped checkbox labels, empty/mispositioned
  combos, dead space) - the dialog is opened at a fixed size with no post-show resize, and on GTK
  `SetSizer` alone doesn't lay children out until an explicit `Layout()` or a size event, so every
  `wxDefaultSize` control rendered at its unlaid-out construction geometry. Added an explicit
  `dlg.Layout()` (a no-op on Windows, which reflows on the initial show).
- The Screenshots lightbox's "1 / 7" image counter rendered stacked vertically (each of "1", "/", "7"
  on its own line) instead of on one line. Root cause: this site's own global CSS reset
  (`img, ion-icon, a, button, time, span { display: block }`, inherited from the vcard template the
  page was adapted from) turns *every* span into a block element, and the counter is built from three
  spans - so each took its own line. Forced just the counter's spans back to `inline`.

## [0.5.0] - 2026-07-09

### Fixed
- On Linux and macOS, toolbar/menu icons, theme files, the User-Defined Language dialog's bookmark
  marker, the context-menu XML, bundled fonts, and - most impactfully - **all 8 translated languages**
  silently failed to load, because a dozen resource-path lookups in `src/main.cpp` were built with a
  hardcoded Windows `\` separator instead of the cross-platform `wxFILE_SEP_PATH`. The UI always fell
  back to English on those platforms even when a non-English language was selected. Windows was never
  affected (its separator happens to be `\`), which is why this went unnoticed until real Linux testing.
- The macOS `.dmg` baked in whatever macOS version the CI runner happened to be running as its minimum
  supported OS, rather than a fixed floor - a `.dmg` built today could refuse to launch on anything
  older than that runner image. `CMAKE_OSX_DEPLOYMENT_TARGET` is now pinned to 11.0 (Big Sur), matching
  the installer's own `LSMinimumSystemVersion`.
- The Preferences dialog's category list rendered left-aligned on Windows but visually centered on
  Linux/macOS - `wxListbook` only self-selects the left-aligned single-column list mode on MSW; other
  platforms fell back to an icon-grid layout that GTK renders less predictably for iconless items. The
  list now forces the same left-aligned mode on every platform.
- The Linux download button's hover state used a fading gradient meant for an avatar glow effect,
  making its label unreadable partway through the fade. Hover now uses a solid accent background.
- The Folder as Workspace tree's icon patch only covered 3 of wxWidgets' 9 built-in slots (folder,
  folder-open, generic file); the other 6 (computer/drive/cdrom/floppy/removable/executable) and every
  per-file-extension icon (`.cpp`, `.py`, `.png`, …) still fell back to the OS's own icon set. All 9
  built-in slots are now patched, plus ~90 common extensions across code/document/image/archive/
  audio/video, pre-seeded into `wxFileIconsTable`'s own lookup cache so the OS's MIME-based icon
  lookup is never consulted for a recognized extension.
- On Linux, the integrated top bar's minimize/maximize/close buttons had a visible hover-repaint lag
  (the highlight would fill half the button, then catch up a moment later) - a plain `wxButton`'s
  native GTK widget drives its own `:hover`/`:prelight` repaint independently of, and racing, this
  app's own `SetBackgroundColour()`+`Refresh()` call on the same pointer events. These buttons are now
  fully custom-painted on non-Windows platforms (`TitleBarBtn`), removing the native GTK widget - and
  its independent repaint cycle - from the picture entirely. Windows is unaffected and unchanged (its
  native `wxButton` has no equivalent independent repaint path to race against).
- Entering or leaving full-screen mode left the toolbar two-toned - the AUI dock-art background strip
  beside the (non-full-width) toolbar pane and the toolbar's own background are both painted by
  `applyTheme()`, which only ran at startup or an explicit theme switch, never on a full-screen
  transition (which changes the frame's width without re-triggering either paint). Both now re-apply
  on every full-screen toggle.
- The active tab's green top-edge marker could leave a sliver of `wxSYS_COLOUR_HOTLIGHT` (a system
  accent colour - blue on GTK/most Linux desktop themes) showing past its right edge: the marker was
  sized from `DrawPageTab`'s return value (the tab's horizontal *advance* to the next tab), which can
  be narrower than the tab's own rendered width, while the base class's own marker underneath - which
  the green one is meant to fully cover - is sized from the tab's actual width. The green marker now
  matches the base class's own rectangle exactly, so it always fully covers it regardless of what the
  advance value happens to be.
- The Linux download button's hover state (fixed earlier to a flat solid colour after a fading
  gradient made the label illegible) now uses a two-stop, fully opaque gradient instead of a flat
  colour - visually closer to the button's own default (non-hover) gradient look, without reintroducing
  the fade-to-transparent bug.
- The project site's Windows/macOS/Linux download dropdowns had three bugs: opening a second one
  while another was already open just visually hid the first behind the newly-raised card instead of
  closing it; clicking a download item inside an open dropdown didn't close it (the outside-click
  handler that closes a dropdown never fires for a click that lands *inside* it); and an open
  dropdown's raised `z-index` (added to clear sibling cards) happened to tie with the site's own
  navbar, so it only rendered above the navbar by DOM-order coincidence rather than an actual
  guarantee. Opening a dropdown now explicitly closes every other one first, picking an item closes
  it, and its raised `z-index` now has deliberate headroom above every other value in the stylesheet.
- Clicking a Screenshots page thumbnail did nothing - a dead, invisible CSS overlay (`::before`, left
  over from the vcard template this site was adapted from, for a hover effect that hasn't applied
  since these tiles stopped being links) sat on top of every thumbnail, intercepting the click before
  it ever reached the `<img>` the lightbox listens on.
- The lightbox's "1 / 7" counter and its reset/rotate/close buttons floated with no header bar behind
  them - `headerOpacity` defaults to 0 in the vendored library, and its own CSS references a
  misspelled custom property (`--header-bg-opcity` instead of `--header-bg-opacity`, which is what its
  JS actually sets) so setting it wouldn't have helped without also fixing that typo, worked around
  here rather than in the vendored file.

### Added
- A click-to-zoom lightbox on the project site's Screenshots page
  ([img-previewer](https://github.com/yue1123/img-previewer), MIT, vendored locally).
- Two Screenshots page tiles showing the Solar and IconPark toolbar icon packs, replacing a redundant
  "Dark theme" tile that duplicated the (also dark-theme) Multi-document editing screenshot.
- A project landing page (`site/`), deployed to GitHub Pages on every published release: About,
  Features, Screenshots, Download, and Changelog, with a system/light/dark theme switch. The version
  badge, per-platform download links, and changelog list are fetched live from the GitHub API, so the
  page never needs a rebuild to stay current with the latest release.
- macOS now ships as two separate, single-arch `.dmg` builds (Apple Silicon `arm64` and Intel
  `x86_64`) instead of one build that only matched whichever architecture happened to be running the
  CI job. Both build on the same runner via `CMAKE_OSX_ARCHITECTURES`; the project site's macOS
  download button is now a dropdown, matching the Linux one.

### Changed
- The menu bar has been reorganized from Notepad++'s original 13-menu layout into an original 10-menu
  hierarchy — File, Edit, **Selection** (new: multi-cursor/select-all items split out of Edit),
  **Go** (renamed from Search), View, **Document** (new: Language + Encoding), **Automation** (new:
  Macro + Run + the MD5/SHA hash-generator submenus, previously three near-empty top-level menus),
  **Extensions** (renamed from Plugins), Window (now also hosts what used to be a standalone Settings
  menu, plus the Localization picker), Help (renamed from "?"/About) — designed from cross-editor
  research (VS Code, Notepad++, Pulsar Edit, Android Studio, Visual Studio) rather than copied from any
  one of them. Every command keeps its existing keyboard shortcut and `IDM_*` id; only which menu it
  lives under changed.

## [0.4.0] - 2026-07-06

### Added
- Command-line switches: `-g`/`--goto <line[,col]>` opens the last file given on the command line at
  that position, `-e`/`--encoding <name>` (ansi/utf8/utf8bom/utf16le/utf16be) re-interprets it in a
  forced encoding, and `-n`/`--new-instance` / `-r`/`--reuse-instance` override the instance-reuse
  setting below for a single launch.
- Preferences > General: "Reuse an existing window" (off by default). When on, launching wxnpp a
  second time hands its file arguments to the already-running window over IPC and exits instead of
  opening a new one.
- Saving to a location your account can't write to (e.g. `C:\Program Files\...`) now offers to
  relaunch wxnpp elevated (UAC) to complete just that one save, instead of failing outright.

### Changed
- The menu bar is now built from data tables instead of one large hand-written function
  (`src/menu_labels_*.h` / `src/menu_data_*.h` / `src/menu_builder.h`, replacing `src/npp_menu.h`) -
  a mechanical, behavior-preserving refactor with no visible change, laying the groundwork for a
  future menu reorganization.

### Fixed
- A full sweep of the UI turned up several strings that were never wired into the translation
  system: a handful of tooltips, four Help-menu items, several Find/Replace dialog labels, the
  Find in Files / Run / Style Configurator / About dialog titles and the About dialog's body text,
  the Column Editor's number-format choices, several `wxFileDialog` file-type filters, the
  Preferences encoding/default-language dropdowns, and the Find in Files result messages. All are
  now translated into all 8 supported languages.
- The Japanese, Chinese, and Korean translations of the Find/Replace-in-Files summary line
  (e.g. "12 hits in 3 / 8 files") had their numbers in the wrong order relative to the English
  source, so those languages were showing the wrong count next to each label.
- The tab pin icon no longer forces an accent-green tint; it uses the same theme-aware color as
  other toolbar icons.
- Building from source on Linux/macOS failed with undefined references from the single-instance
  IPC code above: those platforms route it through wxWidgets' TCP-based `wxTCPServer`/`wxTCPClient`
  classes (Windows uses DDE instead, bundled into the core library already), which live in a
  separate `net` component that `CMakeLists.txt` wasn't linking.

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

[0.4.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.4.0
[0.3.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.3.0
[0.2.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.2.0
[0.1.0]: https://github.com/Alpaq92/wx-notepad-plus-plus/releases/tag/v0.1.0
