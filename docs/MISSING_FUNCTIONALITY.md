# Missing / Incomplete Functionality

A gap analysis of wxNote against Notepad++ and its own cross-platform promise, produced by a
codebase-wide audit (6 parallel investigators, every finding grounded in a `file:line` or doc quote).

- **Baseline:** wxNote 0.14.0 (released 2026-07-31), master @ 2026-07-31.
- **Scope:** what a user or plugin author would find missing, non-functional, or thinner than
  Notepad++ — not code-quality nits.

**Status legend**

| Status | Meaning |
| --- | --- |
| **absent** | not implemented at all |
| **stubbed** | menu/UI item exists but does nothing (routes to `notImpl`) |
| **partial** | works, but narrower than Notepad++ (or only on some platforms) |
| **blocked-upstream** | can't be fixed locally; gated on wx / Scintilla / a rendering backend |
| **planned-deferred** | intentionally postponed; infra may already be wired |

---

## 1. Completely absent (Notepad++ has it; we have nothing)

| Feature | Status | Evidence | Notes |
| --- | --- | --- | --- |
| **Spell checking** | ✅ **done, all platforms** (0.10.0) | Native-first behind an `ISpellEngine` interface — [spell_engine.h](../src/spell_engine.h): Windows `ISpellChecker`, macOS `NSSpellChecker`, and **bundled Hunspell + SCOWL en_US** (`third_party/hunspell/`, `resources/dictionaries/`) as the Linux engine + cross-platform fallback. Squiggle UX + right-click menu in [main.cpp](../src/main.cpp) `checkVisibleSpelling` / `addSpellContext`; verified by the headless `hunspell_selftest` on all CI arches + runtime-verified on Windows | View ▸ Spell Check: squiggles + camelCase-aware tokenization; right-click **suggestions / Add to Dictionary (persisted) / Ignore**; wrong-OS-language fallback fixed (bundled English wins over an unrelated OS pack); user drop-in dictionaries via `<user-data>/dictionaries/` |
| ~~**File Compare / diff**~~ | ✅ **done** (0.10.0) | side-by-side Compare (View ▸ Compare) via a hand-rolled Myers O(ND) engine — [diff_myers.h](../src/diff_myers.h) + `compareWith` in [main.cpp](../src/main.cpp); engine/plan self-test `tests/diff_myers_test.cpp` (79 tests) | markers + annotation filler + intra-line indicators + scroll-sync; runtime-verified |
| ~~**Periodic / timed backup (session snapshot)**~~ | ✅ **done** (0.10.0, hardened later) | 30 s `wxTimer` `backupTick` snapshots every dirty buffer (foreground + background via `peekDoc`) — src/main.cpp | a crash between saves is recoverable. Hardened since: **skip-if-unchanged** (an edit serial per page — an idle-dirty buffer no longer re-copies + rewrites every 30 s in perpetuity), **atomic replace** (`.bak.tmp` + rename; a crash mid-backup can no longer destroy the previous good snapshot), **direct UTF-8 write** (one document copy per snapshot instead of three — the old spelling round-tripped through UTF-16), and a **size-stretched cadence** (`wxnBackupThrottleMs`: every tick ≤ 32 MiB, +30 s per 32 MiB above, capped at 5 min; the exit-path backup never waits) |
| ~~**External-change detection ("file modified on disk, reload?")**~~ | ✅ **done** (0.10.0) | `checkExternalChange` stamps mtime+size on load/save, re-checks on refocus + tab switch, prompts to reload — src/main.cpp | verified live; no longer silently overwrites external edits |
| **Plugins Admin** (in-app browse/install/update) | foundations built, UI deferred | Extensions menu only offers "Open Plugins Folder…" ([site/docs/menus.md](../site/docs/menus.md)) | No GUI catalog yet; plugins are still dropped in by hand — but that now works on installed builds (plugins load from a user-writable dir), and the signed-catalog core (minisign/Ed25519 verify + parser + validators, 83 tests) is in the tree unused. Browse, install, and update detection all remain gated on there being third-party plugins to list. Status table + open decisions in [PLUGINS_ADMIN_DESIGN.md](PLUGINS_ADMIN_DESIGN.md). |

