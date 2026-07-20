# Changelog

All notable changes to wxNote are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.9.7] - 2026-07-20

### Added
- **Cross-platform Notepad++ plugin support — complete `NPPM_*` coverage (Phases 2–6).** The GPL
  `npp-bridge` now answers **all 118 `NPPM_*` messages** — up from ~44 — with **zero silent drops**:
  every message either performs a real action or returns a documented value, and each Windows-only
  concept is an explicitly-commented no-op. Notifications grow to **27 `NPPN_*`** types. The additive
  work spans a per-view buffer model, full session save/load (`NPPM_SAVESESSION` / `LOADSESSION` /
  `GETSESSIONFILES` via `nib.session/1`), Lexilla lexer creation (`NPPM_CREATELEXER` via
  `nib.lexer/1`), and the long-tail file-lifecycle notifications (`NPPN_FILEBEFOREDELETE` /
  `FILEDELETED` / `FILERENAMED` and friends). A headless `bridge_selftest` (175 checks) exercises the
  whole surface, including that a registered plugin's toolbar button rasterizes a real, non-blank image.
- **24 more colored toolbar icons** — drive, removable-media and file-type glyphs for the **Solar**
  and **IconPark** sets, matching the existing Streamline coverage.
- **File&nbsp;&rsaquo; Print Preview** — a cross-platform preview window (`wxPrintPreview` +
  `wxPreviewFrame`, reusing the existing `SciPrintout`), translated into all eight languages. The
  Windows print dialog's own preview pane still reads "not supported" — that pane is only fed by the
  Windows-only modern print API, which the portable printing path can't use — so this is the portable
  answer, working identically on Windows, Linux and macOS.

### Fixed
- **Plugin-unload crash on exit.** A Notepad++-ABI plugin's event/command subscriptions were cleared
  only *after* `deactivate()` + `FreeLibrary`, so a late frame notification could call into unmapped
  memory; and running the unload inside the frame's own `WM_CLOSE` reentrantly deferred a
  `RemoveWindowSubclass`, leaving a dangling subclass after `FreeLibrary`. Subscriptions are now cleared
  first, and the whole unload is deferred via `CallAfter` past the close dispatch.
- **Integrated terminal rendering.** Text no longer sits flush against the top-left edge (a small
  cell-grid margin was added), and the shell no longer starts one blank line down on Windows `cmd` (the
  codepage switch is now followed by a `cls`).
- **Landing-page icon tooltips.** Hovering a sidebar or toolbar glyph showed the icon's raw internal
  name ("Pricetag", "Language", …) instead of the surrounding label. Every icon is now inline SVG with
  its embedded `<title>` stripped, and the ionicons CDN dependency is gone.
- **Docs manual: scrollbar and phones.** The sidebar's stock 4px transparent-until-hover scrollbar
  flickered and read as broken; it and the page scrollbar are now a quiet, always-present themed slim
  bar. Verified no horizontal overflow at phone widths.

## [0.9.6] - 2026-07-19

### Added
- **Cross-platform Notepad++ plugin support, Phase 1.** The Nib ABI grows additively to 1.1 and the
  GPL `npp-bridge` now serves roughly 44 of the 118 `NPPM_*` messages plus 10 `NPPN_*` notifications.
  New in this round: **plugin toolbar icons** (`NPPM_ADDTOOLBARICON_DEPRECATED` / `_FORDARKMODE`
  through the new portable `nib.toolbar/1` — Windows converts the plugin's native images to RGBA
  pixels and picks the dark variant under dark chrome; off-Windows the messages are a documented
  no-op `TRUE`), **before-save/close/open events** (`NPPN_FILEBEFORESAVE` / `NPPN_FILEBEFORECLOSE` /
  `NPPN_FILEBEFOREOPEN`, plus `NPPN_TBMODIFICATION` on every OS), **id allocators**
  (`NPPM_ALLOCATECMDID` / `ALLOCATEMARKER` / `ALLOCATEINDICATOR` over the new `nib.alloc/1`:
  process-lifetime, collision-free ranges whose command ids — including `FuncItem::_cmdID`, now
  host-granted — dispatch on the wx-event path, immune to the 16-bit `WM_COMMAND` wrap),
  **dark-mode queries** (`NPPM_ISDARKMODEENABLED`, `NPPM_GETDARKMODECOLORS` and the editor default
  colours, via the new `nib.ui/1`), **inter-plugin messaging** (`NPPM_MSGTOPLUGIN`), and
  **save-event fidelity**: `nib.events` v2 reports each real disk write with the *written* buffer's
  id, so `NPPN_FILESAVED` no longer false-fires on undo-to-savepoint and Save All reports every
  buffer, not just the active one. Two divergences from real N++ are documented as permanent in
  `packages/npp-bridge/README.md`: `NPPN_READY` fires before session restore, and `NPPN_SHUTDOWN`
  is deferred past the frame's own close dispatch.

