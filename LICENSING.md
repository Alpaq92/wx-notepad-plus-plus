# Licensing

> **Not legal advice.** This document records how the licenses of wxNotepad++'s components interact,
> as understood from the license files in this repository. For any decision you intend to act on,
> consult a lawyer (or the FSF) and read the full texts in [`LICENSE`](LICENSE),
> [`third_party/scintilla/License.txt`](third_party/scintilla/License.txt) and
> [`third_party/lexilla/License.txt`](third_party/lexilla/License.txt).

## TL;DR

**wxNotepad++ is licensed under the GNU GPL v3 (or later)** — the same as upstream Notepad++. The
reimplemented editor compiles against Notepad++'s GPL plugin-ABI headers, so it is a derivative work
of Notepad++ and inherits its copyleft.

Scintilla and Lexilla — which supply the actual editing/highlighting engine — are **permissively
licensed** and impose **no** constraint. The GPL obligation comes **entirely from Notepad++**.

## Components

| Component | License | In this repo? |
|---|---|---|
| `src/` (the wxNotepad++ editor) | **GPL v3 or later** (derivative work) | yes |
| Notepad++ ABI headers — `third_party/notepad-plus-plus/` | **GPL v3 or later** | yes (4 headers, see below) |
| Scintilla headers — `third_party/scintilla/` | Scintilla License (HPND, permissive) | yes (headers only) |
| Lexilla — `third_party/lexilla/` | Scintilla License (HPND, permissive) | yes |
| wxWidgets (incl. its bundled Scintilla/Lexilla) | wxWindows Licence (LGPL + static-linking exception) | no — fetched at build time |

The original Notepad++ application source is **no longer in this repo** — only the four ABI/id headers
needed for plugin and command-id compatibility are kept (with their GPL notices intact).

## Why wxNotepad++ is GPL v3

Copyleft is triggered by **inclusion, not by proportion** — if any GPL code is part of the work, the
whole work is GPL. `src/main.cpp` compiles against these GPL v3+ Notepad++ headers, making the editor
a derivative work of Notepad++:

- `third_party/notepad-plus-plus/PluginInterface.h` — plugin ABI
- `third_party/notepad-plus-plus/Notepad_plus_msgs.h` — `NPPM_*` message ids
- `third_party/notepad-plus-plus/Docking.h` — docking struct (`tTbData`)
- `third_party/notepad-plus-plus/menuCmdID.h` — `IDM_*` command ids

Only the Notepad++ copyright holders (Don Ho and contributors) can relicense Notepad++'s code; a
downstream derivative cannot.

## What the reuse actually is

The editor takes very little Notepad++ *code* — but "very little" is still enough to trigger GPL:

- **Notepad++ logic reused: ~0 lines.** The editor is reimplemented in ~3,400 lines of original
  wxWidgets C++ (`src/`), not by linking Notepad++'s backend.
- **Notepad++ headers reused: ~2,099 lines** — the four interface headers above (`#define`s + struct
  declarations: the plugin ABI + command ids), reused so plugins and command ids stay identical to
  Notepad++. They carry the GPL v3+ notice, so including them imposes GPL.
- **Editor engine: Scintilla + Lexilla (permissive)** — used via wxWidgets' `wxStyledTextCtrl`, plus
  our Lexilla for `CreateLexer`.

## Scintilla / Lexilla license (permissive)

From `third_party/scintilla/License.txt` and `third_party/lexilla/License.txt` (Neil Hodgson, the
"Historical Permission Notice and Disclaimer" style):

> Permission to use, copy, modify, and distribute this software and its documentation for any
> purpose and without fee is hereby granted, provided that the above copyright notice appear in
> all copies and that both that copyright notice and this permission notice appear in supporting
> documentation.

GPL-compatible and copyleft-free; it requires only that the copyright notice be preserved.

## Could the license be changed to something permissive (MIT/BSD/…)?

Not while the editor compiles against Notepad++'s GPL headers. It would require fully **detaching
from Notepad++'s GPL code**:

1. **Clean-room replacements** for the four headers — your own command ids, and the plugin ABI
   re-declared from public documentation without copying Notepad++'s header text.
2. Dropping the **Notepad++ name and the green "N" logo** — trademark/identity, separate from the
   code license (the logo is not even under the GPL).
3. ⚠️ Re-declaring an ABI for binary compatibility is **legally unsettled** (cf. *Google v. Oracle*,
   which leaned toward fair use for interoperability, but the question is jurisdiction-dependent).
   This needs real legal review.

After that you would have a *new, independent* editor (original code + permissive Scintilla/Lexilla)
that merely speaks the Notepad++ plugin ABI — and *that* you could license permissively. It is a
deliberate project decision, not a flag flip.

## Recommendation

For an experimental project, **stay on GPL v3.** It is the honest, zero-risk path. Pursue a permissive
license only as a separate, clean-room, legally-reviewed effort.