## 2. Cross-platform parity holes — Windows-only, dead on Linux/macOS

For a *cross-platform* editor these are the most glaring gaps. Several have easy portable fixes.

| Feature | Status | Evidence | Portable fix |
| --- | --- | --- | --- |
| ~~**MD5 / SHA-1 / SHA-256 / SHA-512 generators** (12 commands)~~ | ✅ **done** (0.10.0) | one portable path everywhere via a hand-rolled engine — [hash_algos.h](../src/hash_algos.h) (BCrypt removed); self-test `tests/hash_test.cpp` (25/25 vs Python hashlib) | endian-safe, > 4 GB safe |
| ~~**Encoding ▸ Character Set** (~50 code pages)~~ | ✅ **done** (0.10.0) | portable `wxCSConv` name table off Windows (Win32 kept on Windows) — [main.cpp](../src/main.cpp) `charsetNameForCp`/`interpretCharset`/`encodeForPage`; self-test `tests/charset_selftest.cpp` | CP720 (Arabic DOS) unavailable on POSIX → loud status-abort, doc untouched; POSIX branch validated by CI |
| ~~**Binary clipboard** (Copy/Cut/Paste Binary Content)~~ | ✅ **done** (0.10.0) | portable `wxCustomDataObject` off Windows (raw Win32 kept on Windows) — [main.cpp](../src/main.cpp) `copyCutBinary`/`pasteBinary` | GTK/macOS same-process round-trip is high-confidence-by-source but wants a live Linux/macOS check |
| ~~**Paste HTML / RTF Content**~~ | ✅ **done** (0.10.0) | now cross-platform via `wxHTMLDataObject` + per-platform RTF name — [main.cpp](../src/main.cpp) `pasteHtmlContent`/`pasteRtfContent` | HTML uses wx's portable `wxDF_HTML` (also strips the CF_HTML header on Windows now); RTF reads `text/rtf`/`public.rtf`/`Rich Text Format` per OS |
| ~~**View ▸ Always on Top**~~ | ✅ **done** (0.10.0) | portable `wxSTAY_ON_TOP` style off Windows (raw `SetWindowPos` kept on Windows for the borderless frame) — [main.cpp](../src/main.cpp) `toggleAlwaysOnTop` | POSIX branch validated by CI |
| ~~**File ▸ Read-Only Attribute** (on-disk)~~ | ✅ **done** (0.10.0) | portable `stat`/`chmod` on the write bits off Windows (`Get/SetFileAttributesW` kept on Windows) — [main.cpp](../src/main.cpp) `toggleSystemReadOnly` | POSIX branch validated by CI |
| **Precompiled N++-ABI (.dll) plugins** | partial | binary ABI is Windows-only ([site/docs/plugins.md](../site/docs/plugins.md)) | recompiled plugins already work cross-platform |

*The in-editor read-only toggle and basic ANSI/UTF-8/UTF-16 encodings **do** work everywhere — only the
listed code paths are Windows-gated. The integrated borderless title bar being Windows/Linux-only is
**by design** (macOS uses the native path in `macos_native.mm`), so it is not counted as a gap.*

## 3. Present but thinner than Notepad++