### Fixed
- **No more spurious "wxWidgets Debug Alert" pop-ups from high command ids.** wxWidgets keeps its
  assertions compiled in even for release builds (`wxDEBUG_LEVEL` defaults to 1 regardless of
  `NDEBUG`), so appending a menu item whose id sits above 32767 — the frozen doc-list, saved-macro,
  Nib-command, Scintillua-language and `nib.alloc` ranges — tripped `wxMenuItemBase`'s advisory
  id-range assertion and popped a modal debug dialog on the user's screen. `WxnApp::OnAssertFailure`
  now swallows exactly that one "invalid itemid value" assertion (the 16-bit `WM_COMMAND` truncation
  it guards against is already handled on the wx-event path) while letting every other assertion
  surface unchanged.

## [0.9.5] - 2026-07-19

### Added
- **The integrated terminal is now a real pseudo-terminal.** Each shell tab runs on a genuine PTY —
  **ConPTY** on Windows 10 1809+, **`forkpty`** on Linux and macOS — with a built-in terminal emulator
  (the vendored **libvterm** core, MIT © 2008 Paul Evans, fetched at build and linked as the `vterm`
  static library) driving a new owner-drawn cell-grid renderer (`src/term_backend.*`,
  `src/term_view.*`). Full-screen **TUI applications** (`vim`, `htop`, `less`, menu-driven installers),
  the shell's own **line editing / history / tab-completion / `Ctrl+R`**, **ANSI colour** (the 16
  themed colours, the 256-colour palette, and bold/italic/underline/reverse), **mouse reporting**
  (hold <kbd>Shift</kbd> to select locally instead), a 5,000-line scrollback, and live resize
  (`SIGWINCH` / `ResizePseudoConsole`) all work now — none of which the previous line-oriented console
  could do. Copy/paste are <kbd>Ctrl+Shift+C</kbd>/<kbd>Ctrl+Shift+V</kbd> so plain <kbd>Ctrl+C</kbd>
  stays the interrupt. On **Windows older than 10 1809** (where ConPTY does not exist) or any
  pty-spawn failure, the tab transparently falls back to the previous redirected-pipe console
  (line-oriented tools only, ANSI stripped); the panel, tabs, shell picker and shortcuts are identical
  either way.
- **The integrated terminal's chrome is keyboard accessible.** Its owner-drawn toolbar buttons (new
  terminal, shell picker, lights, collapse) are now focusable, activate on <kbd>Enter</kbd>/<kbd>Space</kbd>
  and draw a focus ring; the shell dropdown navigates with <kbd>Up</kbd>/<kbd>Down</kbd>/<kbd>Home</kbd>/<kbd>End</kbd>,
  picks with <kbd>Enter</kbd> and cancels with <kbd>Esc</kbd>. Since the terminal itself consumes
  <kbd>Tab</kbd> (it belongs to the shell), <kbd>Ctrl+Shift+Up</kbd> moves focus from the terminal out to
  its toolbar and <kbd>Ctrl+Shift+Down</kbd> returns — previously the chrome was reachable only with a
  mouse. Each glyph-only button also carries an accessible name, so a screen reader announces it rather
  than an unlabelled control.
