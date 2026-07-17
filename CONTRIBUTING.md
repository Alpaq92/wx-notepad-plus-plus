# Contributing to wxNote

Thanks for your interest! wxNote is an experimental, fast-moving project — an
independent, cross-platform reimplementation of the Notepad++ experience on
wxWidgets. Contributions of all kinds are welcome: bug reports, code,
translations, themes, and documentation.

## Reporting bugs & requesting features

Use the issue templates on GitHub — there's one for
[bugs](.github/ISSUE_TEMPLATE/1-bug.yml) and one for
[feature requests](.github/ISSUE_TEMPLATE/2-feature-request.yml). For bugs,
the version + platform (Windows / Linux / macOS) matter a lot, since the
three builds share one codebase but differ in chrome and native-shim code.

One routing note: if a bug is in a third-party **Notepad++-ABI plugin's own
logic** (loaded on Windows through the optional npp-bridge), report it to
that plugin's author — report it here only if wxNote, npp-bridge, or a
bundled Nib plugin is misbehaving.

## Before writing code

For anything larger than a small fix, please open an issue first to discuss
the approach. The project has strong opinions in a few areas (licensing
boundaries, cross-platform rules, the plugin-API design) and a short
conversation up front beats reworking a finished PR.

Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for how the code is put
together, and [`LICENSING.md`](LICENSING.md) for the license structure —
both are short and will save you time.

## Building from source

Requires CMake ≥ 3.20, a C and C++17 compiler, and Ninja. The first
configure fetches and builds wxWidgets 3.3.1 from source, and also fetches
Lua 5.4.7 + LPeg 1.1.0 (built as the `lua_lpeg` static library for the
Scintillua language engine) and downloads Scintillua's `lexer.lua` — so the
first configure needs network access. Expect it to take a while; afterwards
builds are incremental.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target wxnote
# -> build/bin/wxnote   (wxnote.exe on Windows)
```

- **Windows** — the MSVC compiler from Visual Studio Build Tools (the build
  is CMake + Ninja; MSBuild and solution files are not used).
- **Linux** — `sudo apt-get install build-essential cmake ninja-build
  pkg-config libgtk-3-dev`
- **macOS** — the Xcode command-line tools.

If the wxWidgets compile exhausts memory (it can on small machines and does
on CI), cap parallelism: `cmake --build build --target wxnote --parallel 2`.

## Pull requests

- Branch from `master`, push your branch, open a PR. Direct pushes to
  `master` are not accepted.
- CI builds every PR on all three platforms (Windows, Linux, macOS — seven
  build legs in total, including x64 and ARM64 for Windows/Linux plus a
  Linux RISC-V cross-compile) — all required checks must be green before
  merge (currently the four x64/macOS legs; the ARM and RISC-V legs build
  on every PR but aren't merge-blocking yet).
- Keep PRs focused: one fix or feature per PR. Mechanical churn (renames,
  reformatting) separate from behavior changes.
- Describe what you changed and *how you verified it*. The app itself has no
  unit-test suite, so for UI/editor work verification means building and
  exercising the affected feature in the running app on at least your own
  platform. Two components have automated self-tests you should run/extend
  when touching them. The `packages/udl-compat` translator has unit +
  roundtrip tests —
  `cmake -S packages/udl-compat -B build-udltest && cmake --build build-udltest`
  then `ctest --test-dir build-udltest --output-on-failure` (add
  `-DUDL_COMPAT_SCINTILLUA_TEST=ON` to also run the end-to-end
  Lua/LPeg/Scintillua lexing test). The integrated Terminal panel has
  `terminal_selftest` (`tests/terminal_selftest.cpp`), a
  `wxUIActionSimulator`-driven behavioural test that builds a real
  TerminalPanel and drives it through its public API —
  `cmake --build build --target terminal_selftest && build/bin/terminal_selftest`
  (it needs a display, so it is a developer/CI-with-display tool, not part
  of a headless test run). Cross-platform behavior you can't test locally is
  what the CI matrix and review are for; say what you couldn't test.

## Code guidelines

- **Match the surrounding code.** Most of the app lives in `src/main.cpp`;
  follow its existing naming, comment density, and idioms. Comments explain
  *constraints and non-obvious decisions*, not what the next line does.
- **Cross-platform first.** Win32/AppKit/GTK-specific code goes behind
  `#ifdef __WXMSW__` / the CMake-gated platform shims (`src/gtk_native.cpp`,
  `src/macos_native.mm`) — never in shared paths. Note that GCC and Clang
  are stricter than MSVC (e.g. mixed `const char*`/`wxString` ternaries
  don't compile there) — a Windows-only build passing doesn't mean CI will.
- **Respect the license boundary.** The core must stay free of
  Notepad++-ABI code: it includes nothing from `include/npp-compat/`; its
  command ids live in the core's own `src/command_ids.h`, whose numeric
  values are frozen (static_asserts) for bridge compatibility — never
  renumber them. If a Notepad++ plugin needs a capability, add a *generic*
  interface to the Nib API and adapt it inside `packages/npp-bridge/` —
  never teach the core N++ semantics.
- **User-visible strings must be translatable.** Wrap them in `_()`, add the
  new msgid to `resources/locale/wxn.pot` and to all eight language catalogs
  (`{pl,de,fr,es,ru,ja,zh,ko}/LC_MESSAGES/wxn.po`), then recompile the `.mo`
  files with `resources/locale/po2mo.py` (the region-qualified directories
  such as `pl_PL/` and `zh_CN/` ship byte-identical copies of their
  short-code sibling's `.mo`). Untranslated
  strings fall back to English, so a missing translation degrades
  gracefully — but please don't ship new UI strings without at least the
  `.pot` entry.
- **Files the app writes at runtime** (settings, recovery, session) go through
  `wxConfig` or the user-data directory — never next to the executable,
  which is read-only on installed builds.

## Translations

Improving an existing catalog is the easiest first contribution: edit the
language's `resources/locale/<lang>/LC_MESSAGES/wxn.po`, recompile with
`python resources/locale/po2mo.py <in>.po <out>.mo`, and refresh the
region-qualified copy if one exists. To add a new language, copy
`resources/locale/wxn.pot` to `<lang>/LC_MESSAGES/wxn.po`, translate, and
compile the same way — wxWidgets picks it up by directory name.

## Themes

wxNote reads Notepad++'s theme-XML format, so an existing N++ theme file
generally works as-is. To ship a theme *with* wxNote it must be permissively
licensed (MIT/similar or your own original work) with license and author
stated in the file header — see the existing files in `resources/themes/`
for both header styles (kept third-party and first-party regenerated).

## Licensing of contributions

wxNote's core is **Apache-2.0**; the optional `packages/npp-bridge/`,
`packages/test_plugin/`, and `packages/udl-compat/` modules are
**GPL-3.0-or-later** (see [`LICENSING.md`](LICENSING.md) for why). Contributions are accepted under
the license of the component they touch — inbound = outbound. By submitting
a PR you agree your contribution is licensed accordingly; no CLA, no
copyright assignment.

Two things we cannot accept, regardless of quality:

- Code copied from Notepad++ (or any GPL project) into the Apache-2.0 core —
  the core's clean-room status is load-bearing for the whole license
  structure.
- Assets (icons, themes, fonts) without a clear permissive license and
  attribution.