| Feature | Status | Evidence | Gap |
| --- | --- | --- | --- |
| **Macros** | ✅ **persist + bindable** | serialized to `macros.dat` (base64 name/text steps, atomic write) and seeded into the keymap as `macro.<uid>` — [main.cpp](../src/main.cpp) `loadSavedMacros`/`saveSavedMacros`/`seedMacroKeymapDefaults` | saved macros survive restart, can be bound to a shortcut in the Shortcut Mapper, and are renamed / deleted / reordered from **Macro ▸ Manage Saved Macros…** (`manageMacros`), which re-points uid-keyed bindings at the shifted menu ids via `remapCmdId`/`removeDefault` |
| **Shortcut Mapper** | partial (Macros + Plugin commands added) | saved macros and **Nib plugin commands** now appear (both seeded) + a **Show: All/Menu/Editor/Macros/Plugin commands** category selector — [shortcut_mapper_dialog.h](../src/shortcut_mapper_dialog.h), `seedNibCommandKeymapDefaults` in [main.cpp](../src/main.cpp) | Macros and plugin commands are bindable. Plugin rows are keyed `plugin.<id>` on the stable id the plugin passed to `nib.commands`, never on the positional `NIB_CMD_BASE + i` menu id, so installing or removing a plugin cannot repoint an existing binding at a different command. Seeded after `loadNibPlugins()` (end of the shell frame ctor) rather than beside the macro seeding in `buildMenuBar()`, which runs before any plugin exists. A command registered with no id is not bindable — there is nothing stable to key it to. **Run commands remain the one missing category, and not for want of a selector**: wxNote has no user-defined Run commands to list. The Run menu holds only the two static entries (`run.execute`, `run.validateShortcutsXml`), already bindable under Menu commands, and there is a single `RunCommand` setting rather than a named, persisted list. That list is the prerequisite, not the mapper |
| **Function List** | partial (extensible) | +11 more built-in languages (markdown/yaml/ini/css/makefile/dockerfile/batch/perl/kotlin/swift/r), name-based detection for extension-less `Makefile`/`Dockerfile`, plus the user overlay `functionList.conf` merged over the built-ins — [main.cpp](../src/main.cpp) `flRules`/`loadFunctionListRules`/`flLangKey` | 24 built-in languages + user-extensible; still `std::regex` (not N++'s GPL `functionList.xml`), so nesting is brace/indent-scanned rather than parsed — **but the scan is no longer regex-blind**. `flCollect` now takes a per-byte comment/string mask built by `flProseMask` from the style bytes of the lexer that is *already running* (Lexilla via `TagsOfStyle`, or the Scintillua container styles), so a brace inside a construct no pattern covers — a multi-line raw string, a nested or doc comment, an f-string — is no longer counted as nesting and no longer swallows the rest of the file. It also skips the regex mask pass entirely, and with it the `std::regex_error(error_stack)` that pass throws on a large block comment. The mask is preferred, never required: an unstyled container buffer or a lexer exposing no comment/string tags falls back to the old regex zones |
| **Project Panels 1/2/3** | ✅ | three independent panels — per-panel tree, workspace and dock pane — [main.cpp](../src/main.cpp) `ProjectPanelState m_proj[3]` / `toggleProjectPanel(int)` | each menu entry now has its own backing panel, as the menu always implied |
| **Style Configurator** | partial | fg/bg, Bold/Italic/**Underline**, per-style **font name / size / weight**, and a **`genericLangDef`** entry that finally makes Scintillua-lexed (custom/UDL) languages follow the theme, plus **Save As…** and **Revert changes** — `onStyleConfig` / `synthesizeGenericStyles` / `applyStyleFont` / `applyScintilluaStyles` / `userThemeDir` / `reloadThemeLive` in [main.cpp](../src/main.cpp) (the old `main.cpp:9784` anchor was stale) | Underline now renders: bit 4 of Notepad++'s `fontStyle` was parsed and saved all along but never applied, so 167 authored styles across the 28 shipped themes drew plain. Custom-language colours were previously a fixed palette that ignored the theme entirely; the `genericLangDef` block is derived from each theme's own `cpp` styles at load time and written to the theme file only once the user edits one of them, so it works for third-party Notepad++ themes too. `keyword2..8` stay on the built-in palette by design — no Scintillua lexer emits those tags; they are a UDL's own keyword groups, which Notepad++ stores in the UDL file rather than the theme. Per-style font: `fontName` and `fontSize` are Notepad++'s own `WordsStyle` attributes and round-trip to it exactly (0 of 49,507 shipped styles set either, so nothing regresses); `fontWeight` is a **wxNote extension** — Notepad++ has no such attribute, so a theme round-tripped through it keeps the bold flag but loses the exact weight, and the attribute is written only onto styles that actually use one. An explicit weight overrides the bold bit at render time. **Save As…** copies the whole active theme into `<userDataDir>/themes/<name>.xml` and folds the pending edits into the copy, which is also the only theme folder an installed build can write — `<exeDir>/themes` is not user-writable, and the light-mode `Default` maps to `stylers.model.xml` there, so Save As is the supported way to keep edits to a bundled theme. A user theme shadows a bundled one of the same name and is listed once, not twice. **Revert changes** re-reads the theme from disk mid-session (what Cancel does on close) without re-pinning the theme name, so a window still following dark/light mode keeps following it. The dialog is now sized by its sizer rather than a fixed 860×470, which was clipping the font-name combo in Polish. Still no keyword-set editor and no global overrides. Per-style **strike/overline are unreachable** — no upstream Scintilla has them (Notepad4 gets them from a private fork, on message ids upstream assigns to `SCI_MARKERSETFORE`/`BACK`) |
| **Autocomplete** | partial (broadened) | keywords now come from the set actually handed to the active lexer (`EditorPage::lexKeywords`), falling back to the ~25-language `keywordsForExt()` table (Scintillua MIT / SciTE HPND); document words styled as comment or string are filtered out — [main.cpp](../src/main.cpp) `collectKeywords`/`collectWords`/`proseStylesForFilter` | keyword + document-word completion for C-family, JS/TS, Python, Go, Rust, Lua, SQL, CSS, Bash, Perl, Ruby, PowerShell, JSON, PHP, Kotlin, Swift, R, YAML, HTML; picking a Language by hand now changes the completions too. Still keyword-list-based, not semantic. Document-word harvest is bounded on every file: whole doc up to 1 MiB (style-filtered), a 1 MiB caret-centered window above that — filtered up to the caret, fragments at the window's cut edges dropped, and words beyond the window simply not offered (the locality trade). The calltip signature scan shares the same window |
| **Calltips** | partial | harvested from open doc — [main.cpp](../src/main.cpp) `calltip*` | no API/signature (`.xml`) database |
| **Regex** | partial (unified) | Find, the Find-dialog's Find-in-Files tab, and the Find-in-Files menu now share ONE engine — Scintilla's `SCFIND_CXX11REGEX` via a scratch buffer (`fifScanFile`); the old byte-level `std::regex` menu scanner was deleted — [main.cpp](../src/main.cpp) | the two engines no longer diverge (Unicode- and whole-word-correct everywhere); still ECMAScript/`std::regex`, not Boost/PCRE — lookbehind etc. unavailable (PCRE2 is a roadmap option) |
| **Large-file handling** | ✅ **guarded** | per-page `largeFile` flag set at load (> 16 MiB, or a line > 50 000 chars) skips Scintillua/Lexilla styling + the whole-buffer re-lex — [main.cpp](../src/main.cpp) `loadFile` / `setLexerForFile` / `onStcStyleNeeded` | no more re-lex-per-keystroke cliff; picking a Language from the menu forces styling back on. Both thresholds are configurable in **Preferences ▸ Editing** (size 0 = guard off), and the guard now also covers the Function List's whole-buffer symbol scan. **The typing path no longer scales with the file at all**: the autocomplete word harvest and the `(`-triggered calltip signature scan each used to copy + scan the whole buffer per keystroke (measured 300 ms at 16 MiB, 1.19 s at 64 MiB — `funclist_selftest --bench`); both now read at most a 1 MiB caret-centered window (`wxnHarvestWindow` in [main.cpp](../src/main.cpp), ~21 ms adversarial worst case) on **any** size of file — deliberately not gated on `largeFile`, so completion keeps working on big files, just window-bounded |

## 4. Plugin API (npp-bridge / Nib) completeness

| Gap | Status | Evidence | Impact |
| --- | --- | --- | --- |
| ~~**`SCN_CHARADDED` / `SCN_MARGINCLICK` / dwell / hotspot** not delivered~~ | ✅ **done** — `nib.events` **v5** | seven new `NibEventKind`s (CHAR_ADDED / MARGIN_CLICK / DWELL_START / DWELL_END / the three HOTSPOT_*) — [nib.h](../include/nib/nib.h); fired from `onStcCharAdded` / `onStcMarginClick` / the dwell+hotspot binds in [main.cpp](../src/main.cpp), translated to the matching `SCN_*` by `on_nib_event_v5` in `npp_bridge.cpp`; 7 assertions in `tests/bridge_selftest.cpp` pin both the code and the payload | was the **biggest compat gap** — autocomplete/XML-Tools/bracket-helper plugins were inert. Each fires AFTER the host's own handling (matching where N++ notifies), and the payload rides its own union member so `sizeof(NibEvent)` is unchanged (static_assert in main.cpp). Dwell/hotspot stay dormant until a plugin sets a dwell time / hotspot style, as under N++ |
| **Plugin docking (DMM\*) on Linux/macOS** | partial | DMM* cases gated on `g_win32` — `npp_bridge.cpp:1090` | recompiled POSIX plugins can't surface docked UI (silent `TRUE` no-op) |
| **Portable panels are text-only** | partial | `NibPanelsApi` = register/set/append text — [nib.h:363-374](../include/nib/nib.h) | no rich widgets / tree views / Scintilla views |
| **`NPPM_CREATESCINTILLAHANDLE`** | stubbed | `out = 0` — `npp_bridge.cpp:1428` | plugins needing a hidden editor fail |
| **Modeless-dialog keyboard nav** | partial | accepted but no `IsDialogMessage` relay — `npp_bridge.cpp:1421` | Tab/Enter/arrows broken in plugin dialogs |
| **Dark-mode theming of plugin dialogs** | stubbed | `NPPM_DARKMODESUBCLASSANDTHEME` → 0 — `npp_bridge.cpp:1431` | plugin dialogs stay light |
| **`nib.events` has no unsubscribe** | partial | subscribe-only — [nib.h:256-267](../include/nib/nib.h) | plugins can't detach; host must clear before unload |
| **`SWITCHTOFILE`/`RELOAD*` duplicate tabs; lang-type = extension-only** | partial | `npp_bridge.cpp:1036,1106` | correctness gap, not just a stub |
| **Plugin toolbar buttons couldn't follow the host's icon pack / theme** | ✅ **resolved** | `nib.toolbar/2` `add_tool_named` + `NPPM_ADDTOOLBARICONBYNAME` — the plugin names a host icon asset instead of shipping pixels — [nib.h](../include/nib/nib.h), `npp_bridge.cpp` | plugin-supplied RGBA is frozen at whatever pack/theme was active when it was rasterised; a named icon is drawn through the host's own `icon()` path, so it tracks the user's icon-set choice **and** light/dark, including the per-pack retints. Also works on Linux/macOS, where `NPPM_ADDTOOLBARICON*` cannot (no `HBITMAP`) |
| **~44–77 of 118 `NPPM_*` served** | partial | additive; [site/docs/plugins.md](../site/docs/plugins.md) undercounts vs bridge README | doc is also stale (quick fix) |
| **Raw-Win32 / DockingFeature plugins** | planned-deferred | out of scope ([docs/ARCHITECTURE.md](ARCHITECTURE.md)) | native-UI plugins need a separate port |

## 5. Upstream-blocked / rendering-backend (not fixable locally)

| Feature | Status | Evidence | Blocker |
| --- | --- | --- | --- |
| **Change History** (Next/Prev/Clear) | blocked-upstream | all three → `notImpl` — [main.cpp:10775](../src/main.cpp) | needs Scintilla ≥ 5.3; wx vendors 5.0.0 |
| ~~**Font ligatures on Windows**~~ | ✅ **done** (unreleased) — *this row was WRONG* | Not upstream-blocked at all. wx 3.3.1 ships the full Direct2D surface in `src/stc/PlatWX.cpp` behind `HAVE_DIRECTWRITE_TECHNOLOGY`, which it defines whenever `__WXMSW__ && wxUSE_GRAPHICS_DIRECT2D` — both true in this build (`wxUSE_GRAPHICS_CONTEXT` is 1) — and `ScintillaWX::WndProc` handles `SCI_SETTECHNOLOGY` there. `dumpbin` finds **312** `SurfaceD2D`-family symbols in the linked `wxmsw33u_stc.lib`. The app simply never sent the message. | Now **Preferences ▸ Editing ▸ Smoother text rendering (DirectWrite)**, on by default, applied live to both split views. It fails soft: if D2D/DirectWrite will not load, ScintillaWX keeps GDI rather than losing text. Runtime-verified — `DWrite.dll` + `d2d1.dll` load into the process. Note this does not by itself add ligatures: the shipped faces (Cascadia Mono, Iosevka Fixed) are the ligature-free variants deliberately, so what changes is glyph quality and sub-pixel positioning |

## 6. Planned-deferred (infra wired, gated on non-code factors)

| Item | Evidence |
| --- | --- |
| **Release code-signing** (Win Authenticode, macOS notarization, Linux GPG) | pipeline wired, gated on secrets — [docs/GOALS.md](GOALS.md), [docs/SIGNING.md](SIGNING.md); only `SHA256SUMS` ships today |
| **npp-bridge Phase 2 runtime-verify on real Linux/macOS** | CI-compiled only; no recompiled `.so`/`.dylib` runtime-verified — [docs/ARCHITECTURE.md](ARCHITECTURE.md) |
| **udl-compat fidelity** (nested delimiters, per-slot fonts, middle fold keywords) | [packages/udl-compat/README.md](../packages/udl-compat/README.md) |
| **Terminal IME + complex-script/CJK shaping + double/curly underlines** | [site/docs/terminal.md](../site/docs/terminal.md) |

---

## Shipped in 0.10.0

- **Spell check** (Hunspell + native), **File Compare**, periodic backup, external-change detection,
  portable **hashes** / **code-page encodings** / **binary clipboard**, **Always on Top**, **Read-Only
  Attribute**, and **Copy as HTML / Copy as RTF** plus cross-platform **Paste HTML/RTF**. The whole Paste
  Special clipboard cluster now works on Windows, Linux and macOS — no new dependency (bundled wxWidgets
  only). See the [CHANGELOG](../CHANGELOG.md) 0.10.0 section.

## Suggested priority

1. ~~**Data-safety**~~ — ✅ **DONE (0.10.0)**:
   - **a. External-change-on-disk detection** ✅ — stamps each buffer's mtime+size; re-checks on window
     refocus + tab switch; prompts to reload. (`checkExternalChange` in src/main.cpp.)
   - **b. Periodic/timed backup** ✅ — a 30 s `wxTimer` snapshots every buffer with unsaved edits to the
     recovery store (foreground + background via `peekDoc`), so a crash between saves is recoverable.
     (`onBackupTimer` in src/main.cpp.)
2. **Remaining easy cross-platform wins** (clipboard is now done): **Always on Top** (`wxSTAY_ON_TOP`),
   then portable **hashes** and **code-page encodings**.
3. ~~**Plugin `SCN_CHARADDED` / `SCN_MARGINCLICK`**~~ — ✅ **DONE**: `nib.events` v5 delivers char-added,
   margin-click, dwell and the three hotspot clicks; the bridge translates each to its `SCN_*`. The
   plugin class that keys off `SCN_CHARADDED` (autocomplete, XML-Tools, bracket helpers) is unblocked.
   Still open in this section: plugin docking on Linux/macOS, rich portable panels,
   `NPPM_CREATESCINTILLAHANDLE`, modeless-dialog keyboard nav, plugin-dialog dark mode, and
   `nib.events` still having no unsubscribe.
4. **Larger builds**: Style Configurator depth (fg/bg + bold/italic only), a Calltips API/signature
   database, and Plugins Admin (foundations built; browse, install, and update detection deferred -
   `PLUGINS_ADMIN_DESIGN.md`).
5. **Track, don't act**: Change History (genuinely upstream-blocked — needs Scintilla ≥ 5.3, wx vendors
   5.0.0); signing (gated on secrets). **Windows ligatures were listed here in error** and turned out to
   be a single unsent message — a reminder that "blocked-upstream" is a claim worth re-checking against
   the vendored source rather than inheriting between audits.

*Regenerate this audit by re-running the `missing-functionality-audit` workflow.*
