// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-shortcuts-compat runtime plugin - the Nib plugin that re-adds Notepad++ keyboard
// shortcuts to wxNote by importing a Notepad++ shortcuts.xml.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// It registers a single well-known command, "host.shortcuts.import" (titled "Import Notepad++
// shortcuts.xml..."). When invoked - from the Plugins menu, or forwarded by the core's Run >
// "Validate shortcuts.xml" (command 49001) - it finds a shortcuts.xml, parses its five sections,
// translates each keystroke into wxNote's portable accelerator spelling, and feeds them into the
// host as a named "Notepad++ (imported)" scheme through the nib.keymap/1 capability. It then shows
// a validation report (imported / unmapped / unknown / shown-but-not-run).
//
// This is the ONE place that both knows the Notepad++ shortcuts.xml format and speaks it, which is
// exactly why it is a separate, optional, GPL module rather than part of the Apache-2.0 core - the
// core's nib.keymap surface is a generic "here is a key and what it should do", nothing N++-shaped.
//
// SECURITY: <UserDefinedCommands> carry shell command lines (e.g. "firefox $(FULL_CURRENT_PATH)").
// They are treated as PURE DATA - parsed and surfaced in the report for review, and NEVER executed.
// There is no ShellExecute/system/CreateProcess/wxExecute anywhere in this package. Import is also
// read-only on the foreign file; the plugin never writes shortcuts.xml back.

#include "nib.h"
#include "npp_shortcuts_parse.h"
#include "npp_shortcuts_accel.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace nppcompat;

