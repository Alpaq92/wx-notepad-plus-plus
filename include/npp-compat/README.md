# `include/npp-compat/` — clean-room Notepad++ ABI headers

This directory holds **clean-room, Apache-2.0 redeclarations** of the headers a compiled
Notepad++ binary plugin is built against. They exist so that real, already-compiled Notepad++
plugin DLLs can be loaded and driven by `packages/npp-bridge/` (the one GPL component in this
repo that consumes them) without this repository containing, deriving from, or linking against
any Notepad++ source code.

## What "clean-room" means here, concretely

| Header | What's reproduced | Why it has to be |
|---|---|---|
| `menuCmdID.h` | `IDM_*` numeric command ids | A plugin sends these ids in `SendMessage` calls; the numbers themselves are the interface. |
| `Notepad_plus_msgs.h` | `NPPM_*`/`NPPN_*` message ids and payload struct layouts | Same reasoning — a message id and its payload's byte layout are the wire format, not expression. |
| `PluginInterface.h` | `NppData`, `FuncItem`, `ShortcutKey` layouts; the 6 exported entry-point names/signatures | Binary interop requires the exact struct layout and exact exported symbol names — a linker resolves plugins by name, and a plugin reads these structs by raw offset. |
| `Docking.h` | `tTbData`/`DockedWidgetData` layout, `DWS_*`/`CONT_*` flag values | Same: a plugin writes into this struct and the flag values are part of the wire protocol. |
| `npp_plugin_port.h` | *(nothing from N++ — this one is ours outright)* | Portable Win32-type vocabulary so the above compile on Linux/macOS too. |

What is **not** reproduced, anywhere in this directory: Notepad++'s license header, its comments,
its code formatting/ordering choices, its internal helper functions, or any surrounding
implementation. Struct field *names* are kept because a plugin's source code references them by
name (`nppData._nppHandle`) — renaming them would silently break plugin *source* compatibility for
no compatibility gain, since the binary layout is what actually matters for loading a compiled
DLL. Numeric constants, struct layouts, and exported symbol names are the *interface itself* —
they're facts a compatible implementation must match, not creative expression, and are excluded
from copyright the same way a file format's magic number or a network protocol's field order is.

## What a reviewer should check

If you're reviewing a change to this directory (or reviewing the directory for the first time):

1. **No textual overlap with Notepad++ source.** Comments, wording, and formatting in these files
   should read as originally written for wxNotepad++ — if a comment sounds like it was lifted from
   `PluginInterface.h`'s actual header, rewrite it in your own words describing the same fact.
2. **Layout equivalence, not source equivalence.** The goal is that `sizeof`/`offsetof`/alignment
   for `NppData`, `FuncItem`, `ShortcutKey`, `tTbData`, and `CommunicationInfo` match what a real
   compiled N++ plugin DLL expects — see `abi_layout_asserts.h`, included from
   `packages/npp-bridge/npp_bridge.cpp`, which asserts each field's offset follows from the field
   before it (catching *accidental* drift at compile time). That header cannot verify layout
   against the *real* upstream N++ headers directly — this repo has no access to N++ source to diff
   against — so it's a regression guard on top of layout that was worked out once, not a substitute
   for testing against a real plugin DLL when changing these structs.
3. **No new fields beyond what binary compatibility requires.** Don't add convenience members,
   accessors, or non-ABI helper state to these structs — anything beyond the exact real layout
   either breaks binary compatibility (if inserted before the end) or is dead weight (if appended,
   since nothing needs it and it invites future confusion about what's "real" ABI vs. added).
4. **Test against a real plugin DLL when touching these files.** `packages/test_plugin/` is a
   minimal Notepad++-ABI-compliant plugin built by this same project for that purpose — load it via
   `packages/npp-bridge/` and confirm `setInfo`/`getFuncsArray`/`beNotified`/menu commands still
   work end-to-end, since a layout mistake here manifests as silent memory corruption in the loaded
   DLL, not a build failure (`abi_layout_asserts.h` only catches drift *relative to itself*, not
   wrongness baked in from the start).

See `docs/PLUGIN_API_PLAN.md` §1 for the broader plugin-API strategy this directory serves.
