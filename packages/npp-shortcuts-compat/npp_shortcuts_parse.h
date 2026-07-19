// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-shortcuts-compat - parse a Notepad++ shortcuts.xml into a data model.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// A small, self-contained reader for the one Notepad++ format this plugin exists to
// interoperate with: shortcuts.xml, the per-user keybinding file. It has NO wxWidgets /
// Scintilla dependency (plain std::string in, a NppShortcutsDoc out) so the whole
// file -> data-model -> accelerator-string pipeline is unit-testable standalone.
//
// Five sections are recognised, mirroring Notepad++'s shortcuts.xml exactly:
//   <InternalCommands>   <Shortcut id=... Ctrl Alt Shift Key [nth]/>
//   <Macros>             <Macro name=... Ctrl Alt Shift Key> <Action .../>* </Macro>
//   <UserDefinedCommands><Command name=... Ctrl Alt Shift Key>SHELL COMMAND LINE</Command>
//   <PluginCommands>     <PluginCommand moduleName=... internalID=... Ctrl Alt Shift Key/>
//   <ScintillaKeys>      <ScintKey ScintID=... Ctrl Alt Shift Key> <NextKey .../>* </ScintKey>
//
// SECURITY: <Command> element text is a shell command line (e.g. "firefox $(FULL_CURRENT_PATH)").
// It is captured here as OPAQUE DATA and NEVER executed anywhere in this plugin (there is no
// ShellExecute/system/CreateProcess/wxExecute call in the whole package). The parser is also
// XXE-hardened: it never expands external entities or a DTD - only the five predefined XML
// entities and ASCII numeric character references are decoded; every other "&name;" stays literal.

#pragma once

#include <string>
#include <vector>

namespace nppcompat {

// One keystroke as Notepad++ stores it: three modifier booleans plus a Windows virtual-key
// code (decimal). vk == 0 means "no key" (an unbound/empty slot in N++).
struct NppKey {
    bool ctrl  = false;
    bool alt   = false;
    bool shift = false;
    int  vk    = 0;                 // Windows VK decimal (0 = none)
    bool hasKey() const { return vk != 0; }
};

// <Shortcut id="41006" Ctrl="yes" Alt="no" Shift="no" Key="83" [nth="0"]/> - a built-in command's key.
struct NppInternal { int cmdId = 0; NppKey key; int nth = 0; };

// <ScintKey ScintID="2011" ...> with zero or more <NextKey/> children (N++ NextKey = extra binding).
struct NppScintKey { int sciId = 0; std::vector<NppKey> keys; };

// <PluginCommand moduleName="mimeTools.dll" internalID="0" .../> - a compiled-plugin menu entry.
struct NppPluginCmd { std::string moduleName; int internalId = 0; NppKey key; };

// <Command name="Google" ...>https://...$(CURRENT_WORD)</Command> - name + key + a SHELL COMMAND LINE
// held as pure data (see the SECURITY note above; commandLine is reported for review, never executed).
struct NppUserCmd { std::string name; NppKey key; std::string commandLine; };

// <Macro name="Trim and Save" ...> <Action .../>* </Macro> - name + key; the recorded actions are
// counted (actionCount) but NOT translated or replayed in v1 (there is no macro-execution path here).
struct NppMacro { std::string name; NppKey key; int actionCount = 0; };

// The whole parsed document.
struct NppShortcutsDoc {
    std::vector<NppInternal>  internals;
    std::vector<NppScintKey>  scintKeys;
    std::vector<NppPluginCmd> pluginCmds;
    std::vector<NppUserCmd>   userCmds;
    std::vector<NppMacro>     macros;
    bool foundRoot = false;        // recognised a <NotepadPlus> or <wxNote> root element
};

// Parse a Notepad++ shortcuts.xml document. Wx-free, XXE-hardened. Returns true when a
// <NotepadPlus>/<wxNote> root is present (an otherwise-empty document still parses to an empty
// doc and returns true); returns false with *err set if there is no recognisable root at all.
bool parseShortcutsXml(const std::string& xml, NppShortcutsDoc& out, std::string* err = nullptr);

// Read an entire file into a string (binary). Returns "" if it cannot be opened.
std::string readFileToString(const std::string& path);

// ------------------------------------------------------------------------------------------------
// A human-readable import/validation report over a parsed document. Kept here (wx-free, standalone-
// testable) because it is the payload of the "Validate shortcuts.xml" flow (plan 6.5): the plugin
// fills in the runtime bind results, this formats them alongside the parse diagnostics.

// Per-entry outcome tallies the plugin accumulates while feeding the host (bind_* returns 1/0).
struct ImportTally {
    int imported = 0;              // bind_* returned 1
    int unmapped = 0;              // key had no portable wx equivalent (never sent to the host)
    int unknown  = 0;              // bind_* returned 0 (id/name/SCI id not known to this build)
    std::vector<std::string> unmappedNotes;   // "menu 41006 (VK 220)" style lines, for the report
    std::vector<std::string> unknownNotes;    // "plugin npp.mimeTools.0" style lines, for the report
};

// Format the validation report. Lists sections found, the imported/unmapped/unknown tallies, and the
// UserCommands + Macros as "shown for review only - NEVER executed / replayed" (the security stance).
std::string formatReport(const std::string& sourcePath, const NppShortcutsDoc& doc,
                         const ImportTally& tally);

}  // namespace nppcompat
