# npp-shortcuts-compat — the optional Notepad++ `shortcuts.xml` importer

`npp-shortcuts-compat` lets **wxNote import a Notepad++ keybinding file** (`shortcuts.xml`) and
re-add those shortcuts as a named, switchable **"Notepad++ (imported)"** scheme. It reads a real
N++ `shortcuts.xml`, translates each keystroke into wxNote's portable accelerator spelling, and
feeds the bindings into the core through the generic **`nib.keymap/1`** capability — so a user's
existing Notepad++ keys keep working without wxNote's core ever having to understand the
`shortcuts.xml` format at all.

## Why it's a separate, GPL module

This module reproduces **Notepad++'s `shortcuts.xml` file format** — its five sections
(`InternalCommands`, `Macros`, `UserDefinedCommands`, `PluginCommands`, `ScintillaKeys`), the
Windows-VK decimal key encoding, and the attribute schema — in order to interoperate with it.
Reproducing that format is exactly the kind of interoperability work wxNote deliberately confines
to optional, separately-licensed modules, so like [`udl-compat`](../udl-compat/README.md) and
[`npp-bridge`](../npp-bridge/README.md) this one is **GPL-3.0-or-later**. The wxNote **core depends
on none of it**: the core's `nib.keymap/1` surface is a generic *"here is a key and what it should
do"* — nothing `shortcuts.xml`-shaped — and this plugin is loaded only if present. Delete it and you
lose only Notepad++ shortcut import; the core's own scheme system is untouched. Keeping the format
reproduction here is what lets the core stay Apache-2.0 (see [`LICENSING.md`](../../LICENSING.md)).

## What it does

1. **Parses** a Notepad++ `shortcuts.xml` into a data model (`npp_shortcuts_parse.{h,cpp}`), reading
   all five sections. The parser is **wx-free** and **XXE-hardened**: it never expands a DTD or an
   external/custom entity (only the five predefined XML entities and ASCII numeric character
   references are decoded), strips comments and `DOCTYPE` declarations before scanning, and clamps
   every untrusted numeric attribute to a sane range.
2. **Translates** each keystroke into wxNote's portable accelerator string (`npp_shortcuts_accel.{h,cpp}`):
   a ~60-entry table maps a Windows virtual-key code to the exact token
   `wxAcceleratorEntry::FromString` accepts (taken from wx's own `wxKeyNames` table), then assembles
   `Ctrl+Shift+<token>`. The host parses and re-canonicalises each string with `ToRawString`, and
   maps `Ctrl`→`Cmd` itself on macOS — so no per-platform spelling is emitted here.
3. **Registers** the bindings with the host as a named scheme through `nib.keymap/1`
   (`npp_shortcuts_compat_plugin.cpp`, the only `nib.h` consumer):
   - `InternalCommands` → `bind_id` (Notepad++'s `IDM_*` id is wxNote's frozen `kCmd*` id — a
     translation-free mapping),
   - `ScintillaKeys` → `bind_editor` (Scintilla `SCI_*` → the editor keymap tier; `NextKey` = an
     additional binding),
   - `PluginCommands` → `bind_name` (best-effort `npp.<module>.<internalID>` symbolic name).

The three tiers map onto `nib.keymap`'s three binding namespaces exactly. A committed scheme is
written to the host's scheme store and **outlives the plugin** — import once and the keys survive
even if you later delete this importer.

## How to use it

The plugin registers a single command, **"Import Notepad++ shortcuts.xml…"** (well-known id
`host.shortcuts.import`), which appears in the **Plugins** menu; the core's **Run ▸ "Validate
shortcuts.xml"** entry forwards to the same command. Running it:

- looks for a `shortcuts.xml` in wxNote's user-data directory (the folder that holds
  `shortcuts.json`); on Windows an installed Notepad++'s `%APPDATA%\Notepad++\shortcuts.xml` is also
  detected automatically,
- imports the bindings as the **"Notepad++ (imported)"** scheme (it does **not** switch to it — pick
  it in the Shortcut Mapper's scheme picker to apply it), and
- shows a validation report: how many bindings were **imported**, how many keys were **unmapped**
  (no portable equivalent), how many targets were **unknown** to this build, and the
  `UserDefinedCommands` / `Macros` that are **shown for review only, never run**.

## Security stance

- **`UserDefinedCommands` carry shell command lines** (e.g. `firefox $(FULL_CURRENT_PATH)`). This
  plugin treats them as **pure data**: they are parsed and surfaced in the report *for review only
  and are NEVER executed*. There is no `ShellExecute` / `system` / `CreateProcess` / `wxExecute`
  anywhere in the package. Macros are likewise parsed and counted but never replayed. This
  structurally sidesteps the "planted `<Command>` runs on import" class of problem — wxNote imports
  bindings into its own store and the plugin has no execution path at all.
- **Import is read-only** on the foreign file; the plugin never writes `shortcuts.xml` back.
- The parser is **XXE-hardened** (no DTD/entity expansion; bounded reads; untrusted attributes
  clamped).

## Building and testing

Standalone (no wxWidgets / host needed):

```sh
cmake -S packages/npp-shortcuts-compat -B build-nppsc && cmake --build build-nppsc
ctest --test-dir build-nppsc --output-on-failure          # -> npp_shortcuts_selftest ... Passed

# convert a real shortcuts.xml and see the derived accelerators + report:
./build-nppsc/npp2accel path/to/shortcuts.xml
```

The self-test (`selftest.cpp`) covers parsing all five sections, the VK→token table, the
accelerator builder, the comment/`DOCTYPE`/unknown-entity handling (XXE), the "command line captured
as data, never executed" security case, and the report format. It is wired to `ctest`.

As part of the wxNote build the top-level `add_subdirectory(packages/npp-shortcuts-compat)` builds
the runtime plugin as `bin/nib/npp_shortcuts_compat.dll` (`.so` / `.dylib`).

## Files

| File | Role |
|---|---|
| `npp_shortcuts_parse.{h,cpp}` | `shortcuts.xml` → data model + the report formatter (wx-free, XXE-hardened) |
| `npp_shortcuts_accel.{h,cpp}` | Windows VK → wx accelerator token + accel-string builder (wx-free) |
| `npp_shortcuts_compat_plugin.cpp` | the Nib plugin entry — the **only** `nib.h` consumer |
| `npp2accel.cpp` | standalone converter CLI |
| `selftest.cpp` | parse + VK-table + security tests, wired to `ctest` |

This package is deliberately isolated as the one place that both knows the Notepad++ `shortcuts.xml`
format and speaks wxNote's keymap surface, and is scoped to eventually move to its own repository.