- **Keyboard shortcuts are now remappable.** **Settings > Shortcut Mapper…** opens a searchable grid
  over every menu accelerator plus a curated 24-command tier of the editor's own Scintilla keys
  (word/paragraph motion and selection, document start/end, line cut/copy/delete, word/line deletion,
  overtype, …): filter by name or key, reverse-look-up a pressed combination ("Find by shortcut"), and
  **Modify / Clear / Reset** per command with press-the-keys capture. A live conflict engine tints
  colliding rows red and classifies what it found — a **hard conflict** prompts Reassign (stealing only
  the colliding key) / Keep both / Cancel, a menu key **shadowing** a different built-in editor key
  warns, and keys coexisting in mutually-exclusive focus scopes (terminal vs. editor) are correctly
  left alone. Overrides persist to **`shortcuts.json`** in the per-user data directory — a VS
  Code-style delta file (only changes are stored) that is hand-editable, written atomically on every
  edit, supports `"-command"` unbinds and a separate `"editor"` section, keeps entries it doesn't
  recognise, degrades to defaults instead of failing on damage, and goes read-only (with a marker in
  the Mapper) rather than clobbering a file written by a newer wxNote.
- **Named keymap schemes.** JetBrains-style switchable schemes, picked at the top of the Shortcut
  Mapper and persisted as `"activeScheme"`. One bundled read-only preset ships — **wxNote default**,
  the stock keys. There is deliberately no bundled Notepad++ preset: rather than shipping a guess at
  another editor's keymap, Notepad++ bindings arrive through the optional `npp-shortcuts-compat`
  plugin, which imports your actual `shortcuts.xml` as a scheme (see below). User schemes are plain
  JSON in `shortcuts.json` (id, name, optional parent to delta against), and personal Mapper edits
  live in a layer above the active scheme, so switching schemes never discards them. An
  `"activeScheme"` naming a scheme that no longer exists falls back to the default keys and is
  migrated to `wxnote.default` on the next save.
