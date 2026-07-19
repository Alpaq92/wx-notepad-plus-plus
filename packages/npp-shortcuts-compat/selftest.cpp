// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone self-test for the Notepad++ shortcuts.xml -> wxNote accelerator translator.
// Builds and runs with no wxWidgets / Scintilla / host dependency (just the C++ standard library),
// so it can gate the pure parse + VK-table + report logic in CI without the runtime plugin.
//   cl /std:c++17 /EHsc selftest.cpp npp_shortcuts_parse.cpp npp_shortcuts_accel.cpp && selftest.exe
// Exit code 0 = all checks passed.

#include "npp_shortcuts_parse.h"
#include "npp_shortcuts_accel.h"

#include <cstdio>
#include <string>

using namespace nppcompat;

static int g_fail = 0;
static void check(bool cond, const char* what)
{
    if (!cond) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

// A representative shortcuts.xml exercising all five sections, a self-closing and a container form,
// a NextKey (multi-binding), an unbound (Key=0) entry, a shell-command UserDefinedCommand (the
// security case), a macro with actions, an XML comment (must be ignored), and an unknown entity
// (must stay literal - the XXE posture).
static const char* kXml =
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
"<!DOCTYPE NotepadPlus [ <!ENTITY xxe \"PWNED\"> ]>\n"
"<NotepadPlus>\n"
"  <InternalCommands>\n"
"    <Shortcut id=\"41006\" Ctrl=\"yes\" Alt=\"no\" Shift=\"no\" Key=\"83\" />\n"
"    <Shortcut id=\"41019\" Ctrl=\"yes\" Alt=\"no\" Shift=\"yes\" Key=\"83\" />\n"
"    <Shortcut id=\"42000\" Ctrl=\"no\" Alt=\"no\" Shift=\"no\" Key=\"0\" />\n"
"    <!-- <Shortcut id=\"99999\" Ctrl=\"yes\" Key=\"88\" /> commented out, must be ignored -->\n"
"  </InternalCommands>\n"
"  <Macros>\n"
"    <Macro name=\"Trim and Save\" Ctrl=\"yes\" Alt=\"yes\" Shift=\"no\" Key=\"83\">\n"
"      <Action type=\"0\" message=\"2170\" wParam=\"0\" lParam=\"0\" sParam=\"\" />\n"
"      <Action type=\"0\" message=\"2004\" wParam=\"0\" lParam=\"0\" sParam=\"\" />\n"
"    </Macro>\n"
"  </Macros>\n"
"  <UserDefinedCommands>\n"
"    <Command name=\"Open in Firefox\" Ctrl=\"no\" Alt=\"yes\" Shift=\"yes\" Key=\"70\">firefox \"$(FULL_CURRENT_PATH)\" &xxe; &amp; echo done</Command>\n"
"  </UserDefinedCommands>\n"
"  <PluginCommands>\n"
"    <PluginCommand moduleName=\"mimeTools.dll\" internalID=\"4\" Ctrl=\"yes\" Alt=\"yes\" Shift=\"no\" Key=\"66\" />\n"
"  </PluginCommands>\n"
"  <ScintillaKeys>\n"
"    <ScintKey ScintID=\"2011\" menuCmdID=\"0\" Ctrl=\"yes\" Alt=\"no\" Shift=\"no\" Key=\"87\">\n"
"      <NextKey Ctrl=\"yes\" Alt=\"no\" Shift=\"yes\" Key=\"88\" />\n"
"    </ScintKey>\n"
"  </ScintillaKeys>\n"
"</NotepadPlus>\n";

int main()
{
    // ---- Parsing ----------------------------------------------------------------------------------
    NppShortcutsDoc doc;
    std::string err;
    check(parseShortcutsXml(kXml, doc, &err), "parses the representative document");
    check(doc.foundRoot, "recognises the <NotepadPlus> root");

    check(doc.internals.size() == 3, "three internal commands (commented one ignored)");
    if (doc.internals.size() == 3) {
        check(doc.internals[0].cmdId == 41006 && doc.internals[0].key.ctrl && doc.internals[0].key.vk == 83,
              "internal[0] = 41006 Ctrl+S");
        check(doc.internals[1].key.ctrl && doc.internals[1].key.shift && doc.internals[1].key.vk == 83,
              "internal[1] = Ctrl+Shift+S");
        check(doc.internals[2].key.vk == 0, "internal[2] Key=0 (unbound)");
    }
    // The commented-out Shortcut id must not appear.
    check(!(doc.internals.size() >= 1 &&
            (doc.internals[0].cmdId == 99999 || (doc.internals.size() > 1 && doc.internals[1].cmdId == 99999))),
          "commented-out <Shortcut> is not parsed");

    check(doc.macros.size() == 1, "one macro");
    if (!doc.macros.empty()) {
        check(doc.macros[0].name == "Trim and Save", "macro name");
        check(doc.macros[0].actionCount == 2, "macro action count");
        check(doc.macros[0].key.ctrl && doc.macros[0].key.alt && doc.macros[0].key.vk == 83, "macro key Ctrl+Alt+S");
    }

    check(doc.userCmds.size() == 1, "one user command");
    if (!doc.userCmds.empty()) {
        check(doc.userCmds[0].name == "Open in Firefox", "user command name");
        // SECURITY: the shell command line is captured verbatim as DATA (nothing runs it here).
        check(contains(doc.userCmds[0].commandLine, "firefox"), "user command line captured (firefox)");
        check(contains(doc.userCmds[0].commandLine, "$(FULL_CURRENT_PATH)"), "user command line variable preserved");
        // XXE: an unknown entity stays LITERAL (never expanded); the predefined &amp; DOES decode.
        check(contains(doc.userCmds[0].commandLine, "&xxe;"), "unknown entity &xxe; kept literal (not expanded)");
        check(!contains(doc.userCmds[0].commandLine, "PWNED"), "declared DTD entity is NOT expanded");
        check(contains(doc.userCmds[0].commandLine, " & echo"), "predefined &amp; decoded to '&'");
    }

    check(doc.pluginCmds.size() == 1, "one plugin command");
    if (!doc.pluginCmds.empty()) {
        check(doc.pluginCmds[0].moduleName == "mimeTools.dll", "plugin module name");
        check(doc.pluginCmds[0].internalId == 4, "plugin internal id");
        check(doc.pluginCmds[0].key.ctrl && doc.pluginCmds[0].key.alt && doc.pluginCmds[0].key.vk == 66,
              "plugin key Ctrl+Alt+B");
    }

    check(doc.scintKeys.size() == 1, "one scintilla key");
    if (!doc.scintKeys.empty()) {
        check(doc.scintKeys[0].sciId == 2011, "scint id 2011");
        check(doc.scintKeys[0].keys.size() == 2, "scint key has a primary + one NextKey");
        if (doc.scintKeys[0].keys.size() == 2) {
            check(doc.scintKeys[0].keys[0].vk == 87 && doc.scintKeys[0].keys[0].ctrl, "scint primary Ctrl+W");
            check(doc.scintKeys[0].keys[1].vk == 88 && doc.scintKeys[0].keys[1].shift, "scint NextKey Ctrl+Shift+X");
        }
    }

    // A document with no recognisable root is rejected.
    NppShortcutsDoc bad;
    check(!parseShortcutsXml("<foo><bar/></foo>", bad, nullptr), "rejects a document with no NotepadPlus root");
    // The <wxNote> root is also accepted (wxNote writes it; N++ reads/writes <NotepadPlus>).
    NppShortcutsDoc wxn;
    check(parseShortcutsXml("<wxNote><InternalCommands><Shortcut id=\"1\" Key=\"65\"/></InternalCommands></wxNote>", wxn, nullptr),
          "accepts a <wxNote> root");

    // ---- VK -> token table ------------------------------------------------------------------------
    check(vkToToken(0x41) == "A" && vkToToken(0x5A) == "Z", "letters A..Z");
    check(vkToToken(0x30) == "0" && vkToToken(0x39) == "9", "digits 0..9");
    check(vkToToken(0x70) == "F1" && vkToToken(0x87) == "F24", "function keys F1..F24");
    check(vkToToken(0x25) == "Left" && vkToToken(0x26) == "Up" && vkToToken(0x27) == "Right" && vkToToken(0x28) == "Down",
          "arrow keys");
    check(vkToToken(0x21) == "PageUp" && vkToToken(0x22) == "PageDown" && vkToToken(0x23) == "End" && vkToToken(0x24) == "Home",
          "navigation cluster");
    check(vkToToken(0x2D) == "Insert" && vkToToken(0x2E) == "Delete", "insert / delete");
    check(vkToToken(0x08) == "Back" && vkToToken(0x09) == "Tab" && vkToToken(0x0D) == "Enter" &&
          vkToToken(0x1B) == "Esc" && vkToToken(0x20) == "Space", "editing / whitespace keys");
    check(vkToToken(0x60) == "KP_0" && vkToToken(0x69) == "KP_9", "numpad digits");
    check(vkToToken(0x6A) == "KP_Multiply" && vkToToken(0x6B) == "KP_Add" && vkToToken(0x6D) == "KP_Subtract" &&
          vkToToken(0x6E) == "KP_Decimal" && vkToToken(0x6F) == "KP_Divide", "numpad operators");
    check(vkToToken(0xBA) == ";" && vkToToken(0xBB) == "=" && vkToToken(0xBC) == "," && vkToToken(0xBD) == "-" &&
          vkToToken(0xBE) == "." && vkToToken(0xBF) == "/", "OEM punctuation (; = , - . /)");
    check(vkToToken(0xC0) == "`" && vkToToken(0xDB) == "[" && vkToToken(0xDC) == "\\" && vkToToken(0xDD) == "]" &&
          vkToToken(0xDE) == "'", "OEM punctuation (` [ \\ ] ')");
    // Unmappable: VK 0, modifier-only VKs, and layout-dependent / unknown VKs -> "".
    check(vkToToken(0).empty(), "VK 0 unmapped");
    check(vkToToken(0x10).empty() && vkToToken(0x11).empty() && vkToToken(0x12).empty(), "modifier-only VKs unmapped");
    check(vkToToken(0x5B).empty(), "VK_LWIN unmapped");

    // ---- buildAccel -------------------------------------------------------------------------------
    { NppKey k; k.ctrl = true; k.vk = 83;                      check(buildAccel(k) == "Ctrl+S", "buildAccel Ctrl+S"); }
    { NppKey k; k.ctrl = true; k.alt = true; k.shift = true; k.vk = 0x41;
      check(buildAccel(k) == "Ctrl+Alt+Shift+A", "buildAccel modifier order Ctrl+Alt+Shift"); }
    { NppKey k; k.vk = 0x70;                                   check(buildAccel(k) == "F1", "buildAccel bare F1"); }
    { NppKey k; k.shift = true; k.vk = 0x2E;                   check(buildAccel(k) == "Shift+Delete", "buildAccel Shift+Delete"); }
    { NppKey k; k.ctrl = true; k.vk = 0;                       check(buildAccel(k).empty(), "buildAccel unmapped -> empty"); }

    // ---- Report (imported / unmapped / unknown / shown-but-not-run) -------------------------------
    ImportTally t;
    t.imported = 5; t.unmapped = 1; t.unknown = 2;
    t.unmappedNotes.push_back("menu id 42000 (VK 0)");
    t.unknownNotes.push_back("plugin npp.mimeTools.4");
    const std::string report = formatReport("C:/tmp/shortcuts.xml", doc, t);
    check(contains(report, "Imported : 5"), "report shows imported count");
    check(contains(report, "Unmapped : 1"), "report shows unmapped count");
    check(contains(report, "Unknown  : 2"), "report shows unknown count");
    check(contains(report, "Open in Firefox"), "report lists the user command by name");
    check(contains(report, "[NOT EXECUTED]"), "report flags the user command as NOT executed");
    check(contains(report, "Trim and Save") && contains(report, "[NOT REPLAYED]"), "report flags the macro as NOT replayed");
    check(contains(report, "NEVER executed"), "report states user commands are never executed");
    check(contains(report, "Notepad++ (imported)"), "report names the target scheme");

    if (g_fail == 0) std::printf("ALL PASS (npp-shortcuts-compat selftest)\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
