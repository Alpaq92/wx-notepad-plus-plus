# Licensing

> **Not legal advice.** This document records how the licenses of wxNotepad++'s components
> interact, as understood from the license files in this repository. For any decision you intend
> to act on, consult a lawyer (or the FSF) and read the full texts in [`LICENSE`](LICENSE),
> [`scintilla/License.txt`](scintilla/License.txt) and [`lexilla/License.txt`](lexilla/License.txt).

## TL;DR

**wxNotepad++ is licensed under the GNU GPL v3 (or later)** — the same as upstream Notepad++.
It is an experimental fork that (a) contains Notepad++'s GPL source and (b) compiles against
Notepad++'s GPL headers, so it is a derivative work of Notepad++ and inherits its copyleft.

Scintilla and Lexilla — which supply the actual editing/highlighting engine — are **permissively
licensed** and impose **no** constraint on this project. The GPL obligation comes **entirely from
Notepad++**, not from the editor engine.

## Components

| Component | License | In this repo? |
|---|---|---|
| Notepad++ source (`PowerEditor/`) | **GPL v3 or later** | yes |
| Notepad++ headers reused by `spike/` (ABI + ids) | **GPL v3 or later** | yes (see below) |
| `spike/` (our reimplemented editor) | **GPL v3 or later** (as a derivative work) | yes |
| Scintilla (`scintilla/`) | Scintilla License (HPND, permissive) | yes |
| Lexilla (`lexilla/`) | Scintilla License (HPND, permissive) | yes |
| wxWidgets (incl. its bundled Scintilla/Lexilla) | wxWindows Licence (LGPL + static-linking exception) | no — fetched at build time |

## Why wxNotepad++ is GPL v3

Copyleft is triggered by **inclusion, not by proportion** — if any GPL code is part of the work,
the whole work is GPL. Two independent facts bind this project:

1. **It is a fork** containing all of Notepad++'s GPL source (`PowerEditor/src`, ~155k lines).
2. **`spike/main.cpp` compiles against these GPL v3+ Notepad++ headers**, making the editor a
   derivative work:
   - `PowerEditor/src/MISC/PluginsManager/PluginInterface.h` — plugin ABI
   - `PowerEditor/src/MISC/PluginsManager/Notepad_plus_msgs.h` — `NPPM_*` message ids
   - `PowerEditor/src/WinControls/DockingWnd/Docking.h` — docking struct (`tTbData`)
   - `PowerEditor/src/menuCmdID.h` — `IDM_*` command ids

Note also that **only the Notepad++ copyright holders** (Don Ho and contributors) can relicense
Notepad++'s code; a downstream fork cannot.

## What the reuse actually is

For context (line counts measured from the tree), the fork takes very little Notepad++ *code* —
but "very little" is still enough to trigger GPL:

- **Notepad++ logic reused: ~0 lines.** The editor is reimplemented in ~3,400 lines of original
  wxWidgets C++ (`spike/`), not by linking Notepad++'s backend.
- **Notepad++ headers reused: ~2,099 lines** — the four interface headers above. These are
  `#define`s and struct declarations (ABI + command ids), reused so plugins and command ids stay
  identical to Notepad++. They carry the GPL v3+ notice, so including them imposes GPL.
- **Editor engine: Scintilla + Lexilla (~125k lines), permissive** — used via wxWidgets'
  `wxStyledTextCtrl`, plus our Lexilla for `CreateLexer`.

## Scintilla / Lexilla license (permissive)

From `scintilla/License.txt` and `lexilla/License.txt` (Neil Hodgson, "Historical Permission
Notice and Disclaimer" style):

> Permission to use, copy, modify, and distribute this software and its documentation for any
> purpose and without fee is hereby granted, provided that the above copyright notice appear in
> all copies and that both that copyright notice and this permission notice appear in supporting
> documentation.

This is GPL-compatible and copyleft-free; it requires only that the copyright notice be preserved.

## Could the license be changed to something permissive (MIT/BSD/…)?

Not while the project remains a Notepad++ fork. Doing so would require fully **detaching from
Notepad++'s GPL code**:

1. A **separate repository with no Notepad++ source** (drop `PowerEditor/` etc.).
2. **Clean-room replacements** for the four headers above — your own command ids, and the plugin
   ABI re-declared from public documentation without copying Notepad++'s header text.
3. Dropping the **Notepad++ name and the green "N" logo** — that is trademark/identity, separate
   from the code license (the logo is not even under the GPL).
4. ⚠️ Re-declaring an ABI for binary compatibility is **legally unsettled** (cf. *Google v.
   Oracle*, which leaned toward fair use for interoperability, but the question is jurisdiction-
   dependent). This needs real legal review.

After all of that you would have a *new, independent* editor (original code + permissive
Scintilla/Lexilla) that merely speaks the Notepad++ plugin ABI — no longer a Notepad++ fork — and
*that* you could license permissively. It is a deliberate project decision, not a flag flip.

## Recommendation

For an experimental fork, **stay on GPL v3.** It is the honest, zero-risk path and matches the
README. Pursue a permissive license only as a separate, clean-room, legally-reviewed effort.