namespace {

// The scheme this plugin registers. Committing over the same id is idempotent (a re-import replaces
// it), and the parent is the host's bundled "notepad++" preset when present so imported deltas
// compose over N++ defaults; an unknown parent resolves against the host default (host-decided).
const char* const NPP_SCHEME_ID    = "org.wxnote.npp-imported";
const char* const NPP_SCHEME_TITLE = "Notepad++ (imported)";
const char* const NPP_PARENT       = "notepad++";
const char* const IMPORT_CMD_ID    = "host.shortcuts.import";   // the well-known id the core forwards 49001 to

NibLogFn    g_log     = nullptr;   // stashed from the bootstrap for the no-panel fallback
NibPanel*   g_panel   = nullptr;   // cached report panel (register once, reuse across invocations)

// "mimeTools.dll" (or a path) -> "mimeTools": strip any directory and a trailing .dll/.so/.dylib.
std::string moduleBaseName(const std::string& module)
{
    std::string b = module;
    const size_t slash = b.find_last_of("/\\");
    if (slash != std::string::npos) b = b.substr(slash + 1);
    for (const char* ext : { ".dll", ".dylib", ".so" }) {
        const size_t el = std::strlen(ext);
        if (b.size() >= el) {
            std::string tail = b.substr(b.size() - el);
            for (char& c : tail) c = (char)std::tolower((unsigned char)c);
            if (tail == ext) { b.resize(b.size() - el); break; }
        }
    }
    return b;
}

// Locate a Notepad++ shortcuts.xml and read it. Primary source is <userdata>/shortcuts.xml (drop the
// file there); as a convenience a real Windows Notepad++ install's %APPDATA%\Notepad++\shortcuts.xml
// is also probed. Returns the file contents (and sets pathOut) or "" if none is found.
std::string locateAndRead(NibHost* host, NibQueryFn query, std::string& pathOut)
{
    if (const NibPathsApi* paths = static_cast<const NibPathsApi*>(query(host, NIB_IFACE_PATHS, 1))) {
        char buf[4096];
        const int n = paths->user_data_dir(host, buf, static_cast<int>(sizeof(buf)));
        if (n > 0) {
            // The host returns the FULL UTF-8 length snprintf-style, NOT the bytes it actually copied:
            // nibCopyUtf8() clamps the copy to cap-1 but always returns s.size(). So a user_data_dir
            // >= sizeof(buf) yields n >= 4096 while only 4095 bytes + NUL were written; building
            // std::string(buf, n) would read n-4095 bytes past the buffer. Clamp to what fits.
            const size_t len = std::min<size_t>(static_cast<size_t>(n), sizeof(buf) - 1);
            const std::string p = std::string(buf, len) + "/shortcuts.xml";
            const std::string x = readFileToString(p);
            if (!x.empty()) { pathOut = p; return x; }
        }
    }
    if (const char* appdata = std::getenv("APPDATA")) {   // Windows: the canonical N++ config location
        const std::string p = std::string(appdata) + "\\Notepad++\\shortcuts.xml";
        const std::string x = readFileToString(p);
        if (!x.empty()) { pathOut = p; return x; }
    }
    return "";
}

// Parse + translate + feed a shortcuts.xml into the host as the "Notepad++ (imported)" scheme, filling
// `tally` and `srcPath`. Returns true if a scheme was committed. The per-binding bind_* return codes are
// what turn into the imported/unknown tallies - no host read API is needed to build the report.
bool runImport(NibHost* host, NibQueryFn query, ImportTally& tally, std::string& srcPath,
               NppShortcutsDoc& docOut, bool& foundFile)
{
    foundFile = false;
    const NibKeymapApi* keymap = static_cast<const NibKeymapApi*>(query(host, NIB_IFACE_KEYMAP, 1));
    if (!keymap) return false;   // host too old to accept keybindings - graceful no-op

    const std::string xml = locateAndRead(host, query, srcPath);
    if (xml.empty()) return false;
    foundFile = true;

    if (!parseShortcutsXml(xml, docOut, nullptr)) return false;

    NibKeymapScheme* s = keymap->begin_scheme(host, NPP_SCHEME_ID, NPP_SCHEME_TITLE, NPP_PARENT);
    if (!s) return false;

    // Internal commands -> bind_id (frozen kCmd/IDM numeric id; nth>0 = an additional N++ NextKey binding).
    for (const NppInternal& it : docOut.internals) {
        const std::string accel = buildAccel(it.key);
        if (accel.empty()) {
            ++tally.unmapped;
            tally.unmappedNotes.push_back("menu id " + std::to_string(it.cmdId) +
                                          " (VK " + std::to_string(it.key.vk) + ")");
            continue;
        }
        if (keymap->bind_id(host, s, it.cmdId, accel.c_str(), it.nth > 0 ? 1 : 0)) ++tally.imported;
        else { ++tally.unknown; tally.unknownNotes.push_back("menu id " + std::to_string(it.cmdId)); }
    }

    // Scintilla keys -> bind_editor (SCI_* id). The first mappable key REPLACES the default; further
    // mappable keys ADD (N++ NextKey). `replace` only advances past the first slot on a successful bind,
    // so an unmapped primary key does not turn a following NextKey into an unintended additional binding.
    for (const NppScintKey& sk : docOut.scintKeys) {
        bool replace = true;
        for (const NppKey& k : sk.keys) {
            const std::string accel = buildAccel(k);
            if (accel.empty()) {
                ++tally.unmapped;
                tally.unmappedNotes.push_back("editor SCI " + std::to_string(sk.sciId) +
                                              " (VK " + std::to_string(k.vk) + ")");
                continue;
            }
            if (keymap->bind_editor(host, s, sk.sciId, accel.c_str(), replace ? 0 : 1)) {
                ++tally.imported;
                replace = false;
            } else {
                ++tally.unknown;
                tally.unknownNotes.push_back("editor SCI " + std::to_string(sk.sciId));
            }
        }
    }

    // Plugin commands -> bind_name (best-effort symbolic name; usually not known to this build unless a
    // bridge registered a matching alias - risk #11 in the plan). The moduleName basename convention
    // mirrors N++'s FuncItem indexing: "npp.<module>.<internalID>".
    for (const NppPluginCmd& pc : docOut.pluginCmds) {
        const std::string accel = buildAccel(pc.key);
        const std::string name = "npp." + moduleBaseName(pc.moduleName) + "." + std::to_string(pc.internalId);
        if (accel.empty()) {
            ++tally.unmapped;
            tally.unmappedNotes.push_back("plugin " + name + " (VK " + std::to_string(pc.key.vk) + ")");
            continue;
        }
        if (keymap->bind_name(host, s, name.c_str(), accel.c_str(), 0)) ++tally.imported;
        else { ++tally.unknown; tally.unknownNotes.push_back("plugin " + name); }
    }

    // UserCommands + Macros are intentionally NOT bound here: there is no execution/replay path for
    // either, and their shell command lines must never run. They are surfaced in the report only.

    keymap->commit_scheme(host, s, /*activate=*/0);   // leave activation to the user (plan 6.4)
    return true;
}

// Show the report in a dockable host panel (registered once, reused). If nib.panels is unavailable,
// fall back to logging a one-line summary through the bootstrap log fn.
void presentReport(NibHost* host, NibQueryFn query, const std::string& report)
{
    if (const NibPanelsApi* panels = static_cast<const NibPanelsApi*>(query(host, NIB_IFACE_PANELS, 1))) {
        if (!g_panel)
            g_panel = panels->register_panel(host, "org.wxnote.npp-shortcuts.report",
                                             "Notepad++ Import", NIB_DOCK_BOTTOM);
        if (g_panel) {
            panels->set_text(host, g_panel, report.c_str());
            panels->show(host, g_panel, 1);
            return;
        }
    }
    if (g_log) g_log(host, 1, "npp-shortcuts-compat: import finished (no panel host available for the full report)");
}

// The registered command. Runs the whole flow: import, then show the validation report.
void importCommand(NibHost* host, NibQueryFn query, void* /*user*/)
{
    ImportTally tally;
    NppShortcutsDoc doc;
    std::string src;
    bool foundFile = false;
    const bool committed = runImport(host, query, tally, src, doc, foundFile);

    std::string report;
    if (!foundFile) {
        report = "Notepad++ shortcuts.xml import\n"
                 "==============================\n\n"
                 "No shortcuts.xml was found.\n\n"
                 "Place a Notepad++ shortcuts.xml in wxNote's user-data directory (the same folder\n"
                 "as shortcuts.json), then run this command again. On Windows an installed Notepad++'s\n"
                 "%APPDATA%\\Notepad++\\shortcuts.xml is also detected automatically.\n";
    } else if (!committed) {
        report = "Notepad++ shortcuts.xml import\n"
                 "==============================\n\n"
                 "A shortcuts.xml was found but could not be imported (unparseable file, or the host\n"
                 "does not offer the keymap capability).\n";
    } else {
        report = formatReport(src, doc, tally);
    }
    presentReport(host, query, report);
}

void activate(NibHost* host, NibQueryFn query)
{
    // Register the command; the host surfaces it in the Plugins menu and the core forwards command
    // 49001 ("Validate shortcuts.xml") to this well-known id. The import itself is deliberately NOT run
    // at load time: committing writes the host's shortcuts.json, and doing that unprompted on every
    // launch would risk the two-instance write race the plan flags (4.1). Import is a user action.
    if (const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1)))
        cmds->register_command(host, IMPORT_CMD_ID, "Import Notepad++ shortcuts.xml...", importCommand, nullptr);
}

const NibPluginApi g_api = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnote.npp-shortcuts-compat", activate, nullptr
};

}  // namespace

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* bs)
{
    if (bs && bs->struct_size >= sizeof(NibBootstrap)) g_log = bs->log;   // stash for the no-panel fallback
    return &g_api;
}