- **Notepad++ `shortcuts.xml` import.** The new optional GPL module **`npp-shortcuts-compat`**
  (`packages/npp-shortcuts-compat/`) imports a Notepad++ keybinding file as a switchable
  **"Notepad++ (imported)"** scheme — menu keys, `ScintillaKeys` editor rebinds (stored with the
  scheme, applied when it is activated) and best-effort plugin commands — via the new generic
  **`nib.keymap/1`** plugin capability, so the core never learns the `shortcuts.xml` format.
  **Automation > Run > Validate shortcuts.xml** forwards to the importer, which auto-detects the file
  (wxNote's user-data dir, or `%APPDATA%\Notepad++\` on Windows) and shows a validation report:
  imported / unmapped / unknown counts, with `UserDefinedCommands` and `Macros` parsed **as data only,
  never executed** (the plugin has no execution path; the XML parser is XXE-hardened). The imported
  scheme outlives the plugin.

### Changed
- **The default keymap now follows the modern-editor consensus.** The stock keys were audited against
  six editors — VS Code, JetBrains IDEs, TextMate, Notepad4, Sublime Text and Pulsar — and realigned
  wherever a clear consensus exists. The headline moves: toggle line comment is <kbd>Ctrl+/</kbd>
  (was <kbd>Ctrl+Q</kbd>); Save As… is <kbd>Ctrl+Shift+S</kbd> (was <kbd>Ctrl+Alt+S</kbd>), with Save
  All now menu-only; Redo answers to both <kbd>Ctrl+Y</kbd> and <kbd>Ctrl+Shift+Z</kbd> and Close to
  both <kbd>Ctrl+W</kbd> and <kbd>Ctrl+F4</kbd> (the menu shows the first key, a rebind replaces
  both); and the classic <kbd>Ctrl+D</kbd> fork is resolved the multi-cursor way — <kbd>Ctrl+D</kbd>
  now adds the next occurrence to a multi-selection (Multi-select Next, Ignore Case &amp; Whole Word)
  while Duplicate Current Line moves to <kbd>Ctrl+Shift+D</kbd>. Newly bound: increase/decrease line
  indent on <kbd>Ctrl+]</kbd>/<kbd>Ctrl+[</kbd> (<kbd>Tab</kbd>/<kbd>Shift+Tab</kbd> keep working),
  insert blank line below on <kbd>Ctrl+Enter</kbd>, the terminal on <kbd>Ctrl+`</kbd>, and zoom on
  <kbd>Ctrl+=</kbd>/<kbd>Ctrl+-</kbd> (<kbd>Ctrl</kbd>+wheel and the numpad zoom keys are
  unaffected). On the editor tier, delete line moves to <kbd>Ctrl+Shift+K</kbd> (was
  <kbd>Ctrl+Shift+L</kbd>), and the few stock Scintilla keys the new accelerators would have shadowed
  (paragraph up/down, word-part-left, copy line under reopen-closed-tab's <kbd>Ctrl+Shift+T</kbd>)
  now start unbound but stay remappable. Any old chord can be restored per command in the Shortcut
  Mapper.

### Fixed
- **The terminal's scrollbar now follows the terminal's own light/dark toggle** on Windows. Scintilla's
  scrollbar is a native control that `StyleClearAll` doesn't touch, so a dark terminal kept a bright
  white scrollbar. (On Linux/macOS the native scrollbar still tracks the *app* theme rather than the
  per-terminal toggle — the GTK scrollbar styling is installed app-wide.)
- **Plain <kbd>Ctrl</kbd>+<kbd>C</kbd> reaches the terminal on the borderless frame.** With the
  terminal focused, the frame's accelerator table translated **Edit > Copy**'s <kbd>Ctrl</kbd>+<kbd>C</kbd>
  first and ran a silent copy on the hidden editor, so the keystroke never reached the terminal view
  and the shell never got its interrupt (SIGINT). The frame accelerator table is now scoped by focus —
  emptied while the terminal owns the keyboard — so the interrupt goes through. (The native-menu frame
  keeps its accelerators on the menu bar, which cannot be scoped this way; that residual is left for a
  later phase.)

## [0.9.1] - 2026-07-18

### Added
- **Release integrity: `SHA256SUMS`.** Every release now publishes a checksum file over all assets, so
  a download can be verified (`sha256sum -c SHA256SUMS`). The code-signing pipeline (GPG signature of
  the checksums, Windows Authenticode, macOS notarization) is wired and gated on repository secrets —
  each activates automatically once its certificate/key is configured. See `docs/SIGNING.md`.

### Changed
- **The About dialog shows the version in its header** ("wxNote vX.Y.Z") instead of a dimmed line at
  the bottom.

### Docs
- Refreshed the docs to match the 0.8.5/0.9.0 reality that predated them: `npp-bridge` is no longer
  described as Windows-only (it builds on every OS and loads recompiled plugins on Linux/macOS via the
  Phase 1/2 POSIX loader), the RISC-V (`riscv64`) target and the 13-asset / 7-CI-leg counts are now
  reflected, both shipped GPL modules (`npp-bridge` and `udl-compat`) are accounted for, Scintillua
  code folding and the `terminal_selftest` target are mentioned, and stale claims (`zh_CN`'s own
  catalog, the site's tab list) were corrected.

## [0.9.0] - 2026-07-17

### Changed
- **Reworked the integrated terminal's chrome.** The panel's toolbar and tab strip were rebuilt as
  owner-drawn controls so they theme correctly on every platform - the old native `wxChoice` shell
  picker rendered a bright, un-themeable popup on the dark panel. The toolbar is now
  `[+] [shell v] … [lights] [collapse]`; the tab strip carries a themed open-terminals dropdown and a
  close button. New: a per-terminal light/dark toggle independent of the app theme, a toolbar button
  for **View > Show Terminal** (icon in all three sets), the active-tab marker in the project's accent
  green (shared with the editor's own tab strip so the two can't drift), vector toolbar glyphs (a text
  caret font-linked to a 1-bit bitmap on Windows), tooltips on the tab-strip buttons, and a chevron
  "collapse" affordance distinct from the tabs' close.
- **POSIX shell detection now probes the machine.** Linux and macOS previously offered a hardcoded
  one-or-two shells; the picker now lists `$SHELL` plus every common shell actually found on `PATH`
  (`zsh`, `bash`, `fish`, `nu`, `ksh`, `tcsh`, `dash`), deduplicated by basename so `/bin/bash` and
  `/usr/bin/bash` aren't listed twice.

### Fixed
- **The terminal's shell/tab dropdowns toggle correctly.** Clicking the shell chip while its list is
  open now closes it instead of stacking a second popup, and clicking one dropdown's button while the
  other is open switches between them. (Found by a new `wxUIActionSimulator`-driven UI self-test.)
- **`File > Open Plugins Folder` and the other folder-opening actions** now report a failure on the
  status bar instead of doing nothing, and route through one shared launcher that tries `xdg-open`
  first on Linux (where the default-application path is flaky for directories on some distros).
- A fresh `cmd.exe` no longer leaves a blank first line above its prompt.

### Internal
- Added `terminal_selftest`, a behavioural + real-click UI test target for the terminal panel.
- Repaired the `zh_CN` catalog, which had diverged from `zh` (missing 159 strings); it now compiles
  from `zh` like every other regional locale.

## [0.8.5] - 2026-07-16

### Added
- **Code folding for custom (Scintillua) languages.** The Scintillua engine now computes
  per-line fold levels in the same pass it lexes (`scintillua::Engine::lexAndFold()` in
  `src/scintillua_engine.{h,cpp}`), wiring up the `fold`/`style_at`/`fold_level` state that
  Scintillua's standalone library leaves unset, and the host applies those levels idempotently so
  collapse state survives tab switches. Custom languages defined through the Scintillua engine (and
  imported UDLs, below) can now be collapsed/expanded like the built-in lexers.
- **Cross-platform Notepad++ plugin support, Phase 2.** Three Nib capabilities were extended so the
  GPL `npp-bridge` can service more of the Notepad++ plugin ABI on every platform:
  `nib.events` now emits `DOCUMENT_OPENED`/`DOCUMENT_CLOSED`; `nib.commands` v2 adds
  `invoke_command(host, id)`; `nib.documents` v4 adds `doc_path_at(host, index, buf, cap)`
  (`include/nib/nib.h`). `npp-bridge` uses them to deliver `NPPN_FILESAVED`/`READY`/`SHUTDOWN`/
  `FILEOPENED`/`FILECLOSED` notifications and to implement `NPPM_MENUCOMMAND`, `NPPM_SAVEALLFILES`,
  `NPPM_GETCURRENTLINE`/`COLUMN`, and open-file enumeration without depending on Win32 window
  messages.
- **Version number in the About dialog.** Help > About now shows `v<version>`, compiled in from the
  CMake project version via a new `WXN_VERSION` define (and the Windows `.rc` version block is now
  generated from the same single source, `resources/app.rc.in`).
- **Download page: RISC-V and a Notepad++ comparison tab.** The project site's download page offers a
  `riscv64` Linux `.deb`, and a new "Differences" tab summarizes how wxNote differs from Notepad++.

### Changed
- **UDL import fidelity (`udl-compat`).** Imported Notepad++ User-Defined Languages now translate their
  fold markers ("Folders in code/comment", open/middle/close) into real Scintillua fold points, honor
  per-keyword-group **Prefix mode**, and correctly handle a `userDefineLang.xml` that defines multiple
  languages (a nameless `<UserLang>` no longer aborts the rest of the file). Comment/string rules are
  ordered ahead of fold rules so fold symbols inside comments and strings are not mis-detected.

### Fixed
- **Save All now saves every document.** `File > Save All` previously only wrote the active document;
  it now iterates and writes all modified files across both views.

## [0.8.0] - 2026-07-16

### Added
- **Native custom-language engine (Scintillua).** wxNote now has its own language engine
  (`src/scintillua_engine.{h,cpp}`, `scintillua::Engine`, Apache-2.0) that embeds Lua 5.4.7 +
  LPeg 1.1.0 + Scintillua's `lexer.lua` (all MIT), built as a `lua_lpeg` static library fetched and
  compiled by CMake on first configure. Custom languages are defined as Scintillua Lua/LPeg lexer
  grammars run through a Scintilla container lexer whose tags map to editor styles - a permissively
  licensed, cross-platform replacement for the Notepad++-style built-in UDL engine (removed below).
  See `docs/ARCHITECTURE.md`.
- **Two new Nib plugin capabilities.** `nib.langdef/1` lets a plugin register a language
  (`register_language(host, name, exts, scintillua_lua)`); `nib.paths/1` exposes the user-data
  directory (`user_data_dir(host, buf, cap)`) so a plugin can find `userDefineLangs/`. Both are in
  `include/nib/nib.h`.
- **Legacy UDL import via the optional `udl-compat` plugin.** Existing Notepad++
  `userDefineLang.xml` files are supported through a new optional **GPL-3.0-or-later** module,
  `packages/udl-compat`: it parses each UDL, translates it into a Scintillua lexer, and registers it
  with the core through `nib.langdef` (built as `bin/nib/udl_compat.dll`). It also ships a standalone
  `udl2scintillua` converter CLI and unit + round-trip tests. Because it is the one place that knows
  the Notepad++ UDL format, it is kept GPL and isolated from the Apache-2.0 core (and is scoped to
  move to its own repository).

### Changed
- **Menu ordering is now wxNote's own.** The within-menu item ordering and grouping across
  `src/menu_data_*.h` were reorganized into wxNote's own frequency/affinity scheme instead of
  mirroring Notepad++'s resource-file order (e.g. the View menu now leads with the panels; Edit's
  submenus are ordered by everyday frequency; the Go menu is re-clustered find→navigate→go-to). This
  is a pure presentation change: every command id and label is preserved (verified as an identical
  470-entry set) and command dispatch is id-based, so nothing behaves differently - only the on-screen
  order does. Universal editor conventions (Undo/Redo, Cut/Copy/Paste, EOL order) are kept as-is. See
  `docs/ARCHITECTURE.md`.
- **Command-id table regenerated as original constants.** `src/command_ids.h` was rewritten as an
  original table of `kCmd*` constants (grouped by wxNote's own menus, frozen numeric values pinned by
  `static_assert`) rather than reproducing Notepad++'s `menuCmdID.h` layout. The numeric values stay
  identical because the GPL `npp-bridge` forwards plugin `NPPM_MENUCOMMAND` requests by those ids; the
  names, formatting, and organization are now wxNote's own (`docs/ARCHITECTURE.md`).

### Removed
- **The built-in Notepad++-style User-Defined Language subsystem.** The in-app multi-tab "Define your
  language..." editor dialog, the per-style Styler popups, and all `userDefineLang.xml`
  loading/persistence were removed from the Apache-2.0 core (`src/udl.h` and `src/udl_lexer.h`
  deleted). Custom syntax highlighting is now provided by the Scintillua engine above; users who have
  existing Notepad++ UDL files can keep them via the optional `udl-compat` plugin. The core no longer
  contains any Notepad++ UDL-format code.

## [0.7.0] - 2026-07-11

### Added
- **ARM builds for Windows and Linux.** CI now builds two extra legs on GitHub's native arm64
  runners: a Windows **ARM64** NSIS installer (`wxNote-<version>-arm64-Setup.exe`, with a
  native-ARM64 install guard) and all four Linux package formats for **aarch64**
  (`wxnote_<version>_arm64.deb`, `wxnote-<version>-1.aarch64.rpm`, `wxNote-<version>-aarch64.AppImage`,
  `wxNote-<version>-aarch64.flatpak`). Existing x64 asset names are unchanged; the project site's
  download page now offers a per-CPU choice for Windows and Linux.

### Changed
- **Rebranded to wxNote.** The application, window titles, About box, menus, installers, and the
  8-language locale catalogs (`wxnpp.*` -> `wxn.*`) now use the wxNote name; the executable is now
  `wxnote` (`wxnote.exe` on Windows) and packaged artifacts are named `wxNote-<version>-...`.
  Existing settings and per-user data (recovery backups, UDLs, contextMenu.xml) are migrated
  automatically from the old identity on first launch. Because the single-instance channel was
  renamed with the binary, one old-version instance running alongside the first new-version launch
  won't be detected - a one-time transition quirk.
- **Core decoupled from the Notepad++ plugin ABI.** The editor core now defines its own command-id
  space (`src/command_ids.h`) and includes nothing from `include/npp-compat`; those ABI headers are
  consumed exclusively by the optional GPL `packages/npp-bridge`, which remains the (Windows-only)
  compatibility path for legacy Notepad++ plugin binaries. Notepad++ references were removed from
  core code and UI text; the Language menu item linking to Notepad++'s UDL collection site was
  removed.
- **Relicensed to Apache-2.0.** The project (everything that ships except the optional
  `packages/npp-bridge` plugin, which stays GPL-3.0-or-later under its own LICENSE because it
  reproduces Notepad++'s plugin ABI) is now under the Apache License 2.0 - the permissive goal the
  project's licensing roadmap had been driving toward since the de-GPL engineering began. All
  installer/package metadata (NSIS, RPM spec, Flatpak metainfo, macOS Info.plist) and the project
  site now state Apache-2.0. See `LICENSING.md` for the per-component record.
- **docs/ rebuilt.** The five development-era planning documents were removed and replaced with
  three permanent ones: `GOALS.md` (why the project exists and why it isn't a port), `ARCHITECTURE.md`
  (how the editor is put together), and `CREDITS.md` (everything used or consulted during
  development). A root `CONTRIBUTING.md` was added with build instructions, the PR workflow, and the
  project's code/licensing ground rules.

### Removed
- The **DansLeRuSH-Dark** color theme is no longer shipped. Its upstream license is Creative Commons
  BY-NC-SA 3.0 — the **NonCommercial** term conflicts with this project's redistribution goals (the
  MIT-style text in the file's header is labeled a "legal disclaimer" and is overridden by the
  author's explicit license declaration). Details in LICENSING.md. If you were using it, the app
  falls back to the built-in palette; the theme remains available from the author at
  https://codeberg.org/DansLeRuSH/notepad-plus-plus-dark-theme for personal installs (drop it into
  the `themes/` folder).

### Fixed
- macOS **Preferences dialog**: the section list's selected row rendered plain white instead of the
  system highlight colour, and the Close button carried a stray rectangular border in dark mode
  (native buttons are now left fully native under the dialog's dark-mode recolouring).
- **Themes on Linux/macOS**: theme files were resolved with Windows-style path separators, which
  could prevent the theme XMLs deployed next to the executable from loading on non-Windows
  platforms.

## [0.6.2] - 2026-07-11

### Added
- **Integrated terminal** (View > Show Terminal): a bottom panel with multi-tab shells and a
  per-platform picker - cmd/PowerShell always, plus pwsh/Cygwin/WSL if actually installed on Windows;
  the user's `$SHELL` (+ bash) on Linux; zsh/bash on macOS. New terminals open in the active document's
  directory. v1 is a pipe console (like Notepad++'s NppExec) - great for commands, git, compilers,
  REPLs; full-screen TUI apps (vim, htop) need the real-PTY upgrade this lays the groundwork for.
  A long-running child (e.g. `npm run dev`) and any processes IT spawns are properly terminated when
  its tab or the app closes, without freezing the UI while that happens; PowerShell/pwsh read and
  write their console codepage correctly, so typed non-ASCII characters (e.g. Polish `ą/ć/ł/...`,
  including via AltGr) aren't corrupted; output is decoded byte-accurately even if a stray non-UTF-8
  byte shows up mid-stream. Fully translated in all 8 languages.
- **File > Open Containing Folder is now dynamically filled** per what the running machine actually
  has, instead of a fixed Explorer/cmd/PowerShell-shaped list that did nothing useful on Linux/macOS.
  Windows keeps cmd/PowerShell as fixed entries (real Notepad++ plugins may invoke them) and adds
  pwsh/Cygwin/WSL only if installed; Linux lists every terminal emulator found on `$PATH`
  (GNOME Terminal, Konsole, Xfce Terminal, ...); macOS lists Terminal.app and iTerm if installed.
- macOS: **integrated top bar** (Preferences > "Show integrated top bar", now available on macOS too)
  drops the native title-bar band and re-centres the traffic-light window buttons into the toolbar row,
  matching the same layout Windows/Linux already had.
- Documents with **no recognizable extension** now get their language auto-detected from content: a
  shebang line (`#!/usr/bin/env python3` and friends), an XML/HTML prolog, or a JSON-shaped body.
- Project site: Linux screenshot in the gallery, alongside macOS under "Platforms".

### Fixed
- File > Open Containing Folder's **Explorer/File Manager and Folder as Workspace entries** were
  disabled on an untitled buffer instead of falling back to the working directory like every other
  entry in the same submenu.
- Removed the **duplicate "Folder as Workspace"** entry from the Open Containing Folder submenu - it
  did the same job as the top-level File > Open Folder as Workspace..., which already pre-selects the
  current file's folder in its picker.
- `.gitattributes` (and other dotfiles: `.gitignore`, `.editorconfig`, `.env`, ...) could pick up
  whatever icon a locally-installed tool happened to register for them in the OS shell - a
  machine-specific accident, not a deliberate choice. They now always render with our own icon.
- The **Document Map** minimap's viewport-highlight band was unreadable: the translucent-selection
  rendering path makes Scintilla skip painting the selected text entirely at minimap zoom, so the band
  read as a blank slab instead of dimmed text. It's now an opaque, background-near tint.
- Closing **Preferences** could leave the active document's syntax highlighting stripped until the
  next tab switch (the instant gutter-colour update reset every style without re-running the lexer).
- Docked-panel captions (Document Map, Function List, ...) now use a flat, chrome-coloured header
  matching the tab strip's height and colour, instead of the stock grey gradient.
- The **IconPark dark-theme icon set**: eight stroke-only glyphs (including the toolbar Close "x")
  kept their light-mode stroke colour in dark mode, reading as disabled even when enabled.
- Linux/macOS: the **integrated top bar's menu buttons** (File, Edit, ...) had no left/right padding,
  so the hover/click highlight hugged the label text edge-to-edge instead of reading as a proper
  menubar item.

## [0.6.1] - 2026-07-10

### Fixed
- Installs that never explicitly chose a theme - fresh installs, and upgrades that always ran on the
  default - now follow the OS (**System**) instead of being hard-locked into Dark by a missing legacy
  config key, which read as "the app can't tell my system is light" even though the OS-theme detection
  was fine. Users who explicitly picked Dark or Light in Preferences keep their choice.
- On Linux the **toolbar's inter-icon spacing** was much wider than on Windows/macOS: the desktop
  theme's own button metrics (padding + minimum width on GTK toolbar buttons, e.g. Mint-Y) inflate the
  row. The GTK shim now compacts the toolbar buttons and separators to match the other platforms;
  hover/pressed feedback is unchanged.
- On Linux the **"+ / v / x" tab caption buttons** were exactly as tall as the tab strip and painted
  over the 1px separator line above it; they are now slightly shorter and centred, leaving the
  separator visible.

### Changed
- Dropped MSBuild from the repo entirely: removed the vendored Lexilla MSBuild project
  (`Lexilla.vcxproj` and its code-analysis ruleset - lexilla is built by the repo's own CMake), and
  the README now spells out that Windows needs only the MSVC **compiler** from VS Build Tools - the
  build itself is CMake + Ninja; MSBuild and solution files are not used.

## [0.6.0] - 2026-07-10

### Fixed
- **Duplicate "new 1" tab on launch**: quitting with unsaved edits in the untitled buffer backs them up
  for recovery, but the next launch restored that backup *alongside* a fresh startup "new 1" instead of
  replacing it - the redundant-tab cleanup only counted reopened session files, never recovery restores.
  The pristine startup tab is now dropped whenever the restore pass brings in any page (session file or
  recovery backup), and a pristine-empty untitled buffer is no longer backed up at all (there is nothing
  to recover), which also stops empty backup files accumulating on disk.
- On Linux the **"+ / v / x" tab caption buttons rendered blurry**: the 0.5.9 custom-painted caption
  buttons rasterized their glyphs at 12 px while the caption icons are drawn for 16 px. They now render
  at 16 px (the window-control buttons keep their original 12 px).
- On Linux the **coloured tint down the right edge** survived the 0.5.9 scrollbar fix for two reasons,
  both corrected. The big one: every declaration in the shim's CSS carried `!important`, which GTK3's CSS
  parser **does not support** - each such declaration is rejected wholesale ("Junk at end of value"), so
  the entire 0.5.8/0.5.9 stylesheet silently loaded *empty* and the theming never took effect at all. The
  CSS is now valid GTK CSS (the max-priority provider wins the cascade on its own; GTK sorts by provider
  priority, `!important` isn't part of its model). Second, the fix targeted the wrong widget: wxSTC never
  instantiates the native ScintillaGTK backend - its scrollbar is wx's own `GtkScrolledWindow` scrollbar -
  and the accent "tint" can also arrive on the scrolled window's `overshoot`/`undershoot`/`junction`
  decoration nodes. The shim now covers those nodes too, and attaches the provider directly to every
  scrollbar widget in the window as well as screen-wide.
- **Unsaved-changes recovery after a crash**: recovery backups were only restored when the previous run
  exited cleanly (the same flag that gates session restore), so the launch right after a crash - the very
  scenario the backups exist for - silently skipped them, and the backed-up tabs "resurrected" one launch
  later instead. The recovery pass now runs on every launch, independent of the clean-exit flag.

- The Windows exe's **file-properties version metadata** (Explorer > Properties > Details) was stale
  at 0.3.0.0; it now matches the release version.

### Added
- Project site: macOS screenshot in the gallery, under a new "Platforms" category.

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
