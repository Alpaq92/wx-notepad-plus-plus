# wxNotepad++ — Leftover Code & Dependency License Audit

**Repo:** github.com/Alpaq92/wx-notepad-plus-plus · **Branch audited:** wx-migration (merged to master) · **Date:** 2026-07-05

> **Remediation addendum (2026-07-05, same day):** every actionable item this audit surfaced has since
> been fixed in the tree; sections below are annotated where the state changed after the audit ran.
> The changes: (1) `resources/icons/CREDITS.md` added; (2) the misleading theme-XML comment in
> `src/main.cpp` reworded; (3) the freeware Inno Setup installer toolchain replaced with the
> zlib-licensed open-source NSIS (`installer/windows/wxnpp.nsi`); (4) the four reference-project
> name-drop comments in `src/main.cpp` removed/reworded; (5) the Function List rules rewritten -
> re-derived from each language's published grammar with per-rule derivation notes, new capabilities
> (C++ operators/ctors/dtors/qualified names, TS/JS methods + template-literal masking, package-private
> Java methods, C# expression-bodied members, PEP 695) and three new languages (Go, Rust, Lua), so the
> old "distilled from N++'s functionList XML" provenance no longer applies to any shipped rule;
> (6) `src/npp_menu.h`'s header + a new `LICENSING.md` "Compatibility surfaces" section now document the
> menu data's interoperability/method-of-operation rationale precisely.

**Bottom line:** No improper leftover Notepad++ source code was found anywhere in the current tree. The one place that historically held verbatim upstream GPL headers (`third_party/notepad-plus-plus/`, 4 files) was deliberately removed on 2026-06-25 and replaced with a clean-room, Apache-2.0 ABI-compatibility layer (`include/npp-compat/`) whose "only numeric IDs/struct layouts are reproduced" claim holds up under direct diff verification. All third-party dependencies (wxWidgets, Scintilla, Lexilla, wxBorderlessFrame, GTK3, plus CI/installer tooling) are legitimately vendored or linked with correctly identified, compatible permissive/LGPL licenses, and are consistent with the project's GPLv3 status. Non-code assets (icons, themes, app icon) are all provenance-documented at the root (`LICENSING.md`/`NOTICE`) and, with one minor exception, at the asset-directory level too. The only real gap found was a documentation-consistency nit, not a legal problem: `resources/icons/` lacked its own in-directory `CREDITS.md` the way its two colored sibling icon sets have (since fixed — see the remediation addendum above). No agent found any undisclosed copying, unattributed asset, or unidentified dependency.

---

## 1. Notepad++ Source Code Reuse

**What was deliberately reused, and its current status:** The repo's history shows a discrete "de-GPL groundwork" moment. A directory `third_party/notepad-plus-plus/` was created (commit `94c927e`) containing exactly 4 verbatim upstream Notepad++ headers — `Docking.h`, `Notepad_plus_msgs.h`, `PluginInterface.h`, `menuCmdID.h` — each carrying the genuine "This file is part of Notepad++ project / Copyright (C)2025 Don HO" GPLv3 banner. This directory was then **fully deleted** in commit `1d8f0e1` ("de-GPL groundwork + GPL-now/permissive-later licensing," 2026-06-25), whose message explicitly states: "Remove third_party/notepad-plus-plus/ (GPL headers); repoint CMake include dirs." This was confirmed both by the absence of the directory in the current working tree and by inspecting the historical git object (`git show 1d8f0e1~1:third_party/notepad-plus-plus`), which showed no tampering — a genuine, unmodified upstream copy right up until its removal.

**What replaced it — clean-room reimplementation:** `include/npp-compat/` (`menuCmdID.h`, `Notepad_plus_msgs.h`, `PluginInterface.h`, `Docking.h`, plus original files `npp_plugin_port.h` and `abi_layout_asserts.h`) now serves the same ABI-compatibility purpose under Apache-2.0. This was independently verified by diffing each current header against the historical verbatim file it replaces:
- License banners were replaced with an SPDX Apache-2.0 / wxNotepad++ copyright header explaining the clean-room rationale.
- N++'s multi-paragraph prose documentation comments for each `NPPM_*` message (the bulk of the original file — roughly 1195 of 1288 lines in one case) were stripped entirely.
- Struct field names/order/types were kept identical where required for binary/source compatibility (e.g., `NppData._nppHandle`), which is unavoidable — these are ABI facts, not creative expression.
- Numeric `#define` values for `NPPM_*`/`IDM_*` were preserved exactly, again because they constitute the wire protocol and must match.
- The one area with the least "daylight" from the original is the `LangType` enum, whose member list and order mirror N++'s exactly (necessary because enum values must match for ABI compatibility) — this is inherent to the compat layer's function, not evidence of sloppy copying.

This matches what `LICENSING.md` itself says: the headers are "Apache-2.0 expression, but they functionally reproduce N++'s GPL ABI (gate #1) ... to be replaced by the permissive Nib API" — the documentation and the file contents agree.

**Clean-room implementation code:** `packages/npp-bridge/npp_bridge.cpp` (GPL-3.0-or-later, by the project's own declared choice, since it is the ABI-consuming bridge) and `packages/test_plugin/TestPlugin.cpp` were both read in full. Neither contains any N++ internal implementation logic (no classes, algorithms, or internal `.cpp` code) — they consist of original glue code that calls into the fixed ABI surface (numeric constants and exported symbol names that any conforming plugin loader/plugin must use).

**Tree-wide search for real N++ implementation code:** A case-insensitive grep across `src/`, `packages/`, and `include/` for genuine N++ internal class/file names (`Notepad_plus_Window`, `ScintillaEditView`, `FileManager`, `NppParameters`, `Notepad_plus.cpp/.h`, `class Buffer`, `DocTabView`, `PluginsManager`, `FindReplaceDlg`, `generic_string`, `WinControls`, `PowerEditor`, and others) returned **zero hits** for actual implementation code. The only matches were four comments:
- `src/main.cpp:1687` and `:3043` — describe wxNotepad++'s own original code in relation to how N++'s equivalent behaves, for context only.
- `src/main.cpp:429` — at audit time, stated the Function List regex rules were "distilled from N++'s XML and simplified to portable std::regex". **Since remediated:** the rule set has been rewritten from scratch, derived per-language from each language's own published definition grammar (with the derivation documented inline per rule), gaining capabilities the old set never had (C++ operator overloads/ctors/dtors/qualified names, JS/TS class methods and template-literal masking, package-private Java methods, C# expression-bodied members, Python PEP 695 generics) plus Go/Rust/Lua support. No shipped rule derives from N++'s functionList XML anymore. An independent adversarial code review of the rewrite (separate from this license/provenance audit) found 7 real correctness regressions/false-positives in the first draft (an Allman-brace class miss, phantom symbols from qualified-call-with-lambda / bare-callback / `new Type() {` idioms, missing Go 1.18+ generics, and a Rust `impl Trait<Args> for Type` mis-attribution) plus an unguarded `std::regex_error` on pathologically large comments; all were fixed and re-verified with an expanded 15-case regression self-test (temporarily hijacking a debug menu item, reverted before commit) covering every regression found.
- `src/npp_menu.h:4` — documents that the menu structure reproduces Notepad++'s command hierarchy (labels, mnemonics, `IDM_*` command IDs) — menu *data*, not C++ implementation; the actual menu-building code is an original, data-driven `wxMenuBar` builder. **Since remediated (documentation):** the header now spells out the interoperability rationale — the ids are plugin-ABI facts, and the hierarchy is the app's method of operation (the category *Lotus v. Borland* held outside copyright for menu trees specifically) — with a matching "Compatibility surfaces" section added to `LICENSING.md`.

A broader second pass for other distinctive N++ signatures (`class NppParameters`, `struct NppGUI`, `DocTabView`, `verticalFileSwitcher`, `functionListPanel`, `autoCompletion.cpp`, etc.) and for the literal string "PowerEditor" across the whole repo also returned no implementation hits — only the same two comments above, plus one mention in `docs/WXWIDGETS_MIGRATION_PLAN.md` describing the *upstream* Notepad++ project's real architecture as migration-planning background (no `PowerEditor/` directory exists anywhere in this repo).

**Repo-wide naming sweep:** A search for stray N++-named files/directories (`*notepad_plus*`, `*ScintillaEditView*`, `*NppParameters*`, `*winMainRes*`, any `PowerEditor/` directory, any `.rc` files) found only `include/npp-compat/Notepad_plus_msgs.h` (already covered above, intentionally named for the ABI surface it represents) and two unrelated `.rc` files: this project's own `resources/app.rc` and the legitimately vendored `third_party/lexilla/src/LexillaVersion.rc`.

**Verdict:** Clean. Nothing improper remains in the current tree. The one location that ever held verbatim GPL source has already been excised and replaced with a verified clean-room layer.

---

## 2. Non-Code Asset Provenance (icons, themes, stylers.model.xml, app icon)

| Asset | Location | Source / License | Attribution present? |
|---|---|---|---|
| Default toolbar icon set | `resources/icons/` (37 SVGs) | MIT — derivative of Tabler Icons (c) Pawel Kuna + Open Color (c) Heeyeun Jeong | **Partial.** Documented in root `LICENSING.md` and `NOTICE`, but **no in-directory `CREDITS.md`** exists (unlike its two colored siblings below). Individual SVG files carry no embedded license header, consistent with Tabler's own upstream convention. |
| Solar-style icon set | `resources/icons-solar/` | CC BY 4.0 — Solar Icons (Bold Duotone), (c) 480 Design, recolored to Open Color green-8/green-3 | Yes — detailed `CREDITS.md` in the directory, names source, license, link, and documents the recoloring/modifications. Matches `LICENSING.md`. |
| IconPark-style icon set | `resources/icons-iconpark/` | Apache-2.0 — IconPark, (c) ByteDance, recolored to Open Color teal-7/lime-5 | Yes — detailed `CREDITS.md` in the directory, notes the source repo is archived but the license remains in effect, documents modifications. Matches `LICENSING.md`. |
| `resources/stylers.model.xml` | repo root of `resources/` | Apache-2.0, regenerated — NOT a verbatim N++ styler; per-lexer style *structure* taken from the MIT-licensed Monokai N++ styler (c) Fabio Zendhi Nagao; palette is GitHub Primer Light (MIT) | Yes — embedded XML header states this explicitly. (Note: a nearby `src/main.cpp` comment referring to "Notepad++'s real theme XML" reads as slightly misleading in isolation, but the file's own header and root docs clarify it is regenerated, not copied.) |
| `resources/themes/*.xml` (27 files) | `resources/themes/` | **Mixed** — ~14 files are genuine kept upstream permissive themes with original author headers intact (MIT-style: Fabio Zendhi Nagao, Oren Farhi, Paul Neubauer, Renato Silva, and one unattributed-author-but-licensed file); ~13 files are wxNotepad++-regenerated Apache-2.0 replacements naming their source palette's license (GitHub Primer, Atom One Dark/Light, Nord, Dracula, VS Code — all MIT — plus Zenburn/Obsidian as "canonical palettes, colour values are facts") | Yes — every one of the 27 files was individually opened; every file carries either the original author's license header or the project's own regenerated Apache-2.0 header. None is a silently-copied N++ asset. |
| `resources/wxNotepad++.svg` (app icon) | `resources/` | Original work — a green rounded-square plate with a white "N" monogram; not adapted from N++'s actual notepad-with-bent-corner logo | Yes — both `LICENSING.md` and `NOTICE` self-disclose that the icon's styling deliberately "echoes" upstream N++ branding while being original artwork. Git history confirms it was introduced during the de-N++-ification restructuring, not carried over from an N++ tree. |
| `resources/wxNotepad++.ico` | `resources/` | Same asset family as the `.svg` above (binary rasterization) | Implicit — no separate license note, but `resources/app.rc`'s comment confirms it's the Win32 icon-resource rasterization of the same original SVG. |

**Overall:** `LICENSING.md` and `NOTICE` are unusually thorough for a project of this kind — every asset class has some disclosure. The one internal-consistency gap is that `resources/icons/` relies entirely on root-level docs rather than having its own `CREDITS.md`, unlike its two colored siblings. This is a documentation gap, not a false or missing claim (see Section 5).

---

## 3. Dependency & License Inventory

| Dependency | Version | License | How it's used |
|---|---|---|---|
| wxWidgets | 3.3.1 | wxWindows Library Licence 3.1 (LGPLv2+ with a static/binary-linking exception; permits closed-source use) | Fetched via `FetchContent` from the official GitHub release tarball; built from source with `wxBUILD_SHARED OFF` — **statically linked** into `wxnpp.exe`. |
| Scintilla | vendored (via `third_party/scintilla/`, headers used directly; also bundled inside wxWidgets' `wxStyledTextCtrl`) | HPND (Historical Permission Notice and Disclaimer) — MIT/BSD-style permissive, Neil Hodgson | Source vendored in-repo; headers (`Scintilla.h`, `ILexer.h`) used directly by Lexilla and wxnpp; compiled in via wx's bundled copy — effectively **statically bundled**. |
| Lexilla | vendored (`third_party/lexilla/`) | HPND — identical license text to Scintilla, Neil Hodgson | Built as a static library `lexilla` from vendored sources; **statically linked** into `wxnpp`. |
| wxBorderlessFrame (wxbf) | vendored (`third_party/wxbf/`) | wxWindows Library Licence 3.1 (LGPLv2+ with static-linking exception) | Vendored source, built as static library `wxbf`; **statically linked** into `wxnpp` on Windows and Linux only (no macOS backend). |
| GTK3 (gtk+-3.0) | system-provided | LGPL-2.1-or-later | Linux-only; located via `pkg_check_modules`, linked against the **system's dynamically-linked** shared library (`.so`) — not vendored or statically bundled. CI installs the `-dev` headers package. This is the LGPL-compliant dynamic-linking pattern. |
| `include/npp-compat/` (clean-room ABI headers) | in-repo, original | Apache-2.0 | Original project code, not a third-party dependency, but a distinct license zone inside an otherwise GPLv3 project. See Section 1. |
| `packages/npp-bridge/` | in-repo, original | GPL-3.0-or-later (by the project's own declared choice, since it's an ABI-reproducing bridge) | Original glue code; links only `comctl32` (Windows system library). No new external dependency. |
| `packages/test_plugin/` | in-repo, original | Same as project default (GPLv3) | Test harness/double; includes only in-repo headers (`third_party/scintilla/include`, `include/npp-compat`). No new external dependency. |
| Windows SDK import libraries (`comctl32`, `imm32`, `msimg32`, `ole32`, `oleaut32`, `dwmapi`, `uxtheme`) | OS-provided | Proprietary Microsoft (part of Windows OS) | Dynamically linked at build/runtime; never redistributed by the installer. |
| NSIS / makensis (replaced Inno Setup post-audit) | 3.11 used locally (portable download, nothing installed system-wide); CI installs via Chocolatey (`choco install nsis`) since `makensis` is confirmed **not** preinstalled on the `windows-latest` runner (an initial CI run failed with `'makensis' is not recognized` before this install step existed) | zlib/libpng (open source) | **Build-tool-only, with the standard NSIS nuance:** compiles `installer/windows/wxnpp.nsi` into the Setup.exe, whose installer/uninstaller *stub* is NSIS-generated code — the zlib license permits this without conditions. The installed payload itself contains only this project's own build outputs. |
| linuxdeploy | downloaded at build time | MIT | **Mostly build-tool, with a nuance:** its generated `AppRun` launcher/glue (and any auto-bundled `.so` files it deems missing) become part of the shipped AppImage's AppDir/contents — unlike Inno Setup, its output glue is not purely external. No linuxdeploy source is compiled into `wxnpp` itself. |
| dpkg-deb | system tool (Linux) | GPL-2 (dpkg's own license) | **Build-tool-only.** Serializes an already-assembled directory tree (project's own build output + `.desktop` + icon) into `.deb` format; contributes no files of its own to the package payload. |
| Homebrew librsvg / rsvg-convert | installed at build time (macOS) | LGPL-2.1+ | **Build-tool-only.** Rasterizes the project's own SVG icon into an intermediate PNG; the library itself is never bundled into the `.app`/`.dmg`. |
| iconutil | macOS system tool | Apple-proprietary | **Build-tool-only.** Packs PNGs (derived from the project's own icon) into `.icns`; contributes no code to the shipped bundle. |
| sips | macOS system tool | Apple-proprietary | **Build-tool-only.** Resizes the project's own icon PNGs; not shipped. |
| curl | system tool | curl/libcurl license (MIT-style) | **Build-tool-only.** Used solely to download linuxdeploy; not shipped. |
| actions/checkout@v4 | GitHub Action | MIT | CI-only, never shipped. |
| actions/cache@v4 | GitHub Action | MIT | CI-only, never shipped (caches `build/_deps`). |
| ilammy/msvc-dev-cmd@v1 | GitHub Action (third-party) | MIT | CI-only, never shipped. |
| actions/upload-artifact@v4 | GitHub Action | MIT | CI-only, never shipped. |
| actions/download-artifact@v4 | GitHub Action | MIT | CI-only, never shipped. |
| GitHub CLI (`gh`) | preinstalled on runner | Apache-2.0 | CI/release-only, never shipped. |

**Linking-model summary:** wxWidgets, Scintilla, Lexilla, and wxBorderlessFrame are all statically linked. GTK3 is the sole dynamically-linked open-source dependency (Linux only, LGPL-compliant pattern). Windows system libraries are dynamically linked OS components. Everything else in this table is either build-tooling that never ships, or CI-only infrastructure.

---

## 4. Reference-Project Influence (NotepadNext / notepadqq / notepad-- / nextpad-macos)

> **Since remediated:** all four in-source mentions described below have been removed — the comments now
> describe each design decision on its own terms, and the Function List section they sat next to was
> rewritten wholesale (see Section 1). As of the remediation, no reference-project name appears anywhere
> in `src/`, `include/`, or `packages/`. The findings below are preserved as the audit-time record.

At audit time, all four project names appeared **only** in `src/main.cpp` (four locations total) — none appeared in `include/`, `packages/`, or any file under `docs/`.

- **`src/main.cpp:428-433` (Function List):** Credits NotepadNext and notepadqq only to note that *neither Qt-based project has this feature at all* — cited as an absence-of-prior-art contrast, not a source. The actual regex rules are distilled from Notepad++'s own XML format into original `std::regex`-based structures.
- **`src/main.cpp:2628-2629` (session XML):** Credits NotepadNext for the *idea* of which fields to persist (caret position, first-visible line, bookmarks). The actual serialization uses `wxXmlDocument`/`wxXmlNode` matching Notepad++'s own native `<NotepadPlus><Session>` schema — unrelated to NotepadNext's Qt-based format.
- **`src/main.cpp:2694-2696` (auto-completion):** The most explicit "adapted from" language in the codebase — word completion "adapted from NotepadNext," keyword completion "adapted from notepad--." On inspection, the implementation is a short, generic prefix-scan over a `std::string` into a `std::set<std::string>` — a well-known, essentially unavoidable algorithm shape for this feature, not a line-by-line translation of Qt or C++ source from either project.
- **`src/main.cpp:6002-6003` (macro recording):** Credits NotepadNext for a UX/behavioral decision (coalescing consecutive keystrokes into one macro step). The implementation is Scintilla-message-driven plumbing (`SCI_REPLACESEL`/`SCI_DELETEBACK`) dictated by Scintilla itself, which both projects wrap regardless of GUI framework.

No file in `docs/` (`CROSS_PLATFORM_PLAN.md`, `FUTURE_PLANS.md`, `PLUGIN_API_PLAN.md`, `WXWIDGETS_MIGRATION_PLAN.md`) mentions any of the four reference projects or a "frog DNA" research policy — that framing exists only in the project owner's private local Claude Code memory, not in any committed project document.

**Assessment:** Inspiration, not copying. Every instance is a short, credited design/algorithm note followed by a small, independently written, idiomatic implementation appropriate to wxWidgets/Scintilla/C++ — none maps structurally onto a Qt (NotepadNext, notepadqq) or Cocoa (nextpad-macos) idiom. Even in a hypothetical worst case, all four referenced projects are themselves open-source (GPL or similarly licensed), so this would not present a legal problem for a GPLv3 project — at most a documentation nicety.

---

## 5. Findings Requiring Attention

Ranked by severity; **nothing here rises to a confirmed legal problem** — the most notable item was a documentation-consistency nit. **All three findings are now resolved** (see the remediation addendum at the top).

1. **[RESOLVED] `resources/icons/` had no in-directory `CREDITS.md`.** Its two colored sibling sets (`icons-solar/`, `icons-iconpark/`) each had a detailed `CREDITS.md` documenting source, license, and modifications, while the default set's MIT/Tabler/Open-Color attribution existed only in root `LICENSING.md` and `NOTICE` — no missing or false claim, but an inconsistency worth fixing since the icons are the asset class most likely to be redistributed or forked independently. *Fixed: `resources/icons/CREDITS.md` added, matching its siblings' format and sourced from the existing root-doc facts.*

2. **[RESOLVED] `src/main.cpp`'s theme-parsing comment** ("Parse Notepad++'s real theme XML ... so the editor uses Notepad++'s exact colours") read, in isolation, as if `stylers.model.xml` contained literal copied N++ data — the file's own embedded header and both root docs correctly said otherwise. *Fixed: reworded to state the app parses N++'s theme XML **format** while the shipped-by-default files are wxNotepad++'s own regenerated, permissively-licensed replacements.*

3. **[RESOLVED — documented, no code change possible or needed] The `LangType` enum in `include/npp-compat/`** has the least "daylight" from the original N++ header of anything in the clean-room replacement, since enum member order must match exactly for ABI compatibility. Explicitly defensible under the project's own stated clean-room rule (facts/IDs may be reproduced; expression may not) — there is no practical way to give a fixed-value enum "creative" alternate content. *The interoperability rationale for both compatibility surfaces (ABI + menu structure) is now spelled out in `LICENSING.md`'s "Compatibility surfaces" section.*

**If you were hoping for a smoking gun:** none of the four research passes found one. No leftover N++ implementation code, no undisclosed asset, no unidentified or mis-licensed dependency, and no evidence of copied (versus credited-and-reimplemented) code from the four reference projects.

---

## 6. What Was NOT Checked

This audit's scope was defined by four research agents grepping, diffing, and reading files in the working tree, plus targeted git history spelunking. The following were **not** checked and should not be assumed clear:

- **No binary/build-artifact scan.** `build/`, compiled `.o`/`.obj`/`.dll`/`.exe` outputs, and any transient `_deps/` fetched-source caches were not inspected for embedded strings, debug symbols, or accidentally-committed binaries that might reveal different provenance than the source tree suggests.
- **No full-history git blame/archaeology beyond the two commits identified** (`94c927e`, `1d8f0e1`). Other commits in the repo's full history were not systematically reviewed for other since-removed leftover files; the audit relied on the current tree plus the two commits directly relevant to the third_party/notepad-plus-plus removal.
- **No license-compatibility legal opinion.** This report identifies and describes licenses; it does not constitute a legal determination that the overall combination (GPLv3 core + Apache-2.0 compat headers + MIT/HPND/LGPL dependencies + CC BY 4.0/Apache-2.0 icon sets) is fully compliant in every jurisdiction or distribution scenario. A qualified OSS-licensing attorney should review before any relicensing event (the project's own `LICENSING.md` already flags a pending "clean-room audit" gate for this reason).
- **No verification of the `.ico` binary's actual pixel content** — its provenance was inferred from `resources/app.rc`'s comment and its relationship to the `.svg`, not from decoding the ICO file itself.
- **No check of commit authorship/CLA/copyright-assignment status** for contributors, which is a separate question from source provenance.
- **No check of transitive dependencies inside vendored third_party code** (e.g., whether Scintilla/Lexilla/wxWidgets themselves bundle any further third-party code with different licenses) beyond reading their top-level `License.txt`/`LICENSE.txt` files.
- **No dynamic/runtime analysis** (e.g., checking whether any optional runtime plugin-download mechanism could pull in additional code not present in the repo).
- **The "frog DNA" reference-project policy check was limited to name-string greps** (`NotepadNext`, `notepadqq`, `notepad--`, `nextpad-macos`) across `src/`, `include/`, `packages/`, `docs/`. It did not include a structural/algorithmic comparison against those four projects' actual source code to rule out unattributed similarity beyond the four explicitly-commented instances found.
- **CI/installer scripts on platforms not exercised in this session** (e.g., actual execution of `installer/macos/build-dmg.sh` or `installer/linux/build-appimage.sh`) were read statically, not run, so runtime-only side effects (e.g., what linuxdeploy actually bundles for a given build environment) were inferred from script logic, not observed directly.
