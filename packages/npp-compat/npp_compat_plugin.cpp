// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat runtime plugin - brings a user's Notepad++ setup into wxNote.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Three commands, on the Plugins menu:
//
//   Import from Notepad++...           reads a Notepad++ settings folder - %APPDATA%\Notepad++ on Windows,
//                                      and everywhere <wxNote user data>/notepad++, where a copy of that
//                                      folder can be put - and imports everything wxNote has a place for:
//                                      config.xml -> settings, shortcuts.xml -> a key-binding scheme,
//                                      contextMenu.xml -> the right-click menu, stylers.xml and themes/ ->
//                                      themes, langs.xml and the theme's user-defined keywords ->
//                                      languages.yaml. One report says what came across and what had nowhere
//                                      to go.
//   Import the Open Notepad++ File     the same for one file open in wxNote: any of those, plus a session
//                                      (its files open) or a Project-panel workspace (a .yaml workspace is
//                                      written beside it).
//   Import Notepad++ shortcuts.xml...  the key bindings alone. Well-known id host.shortcuts.import, which
//                                      the core's Run > "Validate shortcuts.xml" (command 49001) forwards to.
//
// This is the one place that knows both Notepad++'s files and wxNote's, which is exactly why it is a
// separate, optional, GPL module rather than part of the Apache-2.0 core (LICENSING.md). The translations
// live in npp_translate.h; this file finds the files, hands the results to the host through generic Nib
// interfaces - nib.settings, nib.keymap, nib.documents, nib.paths - and writes the report.
//
// SECURITY: <UserDefinedCommands> in shortcuts.xml carry shell command lines (e.g. "firefox
// $(FULL_CURRENT_PATH)"). They are PURE DATA here - surfaced in the report for review and NEVER executed:
// there is no ShellExecute/system/CreateProcess/wxExecute anywhere in this package. Every Notepad++ file
// is only read; none is ever written back.

#include "nib.h"
#include "npp_shortcuts_parse.h"
#include "npp_shortcuts_accel.h"
#include "npp_translate.h"

#include "lang_table.h"        // wxNote's languages, to lay an import over languages.yaml
#include "language_defs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using namespace nppcompat;
namespace fs = std::filesystem;

