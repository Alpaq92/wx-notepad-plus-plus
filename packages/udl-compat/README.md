# udl-compat — the optional Notepad++ UDL → Scintillua compatibility layer

`udl-compat` lets **wxNote keep loading legacy Notepad++ User-Defined Language files**
(`userDefineLang.xml`) after the core switched to **[Scintillua](https://github.com/orbitalquark/scintillua)**
(Lua LPeg lexers) as its native, cross-platform language-definition engine. It reads an
N++ UDL definition and translates it into a Scintillua lexer, so a user's existing UDL
keeps highlighting without wxNote's core having to understand the N++ format at all.

## Why it's a separate, GPL module

This module reproduces **Notepad++'s UDL file format** — its keyword-list names, the
packed `Comments`/`Delimiters` encodings, and the fixed style slots — in order to
interoperate with it. Reproducing that format is exactly the kind of interoperability
work wxNote deliberately confines to optional, separately-licensed modules, so like
[`npp-bridge`](../npp-bridge/README.md) this one is **GPL-3.0-or-later**. The wxNote
**core depends on none of it**: the core ships Scintillua and knows nothing about UDL;
this plugin is loaded only if present, and deleting it just means legacy `.xml` UDL files
no longer load. Keeping the UDL reproduction here is what lets the core stay Apache-2.0
(see [`LICENSING.md`](../../LICENSING.md)).

This mirrors the project's whole licensing strategy: everything original and every
permissively-licensed dependency (Scintillua and its Lua/LPeg runtime are all MIT) lives
in the core; the one deliberately Notepad++-shaped piece is isolated, optional, and
honestly labeled. It is also scoped to eventually move to its own repository, since it is
the single component that both understands the Notepad++ UDL format and produces Scintillua
lexers.

## What it does

1. **Parses** a Notepad++ `userDefineLang.xml` into the UDL data model (`udl_parse.{h,cpp}`),
   decoding the packed `Comments`/`Delimiters` encodings the same way Notepad++ stores them.
   (The core no longer contains any UDL-format code — `src/udl.h` and `src/udl_lexer.h` were removed.)
2. **Translates** each UDL into a Scintillua lexer (`translateUdlToScintillua()` in
   [`udl_scintillua.cpp`](udl_scintillua.cpp)) — a pure, dependency-free function over
   plain `std::string`/`std::vector` types, unit-tested standalone by
   [`selftest.cpp`](selftest.cpp).
3. **Registers** the resulting lexer with the host through the core's Scintillua
   language-provider capability (the `nib.langdef/1` Nib API), so the language
   appears in the Document ▸ Language menu like any built-in.

## UDL → Scintillua mapping

Only Scintillua constructs confirmed against real bundled lexers are emitted:

| Notepad++ UDL concept | Scintillua construct |
|---|---|
| `Keywords1` | `M:add_rule('keyword1', M:tag(lexer.KEYWORD, lexer.word_match(...)))` |
| `Keywords2`..`Keywords8` | same, each tagged `keyword2`..`keyword8` so themes colour the eight groups apart |
| case-insensitive keywords | `lexer.word_match('…', true)` |
| line comment (`Comments` pack) | `lexer.to_eol('//')` |
| block comment (`Comments` pack) | `lexer.range('/*', '*/')` |
| `Delimiters` set (open/close) | `lexer.range(open, close)` (string tag) |
| `Delimiters` single-line set | `lexer.range(open, '\n', true)` |
| numbers | `lexer.number` |
| operators | `lpeg.S('+-*/…')` (LPeg char set) |

Not every UDL feature has a clean Scintillua equivalent (nested delimiters, the
`Folders in code` fold keywords, per-slot font styling); those are the subject of
ongoing translation-fidelity work in this plugin.
The translator's Lua-escaping and name-sanitizing are covered by the self-test, which
also documents the exact expected output for a representative UDL.

## Status

**Complete and wired into the build.** The offline pipeline, the core Scintillua engine,
both Nib APIs, and the runtime plugin are all in place as of the 0.8.0 line.

**The translation pipeline (pure C++17, standalone-tested):**
- `udl_parse.{h,cpp}` — parses a real `userDefineLang.xml` into a `UdlDef`, decoding the
  packed `Comments`/`Delimiters` encodings the same way Notepad++ files store them (this
  parser was relocated here from the old `src/udl.h`, which no longer exists in the core).
- `udl_scintillua.{h,cpp}` — `translateUdlToScintillua()` turns a `UdlDef` into a
  Scintillua Lua lexer.
- `udl2scintillua.cpp` — a converter CLI: `userDefineLang.xml` → `.lua`.
- `selftest.cpp` — a full-pipeline test (parse → translate, plus edge cases: Lua
  escaping, entity unescaping, bare-root files, malformed input), wired to `ctest`.

```sh
cmake -S packages/udl-compat -B build-udltest && cmake --build build-udltest
ctest --test-dir build-udltest --output-on-failure          # -> udl_selftest ... Passed
# or, standalone:
cl /std:c++17 /EHsc selftest.cpp udl_scintillua.cpp udl_parse.cpp && udl_selftest.exe
```

**Proven against real Scintillua — as a committed test.** Beyond the offline unit
tests, there is an end-to-end test that fetches Lua 5.4 + LPeg + Scintillua, builds an
embedded interpreter, translates a UDL with this package, and lexes sample source with
the generated lexer through *actual* Scintillua — asserting the token tags. It is
gated behind a CMake option (off by default, since it needs network):

```sh
cmake -S packages/udl-compat -B build-udl -DUDL_COMPAT_SCINTILLUA_TEST=ON
cmake --build build-udl --config Release
ctest --test-dir build-udl -C Release --output-on-failure
#   udl_selftest ............. Passed
#   udl_scintillua_roundtrip . Passed   (tags: comment,default,keyword,number,operator,string,whitespace)
```

Getting there surfaced and fixed three real emitter bugs that only a live runtime
reveals: `lexer.property` must not be touched before `lexer.load` initializes it;
Scintillua overrides `require()` inside a loaded lexer, so `lpeg` must be used as the
provided global (never `require('lpeg')`); and Scintillua has no `oneof` — operator sets
use `lpeg.S`. The translator emits only constructs confirmed to run under real Scintillua.

**In the core (host side, done):**
- `nib.langdef/1` and `nib.paths/1` — the Nib capabilities the plugin uses to discover the
  user-data dir and register a Scintillua-backed language (`include/nib/nib.h`).
- `src/scintillua_engine.{h,cpp}` — the host's embedded Lua 5.4.7 + LPeg 1.1.0 + Scintillua
  `lexer.lua` engine (built as the `lua_lpeg` static library via CMake FetchContent), which
  `register_language` compiles the emitted Lua against and drives through a Scintilla
  container lexer. The old Notepad++-style UDL engine (`src/udl.h`, `src/udl_lexer.h`) has
  been removed from the core.

**The runtime plugin (this package, done):**
- `udl_compat_plugin.cpp` — on load it asks the host (`nib.paths`) for the user-data dir,
  scans `userDefineLangs/`, parses each `userDefineLang.xml`, translates it, and registers
  the result via `nib.langdef`. Built as `bin/nib/udl_compat.dll` (`.so`/`.dylib`) when
  compiled as part of the wxNote build (top-level `add_subdirectory(packages/udl-compat)`).

Ongoing fidelity work (constructs without a clean Scintillua equivalent — nested delimiters,
`Folders in code` fold keywords, per-slot font styling) is tracked in
[`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md). This package is deliberately
isolated as the one place that both knows the Notepad++ UDL format and emits Scintillua, and
is scoped to eventually move to its own repository.
