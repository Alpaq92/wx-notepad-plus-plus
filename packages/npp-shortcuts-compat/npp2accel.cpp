// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp2accel - convert a Notepad++ shortcuts.xml into wxNote portable accelerator strings.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Standalone command-line front end to the shortcuts translator (no wxWidgets / Scintilla / host
// needed to run it). Reads a shortcuts.xml and prints, per section, the wxNote accelerator string
// each entry would bind - the same strings the runtime plugin feeds the host through nib.keymap -
// followed by a validation-style summary.
//   npp2accel path/to/shortcuts.xml
//
// SECURITY: <UserDefinedCommands> shell command lines are printed for review, NEVER executed.

#include "npp_shortcuts_parse.h"
#include "npp_shortcuts_accel.h"

#include <cstdio>
#include <string>

using namespace nppcompat;

static std::string accelOrDash(const NppKey& k)
{
    const std::string a = buildAccel(k);
    return a.empty() ? "[unmapped]" : a;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: npp2accel <shortcuts.xml>\n");
        return 2;
    }
    const std::string path = argv[1];
    const std::string xml = readFileToString(path);
    if (xml.empty()) { std::fprintf(stderr, "error: cannot open %s\n", path.c_str()); return 1; }

    NppShortcutsDoc doc;
    std::string err;
    if (!parseShortcutsXml(xml, doc, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }

    ImportTally tally;   // offline tally: mappable -> "imported", unmappable -> "unmapped" (unknown is
                         // a runtime-only judgement made by the host, so it stays 0 here).

    std::printf("Internal commands (-> bind_id):\n");
    for (const NppInternal& it : doc.internals) {
        const std::string a = buildAccel(it.key);
        std::printf("  id %-6d  %s%s\n", it.cmdId, accelOrDash(it.key).c_str(), it.nth > 0 ? "  (additional)" : "");
        if (a.empty()) ++tally.unmapped; else ++tally.imported;
    }

    std::printf("\nScintilla keys (-> bind_editor):\n");
    for (const NppScintKey& sk : doc.scintKeys) {
        std::printf("  SCI %-6d ", sk.sciId);
        for (size_t i = 0; i < sk.keys.size(); ++i) {
            const std::string a = buildAccel(sk.keys[i]);
            std::printf("%s%s", i ? " | " : "", accelOrDash(sk.keys[i]).c_str());
            if (a.empty()) ++tally.unmapped; else ++tally.imported;
        }
        std::printf("\n");
    }

    std::printf("\nPlugin commands (-> bind_name):\n");
    for (const NppPluginCmd& pc : doc.pluginCmds) {
        const std::string a = buildAccel(pc.key);
        std::printf("  %s#%d  %s\n", pc.moduleName.c_str(), pc.internalId, accelOrDash(pc.key).c_str());
        if (a.empty()) ++tally.unmapped; else ++tally.imported;
    }

    std::printf("\n%s\n", formatReport(path, doc, tally).c_str());
    return 0;
}