namespace {

// The scheme shortcuts.xml becomes. Committing over the same id is idempotent (a re-import replaces it);
// an unknown parent resolves against the host default (host-decided).
const char* const NPP_SCHEME_ID    = "org.wxnote.npp-imported";
const char* const NPP_SCHEME_TITLE = "Notepad++ (imported)";
const char* const NPP_PARENT       = "notepad++";
const char* const SHORTCUTS_CMD_ID = "host.shortcuts.import";   // the well-known id the core forwards 49001 to
const char* const IMPORT_CMD_ID    = "npp.import";
const char* const IMPORT_FILE_ID   = "npp.import.file";

NibLogFn  g_log   = nullptr;   // stashed from the bootstrap for the no-panel fallback
NibPanel* g_panel = nullptr;   // the report panel (registered once, reused)

// ---- files: UTF-8 paths through std::filesystem, so a profile folder like C:\Users\Łukasz works ------

fs::path pathFromUtf8(const std::string& s) { return fs::u8path(s); }
std::string utf8Of(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string readFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Write via a sibling temporary and a rename, so a failed write leaves the previous file whole.
bool writeFile(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.close();   // flushes: a full disk fails here, before the rename could put a cut-off file in place
        if (f.fail()) { fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// Before an import replaces a file, keep the one there aside: as <name>.bak, or .bak2, .bak3... when an
// earlier backup exists - never over one, which may be the only copy left of the user's own file.
enum class Replaced { Written, Unchanged, Failed };
Replaced replaceFile(const fs::path& dest, const std::string& text, std::string& report)
{
    std::error_code ec;
    if (fs::exists(dest, ec))
    {
        if (readFile(dest) == text) return Replaced::Unchanged;
        fs::path kept;
        for (int n = 1; n < 100 && kept.empty(); ++n)
        {
            fs::path b = dest;
            b += n == 1 ? std::string(".bak") : ".bak" + std::to_string(n);
            if (fs::exists(b, ec)) continue;
            if (!fs::copy_file(dest, b, fs::copy_options::none, ec) || ec) break;
            kept = b;
        }
        if (kept.empty())
        {
            report += "  " + utf8Of(dest) + " could not be kept aside, so it was not replaced\n";
            return Replaced::Failed;
        }
        report += "  the previous " + utf8Of(dest.filename()) + " is kept as " + utf8Of(kept.filename()) + "\n";
    }
    if (!writeFile(dest, text))
    {
        report += "  could not write " + utf8Of(dest) + "\n";
        return Replaced::Failed;
    }
    return Replaced::Written;
}

fs::path userDataDir(NibHost* host, NibQueryFn query)
{
    const NibPathsApi* paths = static_cast<const NibPathsApi*>(query(host, NIB_IFACE_PATHS, 1));
    if (!paths) return fs::path();
    char buf[4096];
    const int n = paths->user_data_dir(host, buf, static_cast<int>(sizeof(buf)));
    if (n <= 0) return fs::path();
    // The host returns the FULL length snprintf-style, not the bytes it copied: clamp to what fits.
    return pathFromUtf8(std::string(buf, std::min<size_t>(static_cast<size_t>(n), sizeof(buf) - 1)));
}

// Where a Notepad++ keeps its settings: the installed one's %APPDATA%\Notepad++ on Windows, else (or when
// that has nothing) <wxNote user data>/notepad++, the place to put a copy of that folder.
std::vector<fs::path> nppSettingsDirs(NibHost* host, NibQueryFn query)
{
    std::vector<fs::path> out;
#ifdef _WIN32
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) out.push_back(fs::path(appdata) / L"Notepad++");
#endif
    const fs::path data = userDataDir(host, query);
    if (!data.empty()) out.push_back(data / "notepad++");
    return out;
}

std::string activeDocumentPath(NibHost* host, NibQueryFn query)
{
    const NibDocumentsApi* docs = static_cast<const NibDocumentsApi*>(query(host, NIB_IFACE_DOCUMENTS, 1));
    if (!docs) return std::string();
    char buf[4096];
    const int n = docs->active_path(host, buf, static_cast<int>(sizeof(buf)));
    return n > 0 && n < static_cast<int>(sizeof(buf)) ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

// ---- the translations, handed to the host --------------------------------------------------------

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

// Parse + translate + feed a shortcuts.xml into the host as the "Notepad++ (imported)" scheme, filling
// `tally`. Returns true if a scheme was committed. The per-binding bind_* return codes are what turn into
// the imported/unknown tallies - no host read API is needed to build the report.
bool importShortcuts(NibHost* host, NibQueryFn query, const std::string& xml, ImportTally& tally, NppShortcutsDoc& doc)
{
    const NibKeymapApi* keymap = static_cast<const NibKeymapApi*>(query(host, NIB_IFACE_KEYMAP, 1));
    if (!keymap || !parseShortcutsXml(xml, doc, nullptr)) return false;

    NibKeymapScheme* s = keymap->begin_scheme(host, NPP_SCHEME_ID, NPP_SCHEME_TITLE, NPP_PARENT);
    if (!s) return false;

    // Internal commands -> bind_id (frozen kCmd/IDM numeric id; nth>0 = an additional N++ NextKey binding).
    for (const NppInternal& it : doc.internals) {
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
    for (const NppScintKey& sk : doc.scintKeys) {
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
    // bridge registered a matching alias). The moduleName basename convention mirrors N++'s FuncItem
    // indexing: "npp.<module>.<internalID>".
    for (const NppPluginCmd& pc : doc.pluginCmds) {
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
    // Leave activation to the user. The host refuses a scheme it cannot keep (its keybindings.yaml does
    // not parse, or a newer wxNote wrote it), and the report must not claim an import that is gone.
    return keymap->commit_scheme(host, s, /*activate=*/0) != 0;
}

void importConfig(NibHost* host, NibQueryFn query, const std::string& xml, std::string& report)
{
    ConfigTranslation t;
    std::string err;
    if (!settingsFromConfig(xml, t, &err)) { report += "  config.xml could not be read: " + err + "\n"; return; }
    const NibSettingsApi* settings = static_cast<const NibSettingsApi*>(query(host, NIB_IFACE_SETTINGS, 1));
    if (!settings) { report += "  this wxNote cannot take settings from a plugin (nib.settings/1 is missing)\n"; return; }
    int applied = 0;
    std::string refused;
    for (const auto& kv : t.settings)
    {
        // Notepad++'s theme: the copy this import made of it ("<name> (Notepad++)", imported first), else
        // wxNote's own theme of that name. wxNote refuses a name it has no theme for.
        if (kv.first == "ui.colorTheme" && settings->set(host, kv.first.c_str(), (kv.second + " (Notepad++)").c_str()))
            ++applied;
        else if (settings->set(host, kv.first.c_str(), kv.second.c_str())) ++applied;
        else refused += "    " + kv.first + ": " + kv.second + (kv.first == "ui.colorTheme" ? " (no such theme here)" : "") + "\n";
    }
    report += "  " + std::to_string(applied) + " setting(s) imported into settings.yaml - they apply at the next start\n";
    if (!refused.empty()) report += "  refused by wxNote (settings.yaml has an error, or the value is not allowed):\n" + refused;
    for (const std::string& s : t.notTranslated) report += "  not imported: " + s + "\n";
}

void importContextMenu(NibHost* host, NibQueryFn query, const std::string& xml, std::string& report)
{
    std::string yaml, err;
    std::vector<std::string> notTranslated;
    if (!contextMenuFromNpp(xml, yaml, notTranslated, &err)) { report += "  contextMenu.xml could not be read: " + err + "\n"; return; }
    const fs::path data = userDataDir(host, query);
    if (data.empty()) { report += "  no user data folder to write contextmenu.yaml into\n"; return; }
    const fs::path dest = data / "contextmenu.yaml";
    switch (replaceFile(dest, yaml, report))   // the user's own menu is kept aside, not lost
    {
        case Replaced::Written:   report += "  right-click menu written to " + utf8Of(dest) + "\n"; break;
        case Replaced::Unchanged: report += "  the right-click menu is already this one\n"; break;
        case Replaced::Failed:    break;
    }
    for (const std::string& s : notTranslated) report += "  not imported: " + s + "\n";
}

void importTheme(NibHost* host, NibQueryFn query, const std::string& xml, const std::string& name, std::string& report)
{
    wxntheme::Theme t;
    std::string err;
    if (!themeFromNpp(xml, t, &err)) { report += "  theme \"" + name + "\" could not be read: " + err + "\n"; return; }
    if (t.header.empty()) t.header = "# " + name + " - imported from Notepad++\n";
    const std::string yaml = wxntheme::emit(t);
    const fs::path data = userDataDir(host, query);
    if (data.empty() || yaml.empty()) { report += "  could not write the theme \"" + name + "\"\n"; return; }
    // A re-import must not silently undo edits made to the copy in the Style Configurator: replaceFile
    // keeps the earlier one aside.
    switch (replaceFile(data / "themes" / pathFromUtf8(name + ".yaml"), yaml, report))
    {
        case Replaced::Written:   report += "  theme \"" + name + "\" - choose it in Settings > Style Configurator\n"; break;
        case Replaced::Unchanged: report += "  theme \"" + name + "\" is already imported\n"; break;
        case Replaced::Failed:    break;
    }
}

// Notepad++'s own langs.model.xml, which tells the user's additions in langs.xml from Notepad++'s lists:
// beside langs.xml (a portable Notepad++, or a copy put there), else in an installed one's program folder.
fs::path findLangsModel(const fs::path& dir)
{
    std::vector<fs::path> candidates = { dir / "langs.model.xml" };
#ifdef _WIN32
    for (const wchar_t* var : { L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)" })
        if (const wchar_t* pf = _wgetenv(var)) candidates.push_back(fs::path(pf) / L"Notepad++" / L"langs.model.xml");
#endif
    std::error_code ec;
    for (const fs::path& p : candidates) if (fs::exists(p, ec)) return p;
    return fs::path();
}

// langs.xml (against `model`), and the active theme's user-defined keywords, into the user's languages.yaml:
// laid over what it already says (language_defs.h), the previous file kept aside.
void importLanguages(NibHost* host, NibQueryFn query, const std::string& langs, const fs::path& model,
                     const std::string& theme, std::string& report)
{
    LanguagesTranslation t;
    std::string err;
    if (!languagesFromNpp(langs, model.empty() ? std::string() : readFile(model), theme, t, &err))
    {
        report += "  langs.xml could not be read: " + err + "\n";
        return;
    }
    if (t.compared) report += "  compared with " + utf8Of(model) + "\n";
    if (t.yaml.empty()) report += "  nothing to bring across: no added extensions or keywords, no changed comment tokens\n";
    else
    {
        const fs::path data = userDataDir(host, query);
        if (data.empty()) { report += "  no user data folder to write languages.yaml into\n"; return; }
        const fs::path dest = data / "languages.yaml";
        std::string text = readFile(dest);
        if (text.empty()) text = wxnLanguagesYamlTemplate();
        if (!wxnLangDefsMerge(text, t.yaml, &err, wxnLangCanonicalName, wxnLangLexerOf))
        {
            report += "  languages.yaml has an error (" + err + "), so it was not changed\n";
            return;
        }
        std::string names;
        for (const std::string& l : t.languages) names += (names.empty() ? "" : ", ") + l;
        switch (replaceFile(dest, text, report))
        {
            case Replaced::Written:   report += "  languages.yaml now has " + names + " - from the next document shown\n"; break;
            case Replaced::Unchanged: report += "  languages.yaml already has these\n"; break;
            case Replaced::Failed:    break;
        }
    }
    for (const std::string& s : t.notTranslated) report += "  not imported: " + s + "\n";
}

void openSession(NibHost* host, NibQueryFn query, const std::string& xml, std::string& report)
{
    std::vector<std::string> files;
    std::string err;
    if (!sessionFilesFromNpp(xml, files, &err)) { report += "  the session could not be read: " + err + "\n"; return; }
    const NibDocumentsApi* docs = static_cast<const NibDocumentsApi*>(query(host, NIB_IFACE_DOCUMENTS, 1));
    int opened = 0;
    for (const std::string& f : files)
        if (docs && docs->open(host, f.c_str())) ++opened;
        else report += "  could not open " + f + "\n";
    report += "  opened " + std::to_string(opened) + " of the session's " + std::to_string(files.size()) + " file(s)\n";
}

void importWorkspace(const std::string& xml, const fs::path& source, std::string& report)
{
    std::string yaml, err;
    if (!workspaceFromNpp(xml, utf8Of(source.parent_path()), yaml, &err)) { report += "  the workspace could not be read: " + err + "\n"; return; }
    fs::path dest = source;
    dest.replace_extension(".yaml");
    std::error_code ec;
    if (fs::exists(dest, ec)) dest = source.parent_path() / pathFromUtf8(utf8Of(source.stem()) + " (from Notepad++).yaml");
    switch (replaceFile(dest, yaml, report))
    {
        case Replaced::Written:
        case Replaced::Unchanged:
            report += "  workspace written to " + utf8Of(dest) + " - open it from a Project panel (Open Workspace)\n";
            break;
        case Replaced::Failed: break;
    }
}

// ---- the report --------------------------------------------------------------------------------------

void presentReport(NibHost* host, NibQueryFn query, const std::string& report)
{
    if (const NibPanelsApi* panels = static_cast<const NibPanelsApi*>(query(host, NIB_IFACE_PANELS, 1))) {
        if (!g_panel)
            g_panel = panels->register_panel(host, "org.wxnote.npp-compat.report", "Notepad++ Import", NIB_DOCK_BOTTOM);
        if (g_panel) {
            panels->set_text(host, g_panel, report.c_str());
            panels->show(host, g_panel, 1);
            return;
        }
    }
    if (g_log) g_log(host, 1, "npp-compat: import finished (no panel host available for the full report)");
}

// ---- the commands ------------------------------------------------------------------------------------

// Import Notepad++ shortcuts.xml...: <user data>/shortcuts.xml first (drop a file there), then the
// Notepad++ settings folders.
void shortcutsCommand(NibHost* host, NibQueryFn query, void*)
{
    std::vector<fs::path> candidates;
    const fs::path data = userDataDir(host, query);
    if (!data.empty()) candidates.push_back(data / "shortcuts.xml");
    for (const fs::path& d : nppSettingsDirs(host, query)) candidates.push_back(d / "shortcuts.xml");
    for (const fs::path& p : candidates)
    {
        const std::string xml = readFile(p);
        if (xml.empty()) continue;
        ImportTally tally;
        NppShortcutsDoc doc;
        if (importShortcuts(host, query, xml, tally, doc)) presentReport(host, query, formatReport(utf8Of(p), doc, tally));
        else presentReport(host, query, "Notepad++ shortcuts.xml import\n==============================\n\n"
                                        "A shortcuts.xml was found at " + utf8Of(p) + " but could not be imported\n"
                                        "(unparseable file, a host without the keymap capability, or a\n"
                                        "keybindings.yaml wxNote cannot write - see the status bar).\n");
        return;
    }
    presentReport(host, query, "Notepad++ shortcuts.xml import\n==============================\n\n"
                               "No shortcuts.xml was found.\n\n"
                               "Put a Notepad++ shortcuts.xml in wxNote's user data folder (the one holding\n"
                               "keybindings.yaml) and run this command again. On Windows an installed\n"
                               "Notepad++'s %APPDATA%\\Notepad++\\shortcuts.xml is found automatically.\n");
}

// Import from Notepad++...: everything in the first Notepad++ settings folder that has a config.xml.
void importCommand(NibHost* host, NibQueryFn query, void*)
{
    std::string report = "Import from Notepad++\n=====================\n\n";
    fs::path dir;
    std::error_code ec;
    for (const fs::path& d : nppSettingsDirs(host, query))
        if (fs::exists(d / "config.xml", ec)) { dir = d; break; }
    if (dir.empty())
    {
        report += "No Notepad++ settings were found.\n\n"
                  "On Windows, an installed Notepad++'s settings are read from %APPDATA%\\Notepad++.\n"
                  "Elsewhere, copy that folder's contents into a folder called notepad++ inside wxNote's\n"
                  "user data folder and run this command again.\n";
        presentReport(host, query, report);
        return;
    }
    report += "From " + utf8Of(dir) + ":\n";
    // Themes first, so config.xml's choice of theme can name the copy just made.
    report += "\nthemes\n";
    const std::string stylers = readFile(dir / "stylers.xml");
    if (!stylers.empty()) importTheme(host, query, stylers, "Notepad++ (imported)", report);
    for (fs::directory_iterator it(dir / "themes", ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".xml")
            importTheme(host, query, readFile(it->path()), utf8Of(it->path().stem()) + " (Notepad++)", report);

    report += "\nconfig.xml\n";
    importConfig(host, query, readFile(dir / "config.xml"), report);

    report += "\nshortcuts.xml\n";
    const std::string shortcuts = readFile(dir / "shortcuts.xml");
    if (shortcuts.empty()) report += "  none\n";
    else
    {
        ImportTally tally;
        NppShortcutsDoc doc;
        if (importShortcuts(host, query, shortcuts, tally, doc))
            report += "  " + std::to_string(tally.imported) + " binding(s) imported as the \"" + NPP_SCHEME_TITLE
                    + "\" scheme - select it in Settings > Shortcut Mapper (" + std::to_string(tally.unmapped)
                    + " key(s) with no equivalent, " + std::to_string(tally.unknown) + " command(s) unknown here;"
                    + " Import Notepad++ shortcuts.xml... shows them)\n";
        else report += "  could not be imported (if wxNote's keybindings.yaml has an error, the status bar says so)\n";
    }

    report += "\ncontextMenu.xml\n";
    const std::string menu = readFile(dir / "contextMenu.xml");
    if (menu.empty()) report += "  none\n";
    else importContextMenu(host, query, menu, report);

    // The active theme holds Notepad++'s "User-defined keywords"; config.xml names it (stylers.xml when not).
    report += "\nlangs.xml\n";
    fs::path themePath = dir / "stylers.xml";
    const std::string named = activeThemeFromConfig(readFile(dir / "config.xml"));
    if (!named.empty())
    {
        themePath = pathFromUtf8(named);
        if (!fs::exists(themePath, ec)) themePath = dir / "themes" / pathFromUtf8(nppFileNameOf(named));   // a copied folder
    }
    const std::string langs = readFile(dir / "langs.xml"), theme = readFile(themePath);
    if (!named.empty() && theme.empty())
        report += "  the active theme, " + nppFileNameOf(named) + ", is not in " + utf8Of(dir / "themes")
                + ", so its user-defined keywords were not imported\n";
    if (langs.empty() && theme.empty()) report += "  none\n";
    else importLanguages(host, query, langs, findLangsModel(dir), theme, report);

    if (fs::exists(dir / "session.xml", ec))
        report += "\nsession.xml\n  Notepad++'s last session is not opened here: open session.xml in wxNote and run\n"
                  "  Import the Open Notepad++ File to open its files.\n";
    presentReport(host, query, report);
}

// Import the Open Notepad++ File: whichever kind of Notepad++ file the active document is.
void importFileCommand(NibHost* host, NibQueryFn query, void*)
{
    std::string report = "Import a Notepad++ file\n=======================\n\n";
    const std::string path = activeDocumentPath(host, query);
    if (path.empty())
    {
        presentReport(host, query, report + "Open a Notepad++ file - a theme, config.xml, shortcuts.xml, contextMenu.xml,\n"
                                            "langs.xml, a session or a workspace - save it, and run this command again.\n");
        return;
    }
    const fs::path source = pathFromUtf8(path);
    const std::string xml = readFile(source);
    const NppFileKind kind = detectNppFile(xml);
    report += utf8Of(source) + " (" + nppFileKindName(kind) + ")\n\n";
    switch (kind)
    {
        case NppFileKind::Theme:       importTheme(host, query, xml, utf8Of(source.stem()) + " (Notepad++)", report); break;
        case NppFileKind::Config:      importConfig(host, query, xml, report); break;
        case NppFileKind::ContextMenu: importContextMenu(host, query, xml, report); break;
        case NppFileKind::Session:     openSession(host, query, xml, report); break;
        case NppFileKind::Workspace:   importWorkspace(xml, source, report); break;
        case NppFileKind::Languages:   importLanguages(host, query, xml, findLangsModel(source.parent_path()), std::string(), report); break;
        case NppFileKind::Shortcuts:
        {
            ImportTally tally;
            NppShortcutsDoc doc;
            report = importShortcuts(host, query, xml, tally, doc)
                   ? formatReport(utf8Of(source), doc, tally)
                   : report + "  could not be imported (if wxNote's keybindings.yaml has an error, the status bar says so)\n";
            break;
        }
        case NppFileKind::Unknown:
            report += "Not a Notepad++ file this can import: expected a theme, config.xml, shortcuts.xml,\n"
                      "contextMenu.xml, langs.xml, a session or a workspace.\n";
            break;
    }
    presentReport(host, query, report);
}

// A command runs inside the host's event loop, and an exception crossing the plugin's C boundary is
// undefined behaviour - so none may leave one. Running out of memory on a hostile file ends in a report.
template <void (*Command)(NibHost*, NibQueryFn, void*)>
void guarded(NibHost* host, NibQueryFn query, void* user)
{
    const char* why = nullptr;
    try { Command(host, query, user); return; }
    catch (const std::bad_alloc&) { why = "not enough memory for that file - is it really one of Notepad++'s?"; }
    catch (...) { why = "an unexpected error"; }
    try { presentReport(host, query, std::string("Notepad++ import\n================\n\nThe import stopped: ") + why + "\n"); }
    catch (...) {}
}

void activate(NibHost* host, NibQueryFn query)
{
    // Imports run only when asked: each writes the user's files (settings.yaml, keybindings.yaml, themes),
    // and doing that unprompted at every load would be both surprising and a race between two instances.
    if (const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1)))
    {
        cmds->register_command(host, IMPORT_CMD_ID, "Import from Notepad++...", guarded<importCommand>, nullptr);
        cmds->register_command(host, IMPORT_FILE_ID, "Import the Open Notepad++ File", guarded<importFileCommand>, nullptr);
        cmds->register_command(host, SHORTCUTS_CMD_ID, "Import Notepad++ shortcuts.xml...", guarded<shortcutsCommand>, nullptr);
    }
}

const NibPluginApi g_api = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnote.npp-compat", activate, nullptr,
    "Notepad++ import", "2.0.0"   // ABI 1.7 display fields; the host reads them via struct_size
};

}  // namespace

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* bs)
{
    if (bs && bs->struct_size >= sizeof(NibBootstrap)) g_log = bs->log;   // stash for the no-panel fallback
    return &g_api;
}
