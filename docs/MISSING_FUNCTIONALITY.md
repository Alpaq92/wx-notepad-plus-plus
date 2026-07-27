# Missing / Incomplete Functionality

A gap analysis of wxNote against Notepad++ and its own cross-platform promise, produced by a
codebase-wide audit (6 parallel investigators, every finding grounded in a `file:line` or doc quote).

- **Baseline:** wxNote 0.11.0 (released 2026-07-27), master @ 2026-07-27.
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
| ~~**Periodic / timed backup (session snapshot)**~~ | ✅ **done** (0.10.0) | 30 s `wxTimer` `onBackupTimer` snapshots every dirty buffer (foreground + background via `peekDoc`) — src/main.cpp | a crash between saves is now recoverable |
| ~~**External-change detection ("file modified on disk, reload?")**~~ | ✅ **done** (0.10.0) | `checkExternalChange` stamps mtime+size on load/save, re-checks on refocus + tab switch, prompts to reload — src/main.cpp | verified live; no longer silently overwrites external edits |
| **Plugins Admin** (in-app browse/install/update) | planned-deferred | Extensions menu only offers "Open Plugins Folder…" ([site/docs/menus.md](../site/docs/menus.md)) | No GUI catalog; plugins are dropped in by hand. Full design (static catalog + GitHub-Release assets + Ed25519 signing + curated PRs; Pulsar/VS Code/Sublime survey) in [PLUGINS_ADMIN_DESIGN.md](PLUGINS_ADMIN_DESIGN.md). |

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
| **Shortcut Mapper** | partial (Macros added) | saved macros now appear (seeded) + a **Show: All/Menu/Editor/Macros** category selector — [shortcut_mapper_dialog.h](../src/shortcut_mapper_dialog.h) | Macros are bindable now; Run / Plugin-command categories still deferred (no dynamic command list to bind yet) |
| **Function List** | partial (extensible) | +5 built-in languages (php/ruby/sql/sh/powershell) and a user overlay `functionList.conf` (add/override languages with regex rules) merged over the built-ins — [main.cpp](../src/main.cpp) `flRules`/`loadFunctionListRules`/`flLangKey` | 13 built-in languages + user-extensible; still `std::regex` (not N++'s GPL `functionList.xml`) |
| **Project Panels 1/2/3** | partial | three ids → one `toggleProjectPanel()` — [main.cpp:10833](../src/main.cpp) | three menu entries share one backing panel |
| **Style Configurator** | partial | only fg/bg + Bold/Italic — [main.cpp:9784](../src/main.cpp) | no font name/size/underline, no keyword-set editor, no global overrides |
| **Autocomplete** | partial (broadened) | `keywordsForExt()` is now a ~25-language table reusing the highlighter's keyword sets + curated PHP/Kotlin/Swift/R/YAML/HTML (Scintillua MIT / SciTE HPND) — [main.cpp](../src/main.cpp) `keywordsForExt` | keyword + document-word completion for C-family, JS/TS, Python, Go, Rust, Lua, SQL, CSS, Bash, Perl, Ruby, PowerShell, JSON, PHP, Kotlin, Swift, R, YAML, HTML; still keyword-list-based, not semantic |
| **Calltips** | partial | harvested from open doc — [main.cpp](../src/main.cpp) `calltip*` | no API/signature (`.xml`) database |
| **Regex** | partial (unified) | Find, the Find-dialog's Find-in-Files tab, and the Find-in-Files menu now share ONE engine — Scintilla's `SCFIND_CXX11REGEX` via a scratch buffer (`fifScanFile`); the old byte-level `std::regex` menu scanner was deleted — [main.cpp](../src/main.cpp) | the two engines no longer diverge (Unicode- and whole-word-correct everywhere); still ECMAScript/`std::regex`, not Boost/PCRE — lookbehind etc. unavailable (PCRE2 is a roadmap option) |
| **Large-file handling** | ✅ **guarded** | per-page `largeFile` flag set at load (> 16 MiB, or a line > 50 000 chars) skips Scintillua/Lexilla styling + the whole-buffer re-lex — [main.cpp](../src/main.cpp) `loadFile` / `setLexerForFile` / `onStcStyleNeeded` | no more re-lex-per-keystroke cliff; picking a Language from the menu forces styling back on. Both thresholds are configurable in **Preferences ▸ Editing** (size 0 = guard off), and the guard now also covers the Function List's whole-buffer symbol scan |

## 4. Plugin API (npp-bridge / Nib) completeness

| Gap | Status | Evidence | Impact |
| --- | --- | --- | --- |
| **`SCN_CHARADDED` / `SCN_MARGINCLICK` / dwell / hotspot** not delivered | partial | `forward_nib_event` only synthesizes MODIFIED/UPDATEUI/SAVEPOINT; no such `NibEventKind` — [nib.h:180-241](../include/nib/nib.h) | **biggest compat gap** — breaks autocomplete/XML-Tools/bracket-helper plugins; needs new core event kinds |
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
| **Font ligatures on Windows** | blocked-upstream | Scintilla draws via GDI; `SC_TECHNOLOGY_DIRECTWRITE` never set — [main.cpp:11795](../src/main.cpp) | GDI does no OpenType shaping |

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
3. **Plugin `SCN_CHARADDED` / `SCN_MARGINCLICK`** — unblocks a whole plugin class (needs new core event kinds).
4. **Larger builds**: Spell check, File Compare, macro persistence, Function List/Style Configurator depth.
5. **Track, don't act**: Change History and Windows ligatures (upstream-blocked); signing (secrets).

*Regenerate this audit by re-running the `missing-functionality-audit` workflow.*
