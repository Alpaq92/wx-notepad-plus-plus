// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-compat - Notepad++'s files, translated into wxNote's (see npp_translate.h).
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).

#include "npp_translate.h"
#include "npp_menu_names.h"      // Notepad++'s English menu item names, which contextMenu.xml can name items by
#include "npp_xml.h"

#include "lang_detect.h"         // wxnLangForNppLexerType: Notepad++ lexer name -> Language-menu name
#include "lang_table.h"          // each Language-menu language's lexer
#include "language_defs.h"       // languages.yaml, and wxNote's keyword lists by name
#include "settings_schema.h"     // the settings a translation may set, and how each is spelled
#include "yaml_io.h"

// LangType - the language numbers config.xml stores. Last, and without min/max: on Windows it brings in
// <windows.h>, whose macros would otherwise rewrite std::min and friends in everything after it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Notepad_plus_msgs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nppcompat {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool yes(const std::string& v)
{
    const std::string l = lower(v);
    return l == "yes" || l == "show" || l == "true" || l == "1";
}

// A whole decimal number within [lo, hi], or false.
bool number(const std::string& v, long lo, long hi, long& out)
{
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (!end || *end != '\0' || n < lo || n > hi) return false;
    out = n;
    return true;
}

// "RRGGBB" (Notepad++ writes no '#') -> 0xRRGGBB, or -1 when empty or not a colour.
int nppColor(const std::string& v)
{
    unsigned rgb = 0;
    return wxnyaml::parseColor(v, rgb) ? static_cast<int>(rgb) : -1;
}

// An XML comment's text as YAML comment lines: common indentation removed, blank edges dropped.
std::string commentAsYaml(const std::string& comment)
{
    std::vector<std::string> lines;
    size_t from = 0;
    while (from <= comment.size())
    {
        size_t nl = comment.find('\n', from);
        if (nl == std::string::npos) nl = comment.size();
        std::string line = comment.substr(from, nl - from);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        lines.push_back(line);
        from = nl + 1;
    }
    // Leading blank lines dropped in one erase: one at a time is quadratic, and a hostile file can hold
    // millions of them.
    const auto firstText = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return !l.empty(); });
    lines.erase(lines.begin(), firstText);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    size_t indent = std::string::npos;
    for (const std::string& l : lines)
        if (!l.empty()) indent = std::min(indent, l.find_first_not_of(" \t"));
    std::string out;
    for (const std::string& l : lines)
        out += l.empty() ? "#\n" : "# " + l.substr(indent == std::string::npos ? 0 : indent) + "\n";
    return out;
}

wxntheme::Style styleFrom(const XmlElement& e, bool withId)
{
    wxntheme::Style s;
    if (withId)
    {
        long id = -1;
        s.id = number(e.attr("styleID"), 0, 255, id) ? static_cast<int>(id) : -1;
    }
    s.name = e.attr("name");
    s.fg = nppColor(e.attr("fgColor"));
    s.bg = nppColor(e.attr("bgColor"));
    long v = 0;
    // Only the bold (1), italic (2) and underline (4) bits mean anything; some themes carry others
    // (Perl's "10" is italic plus an unused 8), so keep those three and drop the rest.
    if (number(e.attr("fontStyle"), 0, 0xFFFF, v)) s.fontStyle = static_cast<int>(v) & 7;
    s.font = e.attr("fontName");
    if (number(e.attr("fontSize"), 1, 400, v)) s.size = static_cast<int>(v);
    if (number(e.attr("fontWeight"), 1, 1000, v)) s.weight = static_cast<int>(v);   // wxNote's own extension
    return s;
}

// LangType -> Notepad++'s lexer name (the LexerType key of its themes), which lang_detect.h maps on to
// a Language-menu name.
struct LangTypeRow { int type; const char* lexer; };
const LangTypeRow kLangTypes[] = {
    { L_PHP, "php" }, { L_C, "c" }, { L_CPP, "cpp" }, { L_CS, "cs" }, { L_OBJC, "objc" }, { L_JAVA, "java" },
    { L_RC, "rc" }, { L_HTML, "html" }, { L_XML, "xml" }, { L_MAKEFILE, "makefile" }, { L_PASCAL, "pascal" },
    { L_BATCH, "batch" }, { L_ASP, "asp" }, { L_SQL, "sql" }, { L_VB, "vb" }, { L_CSS, "css" }, { L_PERL, "perl" },
    { L_PYTHON, "python" }, { L_LUA, "lua" }, { L_TEX, "tex" }, { L_FORTRAN, "fortran" }, { L_BASH, "bash" },
    { L_FLASH, "actionscript" }, { L_NSIS, "nsis" }, { L_TCL, "tcl" }, { L_LISP, "lisp" }, { L_SCHEME, "scheme" },
    { L_ASM, "asm" }, { L_DIFF, "diff" }, { L_PROPS, "props" }, { L_PS, "postscript" }, { L_RUBY, "ruby" },
    { L_SMALLTALK, "smalltalk" }, { L_VHDL, "vhdl" }, { L_KIX, "kix" }, { L_AU3, "autoit" }, { L_CAML, "caml" },
    { L_ADA, "ada" }, { L_VERILOG, "verilog" }, { L_MATLAB, "matlab" }, { L_HASKELL, "haskell" },
    { L_INNO, "inno" }, { L_CMAKE, "cmake" }, { L_YAML, "yaml" }, { L_COBOL, "cobol" }, { L_GUI4CLI, "gui4cli" },
    { L_D, "d" }, { L_POWERSHELL, "powershell" }, { L_R, "r" }, { L_COFFEESCRIPT, "coffeescript" },
    { L_JSON, "json" }, { L_JAVASCRIPT, "javascript.js" }, { L_FORTRAN_77, "fortran77" }, { L_BAANC, "baanc" },
    { L_SREC, "srec" }, { L_IHEX, "ihex" }, { L_TEHEX, "tehex" }, { L_SWIFT, "swift" }, { L_ASN1, "asn1" },
    { L_AVS, "avs" }, { L_BLITZBASIC, "blitzbasic" }, { L_PUREBASIC, "purebasic" }, { L_FREEBASIC, "freebasic" },
    { L_CSOUND, "csound" }, { L_ERLANG, "erlang" }, { L_ESCRIPT, "escript" }, { L_FORTH, "forth" },
    { L_LATEX, "latex" }, { L_MMIXAL, "mmixal" }, { L_NIM, "nim" }, { L_NNCRONTAB, "nncrontab" },
    { L_OSCRIPT, "oscript" }, { L_REBOL, "rebol" }, { L_REGISTRY, "registry" }, { L_RUST, "rust" },
    { L_SPICE, "spice" }, { L_TXT2TAGS, "txt2tags" }, { L_VISUALPROLOG, "visualprolog" },
    { L_TYPESCRIPT, "typescript" }, { L_JSON5, "json5" }, { L_MSSQL, "mssql" }, { L_GDSCRIPT, "gdscript" },
    { L_HOLLYWOOD, "hollywood" }, { L_GOLANG, "go" }, { L_RAKU, "raku" }, { L_TOML, "toml" }, { L_SAS, "sas" },
};

std::string languageForLangType(long type)
{
    for (const LangTypeRow& r : kLangTypes)
        if (r.type == type) return wxnLangForNppLexerType(r.lexer);
    return std::string();
}

}   // namespace

// ------------------------------------------------------------------------------------------------

NppFileKind detectNppFile(const std::string& xml)
{
    XmlElement root;
    if (!parseXml(xml, root)) return NppFileKind::Unknown;
    for (const XmlElement& c : root.children)
    {
        if (c.name == "LexerStyles" || c.name == "GlobalStyles") return NppFileKind::Theme;
        if (c.name == "GUIConfigs") return NppFileKind::Config;
        if (c.name == "InternalCommands" || c.name == "Macros" || c.name == "UserDefinedCommands"
            || c.name == "PluginCommands" || c.name == "ScintillaKeys") return NppFileKind::Shortcuts;
        if (c.name == "ScintillaContextMenu") return NppFileKind::ContextMenu;
        if (c.name == "Session") return NppFileKind::Session;
        if (c.name == "Project") return NppFileKind::Workspace;
        if (c.name == "Languages") return NppFileKind::Languages;
    }
    return NppFileKind::Unknown;
}

const char* nppFileKindName(NppFileKind k)
{
    switch (k)
    {
        case NppFileKind::Theme:       return "theme";
        case NppFileKind::Config:      return "config.xml";
        case NppFileKind::Shortcuts:   return "shortcuts.xml";
        case NppFileKind::ContextMenu: return "contextMenu.xml";
        case NppFileKind::Session:     return "session";
        case NppFileKind::Workspace:   return "workspace";
        case NppFileKind::Languages:   return "langs.xml";
        default:                       return "unknown";
    }
}

bool themeFromNpp(const std::string& xml, wxntheme::Theme& out, std::string* err)
{
    out = wxntheme::Theme();
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    const std::string comment = leadingXmlComment(xml);
    if (!comment.empty()) out.header = commentAsYaml(comment);
    if (const XmlElement* globals = root.child("GlobalStyles"))
        for (const XmlElement& w : globals->children)
            if (w.name == "WidgetStyle" && !w.attr("name").empty())
            {
                wxntheme::Style s = styleFrom(w, false);
                s.fontStyle = 0;   // a widget's font style is not used: Default Style's face and size are
                out.globals.push_back(s);
            }
    if (const XmlElement* lexers = root.child("LexerStyles"))
        for (const XmlElement& lt : lexers->children)
        {
            if (lt.name != "LexerType" || lt.attr("name").empty()) continue;
            wxntheme::Lexer l;
            l.name = lt.attr("name");
            l.description = lt.attr("desc");
            l.extensions = lt.attr("ext");
            for (const XmlElement& ws : lt.children)
            {
                if (ws.name != "WordsStyle") continue;
                const wxntheme::Style s = styleFrom(ws, true);
                if (s.id >= 0) l.styles.push_back(s);
            }
            out.lexers.push_back(std::move(l));
        }
    if (out.globals.empty() && out.lexers.empty())
    {
        if (err) *err = "no LexerStyles or GlobalStyles: not a Notepad++ theme";
        return false;
    }
    return true;
}

bool settingsFromConfig(const std::string& xml, ConfigTranslation& out, std::string* err)
{
    out = ConfigTranslation();
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    const XmlElement* guis = root.child("GUIConfigs");
    if (!guis)
    {
        if (err) *err = "no GUIConfigs: not a Notepad++ config.xml";
        return false;
    }
    auto set = [&out](const char* id, const std::string& value) { out.settings.emplace_back(id, value); };
    auto setBool = [&set](const char* id, bool v) { set(id, v ? "true" : "false"); };
    auto skip = [&out](const std::string& what) { out.notTranslated.push_back(what); };

    for (const XmlElement& g : guis->children)
    {
        if (g.name != "GUIConfig") continue;
        const std::string name = g.attr("name");
        long v = 0;
        if (name == "ToolBar")
        {
            if (g.hasAttr("visible")) setBool("ui.toolbar.visible", yes(g.attr("visible")));
        }
        else if (name == "StatusBar") setBool("ui.statusBar.visible", lower(g.text).find("hide") == std::string::npos);
        else if (name == "TabBar")
        {
            if (g.hasAttr("closeButton")) setBool("ui.tabs.closeButton", yes(g.attr("closeButton")));
        }
        else if (name == "ScintillaPrimaryView")
        {
            if (g.hasAttr("lineNumberMargin")) setBool("editor.lineNumbers", yes(g.attr("lineNumberMargin")));
            if (g.hasAttr("indentGuideLine")) setBool("editor.indentGuides", yes(g.attr("indentGuideLine")));
            if (g.hasAttr("Wrap")) setBool("editor.wordWrap", yes(g.attr("Wrap")));
            if (g.hasAttr("wrapSymbolShow")) setBool("editor.wrapSymbol", yes(g.attr("wrapSymbolShow")));
            if (g.hasAttr("whiteSpaceShow")) setBool("editor.showWhitespace", yes(g.attr("whiteSpaceShow")));
            if (g.hasAttr("scrollBeyondLastLine")) setBool("editor.scrollBeyondLastLine", yes(g.attr("scrollBeyondLastLine")));
            if (g.hasAttr("multiSelection")) setBool("editor.multiCursor", yes(g.attr("multiSelection")));
            if (g.hasAttr("currentLineIndicator"))                         // 0 none, 1 background, 2 frame
                setBool("editor.highlightCurrentLine", g.attr("currentLineIndicator") != "0");
            else if (g.hasAttr("currentLineHilitingShow"))                 // before Notepad++ 8.4
                setBool("editor.highlightCurrentLine", yes(g.attr("currentLineHilitingShow")));
            const std::string edges = g.attr("edgeMultiColumnPos");
            if (number(edges.substr(0, edges.find(' ')), 1, 300, v)) set("editor.edgeColumn", std::to_string(v));
            if (g.hasAttr("zoom")) skip("zoom (wxNote keeps zoom per window, not as a setting)");
        }
        else if (name == "ScintillaGlobalSettings")
        {
            if (g.hasAttr("enableMultiSelection")) setBool("editor.multiCursor", yes(g.attr("enableMultiSelection")));
        }
        else if (name == "Caret")
        {
            if (number(g.attr("width"), 1, 3, v)) set("editor.caretWidth", std::to_string(v));
            if (number(g.attr("blinkRate"), 0, 2000, v)) set("editor.caretBlinkMs", std::to_string(v));
        }
        else if (name == "TabSetting")
        {
            if (number(g.attr("size"), 1, 16, v)) set("editor.tabSize", std::to_string(v));
            if (g.hasAttr("replaceBySpace")) setBool("editor.useTabs", !yes(g.attr("replaceBySpace")));
        }
        else if (name == "NewDocDefaultSettings")
        {
            static const char* const eol[] = { "crlf", "cr", "lf" };           // Windows, Mac, Unix
            if (number(g.attr("format"), 0, 2, v)) set("files.newDocument.eol", eol[v]);
            // UniMode: 0 ANSI, 1 UTF-8 with BOM, 2 UTF-16 BE, 3 UTF-16 LE, 4 UTF-8 ("cookie": no BOM)
            static const char* const enc[] = { "ansi", "utf-8-bom", "utf-16be", "utf-16le", "utf-8" };
            if (number(g.attr("encoding"), 0, 4, v)) set("files.newDocument.encoding", enc[v]);
            else if (g.hasAttr("encoding")) skip("new document encoding " + g.attr("encoding"));
            if (number(g.attr("lang"), 0, 1000, v))
            {
                const std::string lang = v == 0 ? std::string() : languageForLangType(v);
                if (v == 0 || !lang.empty()) set("files.newDocument.language", lang);
                else skip("new document language " + g.attr("lang") + " (no wxNote equivalent)");
            }
        }
        else if (name == "auto-completion")
        {
            if (number(g.attr("autoCAction"), 0, 3, v)) setBool("editor.autoComplete.enabled", v != 0);
            if (number(g.attr("triggerFromNbChar"), 1, 10, v)) set("editor.autoComplete.minChars", std::to_string(v));
        }
        else if (name == "auto-insert")
        {
            bool any = false;
            for (const char* a : { "parentheses", "brackets", "curlyBrackets", "quotes", "doubleQuotes" }) any = any || yes(g.attr(a));
            setBool("editor.autoClosePairs", any);
            if (yes(g.attr("htmlXmlTag"))) skip("auto-insert of closing HTML/XML tags");
        }
        else if (name == "MultiInstance")
        {
            // 0 one instance, 1 a new instance per session, 2 always a new instance
            if (number(g.attr("setting"), 0, 2, v)) setBool("window.reuseInstance", v == 0);
        }
        else if (name == "SearchEngine")
        {
            // 0 custom, 1 DuckDuckGo, 2 Google, 3 Bing, 4 Yahoo, 5 Stack Overflow
            static const char* const engines[] = { nullptr, "duckduckgo", "google", "bing", "yahoo", nullptr };
            if (number(g.attr("searchEngineChoice"), 0, 5, v) && engines[v]) set("search.webEngine", engines[v]);
            else if (g.hasAttr("searchEngineChoice")) skip("search engine (custom or Stack Overflow)");
        }
        else if (name == "DarkMode")
        {
            if (yes(g.attr("enable"))) set("ui.themeMode", "dark");
        }
        else if (name == "stylerTheme")
        {
            std::string path = g.attr("path");
            const size_t slash = path.find_last_of("/\\");
            if (slash != std::string::npos) path = path.substr(slash + 1);
            const size_t dot = path.rfind('.');
            if (dot != std::string::npos) path = path.substr(0, dot);
            if (!path.empty() && lower(path) != "stylers") set("ui.colorTheme", path);
        }
        else if (name == "Print")
        {
            const std::string h = g.attr("headerLeft") + g.attr("headerMiddle") + g.attr("headerRight");
            const std::string f = g.attr("footerLeft") + g.attr("footerMiddle") + g.attr("footerRight");
            if (!h.empty() || !f.empty()) skip("print header and footer (Notepad++ has three parts, wxNote one)");
        }
    }
    if (const XmlElement* history = root.child("History"))
    {
        long v = 0;
        if (number(history->attr("nbMaxFile"), 1, 50, v)) out.settings.emplace_back("files.maxRecentFiles", std::to_string(v));
    }

    // Keep only what the schema accepts, in the spelling it reads: a later Notepad++ could write a
    // value this table maps to nothing sensible, and the host would refuse it anyway.
    std::vector<std::pair<std::string, std::string>> kept;
    for (const auto& kv : out.settings)
        if (wxnsettings::findDef(kv.first)) kept.push_back(kv);
    out.settings.swap(kept);
    return true;
}

namespace {

// A menu label as Notepad++ compares it: no '&' accelerator marks, no "\t<shortcut>" or "...", any case.
std::string menuName(const std::string& s)
{
    std::string out;
    for (char c : s.substr(0, s.find('\t')))
        if (c != '&') out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    return out;
}

// The command an item names by its English menu entry and item names, found as Notepad++ finds it: the
// first item of that name in menu order (npp_menu_names.h keeps that order) - within the named top-level
// menu when the name occurs under more than one. Notepad++'s command numbers are wxNote's. 0: none.
int namedMenuCommand(const std::string& entry, const std::string& item)
{
    const std::string want = menuName(item);
    if (want.empty()) return 0;
    static const struct { const char* entry; int lo, hi; } kEntries[] = {
        { "file", 41000, 41999 }, { "edit", 42000, 42999 }, { "search", 43000, 43999 }, { "view", 44000, 44999 },
        { "encoding", 45000, 45999 }, { "language", 46000, 46999 }, { "?", 47000, 47999 },
        { "settings", 48000, 48499 }, { "tools", 48500, 48999 }, { "macro", 42000, 42999 }, { "run", 49000, 49999 },
    };
    const std::string e = menuName(entry);
    int lo = 0, hi = 0x7FFFFFFF;                       // an entry name this does not know: any menu
    for (const auto& r : kEntries) if (e == r.entry) { lo = r.lo; hi = r.hi; break; }
    int elsewhere = 0;
    for (const NppMenuName& n : kNppMenuNames)
        if (menuName(n.name) == want)
        {
            if (n.id >= lo && n.id <= hi) return n.id;
            if (!elsewhere) elsewhere = n.id;
        }
    return elsewhere;                                  // the name, under another menu than the one given
}

// A submenu's label. Notepad++'s own folders carry a TranslateID; where wxNote has the same submenu, its
// label is used instead - which wxNote shows in the user's language. A folder the user renamed keeps its name.
std::string folderLabel(const XmlElement& it)
{
    static const struct { const char* id; const char* english; const char* wxnote; } kFolders[] = {
        { "contextMenu-styleAlloccurrencesOfToken", "style all occurrences of token", "Style &All Occurrences of Token" },
        { "contextMenu-styleOneToken", "style one token", "Style &One Token" },
        { "contextMenu-clearStyle", "clear style", "Clear Style" },
    };
    const std::string name = it.attr("FolderName");
    for (const auto& f : kFolders)
        if (it.attr("TranslateID") == f.id && menuName(name) == f.english) return f.wxnote;
    return name;
}

}   // namespace

bool contextMenuFromNpp(const std::string& xml, std::string& yaml, std::vector<std::string>& notTranslated, std::string* err)
{
    yaml.clear();
    notTranslated.clear();
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    const XmlElement* menu = root.child("ScintillaContextMenu");
    if (!menu)
    {
        if (err) *err = "no ScintillaContextMenu: not a Notepad++ contextMenu.xml";
        return false;
    }
    ryml::Tree t;
    wxnyaml::MutNode r = wxnyaml::resetToMap(t);
    wxnyaml::MutNode items = wxnyaml::addSeq(r, "items");
    // Consecutive items with the same FolderName form one submenu, as in Notepad++; the list being filled
    // is the current folder's while it lasts, the top level otherwise.
    std::string folder;
    wxnyaml::MutNode into = items;
    for (const XmlElement& it : menu->children)
    {
        if (it.name != "Item") continue;
        const std::string itemFolder = it.attr("FolderName");
        if (itemFolder != folder)
        {
            folder = itemFolder;
            into = items;
            if (!folder.empty())
            {
                wxnyaml::MutNode sub = wxnyaml::addMapItem(items);
                wxnyaml::setText(wxnyaml::addKey(sub, "menu"), folderLabel(it));
                into = wxnyaml::addSeq(sub, "items");
            }
        }
        if (lower(it.attr("type")) == "separator" || it.attr("id") == "0") { wxnyaml::setText(wxnyaml::addItem(into), "-"); continue; }
        const std::string label = it.attr("ItemNameAs");
        long id = -1;
        if (!number(it.attr("id"), 1, 0x7FFFFFFF, id))
            id = namedMenuCommand(it.attr("MenuEntryName"), it.attr("MenuItemName"));
        if (id > 0)
        {
            if (label.empty()) { wxnyaml::setInteger(wxnyaml::addItem(into), id); continue; }
            wxnyaml::MutNode e = wxnyaml::addMapItem(into);
            wxnyaml::setOneLine(e);
            wxnyaml::setInteger(wxnyaml::addKey(e, "command"), id);
            wxnyaml::setText(wxnyaml::addKey(e, "label"), label);
            continue;
        }
        // A plugin's command: by the plugin's menu name and the command's, as Notepad++ lists it - which is
        // how wxNote finds it too, among the commands its plugins (the npp-bridge's included) registered.
        if (!it.attr("PluginEntryName").empty() && !it.attr("PluginCommandItemName").empty())
        {
            wxnyaml::MutNode e = wxnyaml::addMapItem(into);
            wxnyaml::setOneLine(e);
            wxnyaml::setText(wxnyaml::addKey(e, "plugin"), it.attr("PluginEntryName"));
            wxnyaml::setText(wxnyaml::addKey(e, "command"), it.attr("PluginCommandItemName"));
            if (!label.empty()) wxnyaml::setText(wxnyaml::addKey(e, "label"), label);
            continue;
        }
        if (!it.attr("MenuItemName").empty())
            notTranslated.push_back("\"" + it.attr("MenuEntryName") + " > " + it.attr("MenuItemName") + "\" (no Notepad++ menu item of that name)");
    }
    std::string body;
    if (!wxnyaml::emit(t, body)) { if (err) *err = "could not write the menu"; return false; }
    yaml = "# The editor's right-click menu, top to bottom (see the shipped contextmenu.yaml for the format).\n"
           "# Imported from Notepad++'s contextMenu.xml: numbers are its command numbers, which are wxNote's.\n" + body;
    return true;
}

namespace {

bool absolutePath(const std::string& p)
{
    return (!p.empty() && (p[0] == '/' || p[0] == '\\')) || (p.size() > 1 && p[1] == ':');
}

void addProjectItems(const XmlElement& from, wxnyaml::MutNode list, const std::string& baseDir)
{
    for (const XmlElement& c : from.children)
    {
        if (c.name == "Folder")
        {
            wxnyaml::MutNode f = wxnyaml::addMapItem(list);
            wxnyaml::setText(wxnyaml::addKey(f, "folder"), c.attr("name", "Folder"));
            addProjectItems(c, wxnyaml::addSeq(f, "items"), baseDir);
        }
        else if (c.name == "File" && !c.attr("name").empty())
        {
            std::string path = c.attr("name");
            if (!absolutePath(path) && !baseDir.empty()) path = baseDir + "/" + path;   // Notepad++ stores them relative
            wxnyaml::MutNode f = wxnyaml::addMapItem(list);
            wxnyaml::setText(wxnyaml::addKey(f, "file"), path);
        }
    }
}

}   // namespace

bool workspaceFromNpp(const std::string& xml, const std::string& baseDir, std::string& yaml, std::string* err)
{
    yaml.clear();
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    std::vector<const XmlElement*> projects;
    for (const XmlElement& c : root.children)
        if (c.name == "Project") projects.push_back(&c);
    if (projects.empty())
    {
        if (err) *err = "no Project: not a Notepad++ workspace";
        return false;
    }
    ryml::Tree t;
    wxnyaml::MutNode r = wxnyaml::resetToMap(t);
    wxnyaml::setText(wxnyaml::addKey(r, "name"), projects[0]->attr("name", "Workspace"));
    wxnyaml::MutNode items = wxnyaml::addSeq(r, "items");
    addProjectItems(*projects[0], items, baseDir);
    for (size_t i = 1; i < projects.size(); ++i)          // wxNote's workspace holds one project: the rest
    {                                                    // come in as top-level folders
        wxnyaml::MutNode f = wxnyaml::addMapItem(items);
        wxnyaml::setText(wxnyaml::addKey(f, "folder"), projects[i]->attr("name", "Project"));
        addProjectItems(*projects[i], wxnyaml::addSeq(f, "items"), baseDir);
    }
    std::string body;
    if (!wxnyaml::emit(t, body)) { if (err) *err = "could not write the workspace"; return false; }
    yaml = "# A wxNote workspace, imported from a Notepad++ project file.\n" + body;
    return true;
}

std::string activeThemeFromConfig(const std::string& xml)
{
    XmlElement root;
    if (!parseXml(xml, root)) return std::string();
    if (const XmlElement* gui = root.child("GUIConfigs"))
        for (const XmlElement& g : gui->children)
            if (g.name == "GUIConfig" && g.attr("name") == "stylerTheme")
            {
                const std::string path = g.attr("path");
                return lower(nppFileNameOf(path)) == "stylers.xml" ? std::string() : path;
            }
    return std::string();
}

std::string nppFileNameOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---- langs.xml -------------------------------------------------------------------------------------

namespace {

// One <Language> of langs.xml (or langs.model.xml).
struct NppLanguage
{
    std::string name;
    std::string ext;
    bool hasLine = false;
    std::string line, start, end;
    std::map<std::string, std::string> keywords;   // keyword class ("instre1", "type2", "substyle3") -> words
    std::string tabSettings;
};

bool readNppLanguages(const std::string& xml, std::vector<NppLanguage>& out, std::string* err)
{
    XmlElement root;
    if (!parseXml(xml, root, err)) return false;
    const XmlElement* langs = root.child("Languages");
    if (!langs) { if (err) *err = "no <Languages>: not a Notepad++ langs.xml"; return false; }
    for (const XmlElement& l : langs->children)
    {
        if (l.name != "Language" || l.attr("name").empty()) continue;
        NppLanguage n;
        n.name = l.attr("name");
        n.ext = l.attr("ext");
        n.hasLine = l.hasAttr("commentLine");
        n.line = l.attr("commentLine");
        n.start = l.attr("commentStart");
        n.end = l.attr("commentEnd");
        n.tabSettings = l.attr("tabSettings");
        for (const XmlElement& k : l.children)
            if (k.name == "Keywords" && !k.attr("name").empty()) n.keywords[k.attr("name")] = k.text;
        out.push_back(std::move(n));
    }
    return true;
}

// Notepad++'s keyword class as its index: instre1 0, instre2 1, type1-type7 2-8 (or the bare digits),
// substyle1-substyle8 100-107; -1 for anything else.
int keywordClassIndex(const std::string& c)
{
    if (c == "instre1") return 0;
    if (c == "instre2") return 1;
    if (c.size() == 5 && c.compare(0, 4, "type") == 0 && c[4] >= '1' && c[4] <= '7') return 2 + (c[4] - '1');
    if (c.size() == 9 && c.compare(0, 8, "substyle") == 0 && c[8] >= '1' && c[8] <= '8') return 100 + (c[8] - '1');
    if (c.size() == 1 && c[0] >= '0' && c[0] <= '8') return c[0] - '0';
    return -1;
}

const char* const kCFamily[] = { "c", "cpp", "cs", "objc", "java", "rc", "javascript.js", "actionscript", "swift",
                                 "typescript", "go" };
const char* const kHtmlFamily[] = { "HTML", "PHP", "ASP", "JSP" };

std::string lexerOfLanguage(const std::string& lang)
{
    const WxnLang* l = wxnLangFindByName(lang);
    return l ? std::string(l->lexer) : std::string();
}

// The wxNote language a Notepad++ <Language> is: "" for the panes and overlays that are none, and for the
// embedded-script and markup blocks the HTML family shares (they have no file extensions of their own).
std::string wxnoteLanguage(const std::string& npp)
{
    if (npp == "ini") return "Properties";   // wxNote opens .ini files as Properties
    const std::string l = wxnLangForNppLexerType(npp);
    return wxnLangFindByName(l) ? l : std::string();
}

// Where Notepad++'s keyword class `cls` of language `npp` lands in wxNote: (language, list) pairs, none
// when wxNote has no such list. See languagesFromNpp in the header for the wiring.
std::vector<std::pair<std::string, std::string>> wxnoteListsFor(const std::string& npp, const std::string& cls)
{
    std::vector<std::pair<std::string, std::string>> out;
    const int idx = keywordClassIndex(cls);
    if (idx < 0) return out;
    const bool group = idx >= 100;
    const int n = group ? idx - 100 : idx;
    auto slot = [&](const std::string& lang, int s) {
        for (const WxnKeywordSetName* k : wxnKeywordSetsOf(lexerOfLanguage(lang)))
            if (k->slot == s) out.emplace_back(lang, k->name);
    };
    auto nthGroup = [&](const std::string& lang, int i) {
        const std::vector<WxnSubstyleGroup> g = wxnSubstyleGroupsOf(lexerOfLanguage(lang), lang);
        if (i >= 0 && i < static_cast<int>(g.size())) out.emplace_back(lang, g[i].name);
    };
    if (npp == "html" || npp == "javascript" || npp == "php" || npp == "asp")
    {
        // One lexer for the four, each list set for all of them; the groups after the markup's eight
        // (tags, attributes) are the language's own script's.
        for (const char* l : kHtmlFamily)
        {
            const std::string lang = l;
            if (group)
            {
                if (npp == "html") nthGroup(lang, n);
                else if ((npp == "javascript" && (lang == "HTML" || lang == "JSP")) || (npp == "php" && lang == "PHP")
                         || (npp == "asp" && lang == "ASP"))
                    nthGroup(lang, 8 + n);
            }
            else if (npp == "html" && n == 0) slot(lang, 0);
            else if (npp == "html" && n == 1) slot(lang, 5);
            else if (npp == "javascript" && n == 0) slot(lang, 1);
            else if (npp == "asp" && n == 0) slot(lang, 2);
            else if (npp == "php" && n == 0) slot(lang, 4);
        }
        return out;
    }
    if (npp == "xml") { if (group) nthGroup("XML", n); else if (n == 0) slot("XML", 5); return out; }
    const std::string lang = wxnoteLanguage(npp);
    if (lang.empty()) return out;
    if (group) { nthGroup(lang, n); return out; }
    if (std::find_if(std::begin(kCFamily), std::end(kCFamily), [&](const char* c) { return npp == c; }) != std::end(kCFamily))
    {
        if (n == 0) slot(lang, 0);
        else if (n == 2) slot(lang, 1);
        else if (n == 1) slot(lang, 3);
        else if (n == 3 && npp == "cpp")   // the doxygen words, which Notepad++ gives every C-family language but Go
            for (const char* c : kCFamily)
                if (std::string(c) != "go")
                    if (const std::string l = wxnoteLanguage(c); !l.empty()) slot(l, 2);
        return out;
    }
    slot(lang, n);
    return out;
}

std::vector<std::string> splitWords(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s + " ")
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    return out;
}

// `a` less `b`, in a's order, each once.
std::vector<std::string> without(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    const std::set<std::string> drop(b.begin(), b.end());
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const std::string& w : a) if (!drop.count(w) && seen.insert(w).second) out.push_back(w);
    return out;
}

}   // namespace

bool languagesFromNpp(const std::string& langsXml, const std::string& modelXml, const std::string& themeXml,
                      LanguagesTranslation& out, std::string* err)
{
    out = LanguagesTranslation();
    std::vector<NppLanguage> user, model;
    if (!langsXml.empty() && !readNppLanguages(langsXml, user, err)) return false;
    if (!modelXml.empty())
    {
        std::string why;
        if (!readNppLanguages(modelXml, model, &why))
        {
            out.notTranslated.push_back("langs.model.xml could not be read (" + why + "), so langs.xml was not compared");
            model.clear();
        }
    }
    out.compared = !user.empty() && !model.empty();

    // What each wxNote language gets, in the order Notepad++ lists them: words and extensions it adds.
    using Words = std::vector<std::string>;
    struct Entry
    {
        Words ext;
        bool line = false, block = false;
        std::string lineToken, open, close;
        std::vector<std::pair<std::string, Words>> lists;   // in the order they came
    };
    std::vector<std::pair<std::string, Entry>> entries;
    auto entryOf = [&](const std::string& lang) -> Entry& {
        for (auto& e : entries) if (e.first == lang) return e.second;
        entries.emplace_back(lang, Entry());
        return entries.back().second;
    };
    auto addTo = [&](const std::string& lang, const std::string& list, const Words& add) {
        Entry& e = entryOf(lang);
        auto it = std::find_if(e.lists.begin(), e.lists.end(), [&](const auto& l) { return l.first == list; });
        if (it == e.lists.end()) { e.lists.emplace_back(list, Words()); it = e.lists.end() - 1; }
        for (const std::string& w : add) if (std::find(it->second.begin(), it->second.end(), w) == it->second.end()) it->second.push_back(w);
    };
    auto skipped = [](const std::string& npp) {
        return npp == "normal" || npp == "searchResult" || npp == "errorlist" || npp == "escseq" || npp == "nfo";
    };

    if (out.compared)
        for (const NppLanguage& u : user)
        {
            if (skipped(u.name)) continue;
            const auto mi = std::find_if(model.begin(), model.end(), [&](const NppLanguage& m) { return m.name == u.name; });
            if (mi == model.end())
            {
                // Another Notepad++ version's language: nothing in it can be told from that version's own.
                out.notTranslated.push_back("Notepad++'s " + u.name + ": not in langs.model.xml, so what you changed in it cannot "
                                            "be told from Notepad++'s own - not imported");
                continue;
            }
            const NppLanguage* m = &*mi;
            const std::string lang = wxnoteLanguage(u.name);
            // Extensions, as wxNote spells them.
            auto exts = [](const std::string& s) {
                std::vector<std::string> v;
                for (const std::string& w : splitWords(s)) if (const std::string e = wxnUserExtNormalize(w); !e.empty()) v.push_back(e);
                return v;
            };
            // Only what the user ADDED: what langs.xml lacks against the model is as likely a Notepad++
            // update the copy has not caught up with, and Notepad++ itself puts such entries back when it
            // updates - so a removal never lasts there either. It is counted, not imported.
            const std::vector<std::string> ue = exts(u.ext), me = exts(m->ext);
            const std::vector<std::string> extAdd = without(ue, me);
            size_t lacking = without(me, ue).size();
            if (!extAdd.empty())
            {
                if (lang.empty()) out.notTranslated.push_back("Notepad++'s " + u.name + " extensions: wxNote has no such language");
                else   // ini and props are both Properties: what each adds
                {
                    Words& x = entryOf(lang).ext;
                    for (const std::string& e : extAdd) if (std::find(x.begin(), x.end(), e) == x.end()) x.push_back(e);
                }
            }
            // Comment tokens. Notepad++'s ini and wxNote's Properties comment differently by design (.ini
            // takes ';' in wxNote too, from its own table), so an ini change is left to the user.
            const bool lineChanged = u.line != m->line || u.hasLine != m->hasLine;
            const bool blockChanged = u.start != m->start || u.end != m->end;
            if (lineChanged || blockChanged)
            {
                if (lang.empty() || u.name == "ini")
                    out.notTranslated.push_back("Notepad++'s " + u.name + " comment tokens: set them in languages.yaml by hand");
                else
                {
                    Entry& e = entryOf(lang);
                    if (lineChanged) { e.line = true; e.lineToken = u.line; }
                    if (blockChanged) { e.block = true; e.open = u.start; e.close = u.end; }
                }
            }
            // Keyword lists: what the user added and took away.
            std::set<std::string> classes;
            for (const auto& k : u.keywords) classes.insert(k.first);
            for (const auto& k : m->keywords) classes.insert(k.first);
            for (const std::string& cls : classes)
            {
                const auto uw = u.keywords.count(cls) ? splitWords(u.keywords.at(cls)) : std::vector<std::string>();
                const auto mw = m->keywords.count(cls) ? splitWords(m->keywords.at(cls)) : std::vector<std::string>();
                const std::vector<std::string> add = without(uw, mw);
                lacking += without(mw, uw).size();
                if (add.empty()) continue;
                const auto targets = wxnoteListsFor(u.name, cls);
                if (targets.empty())
                    out.notTranslated.push_back("Notepad++'s " + u.name + " " + cls + " keywords (" + std::to_string(add.size())
                                                + " added): wxNote has no such list");
                for (const auto& t : targets) addTo(t.first, t.second, add);
            }
            if (lacking)
                out.notTranslated.push_back("Notepad++'s " + u.name + ": " + std::to_string(lacking) + " extension(s) or keyword(s) of "
                                            "Notepad++'s own are missing from langs.xml - Notepad++ restores those when it updates, "
                                            "so they were not taken away here");
            if (u.tabSettings != m->tabSettings && !u.tabSettings.empty())
                out.notTranslated.push_back((lang.empty() ? u.name : lang) + "'s indentation (tabSettings " + u.tabSettings
                                            + "): set editor.tabSize and editor.useTabs for it under languages: in settings.yaml");
        }
    else if (!user.empty())
        out.notTranslated.push_back("langs.xml: without Notepad++'s langs.model.xml (in the Notepad++ program folder) the changes "
                                    "you made cannot be told from Notepad++'s own lists, so none were imported - put a copy "
                                    "beside langs.xml and import again");

    // The theme's "User-defined keywords": additions to the class of the style they sit on.
    if (!themeXml.empty())
    {
        XmlElement root;
        if (parseXml(themeXml, root))
            if (const XmlElement* lexers = root.child("LexerStyles"))
                for (const XmlElement& lt : lexers->children)
                    for (const XmlElement& ws : lt.children)
                    {
                        const std::vector<std::string> words = splitWords(ws.text);
                        if (ws.name != "WordsStyle" || words.empty()) continue;
                        const auto targets = wxnoteListsFor(lt.attr("name"), ws.attr("keywordClass"));
                        if (targets.empty())
                            out.notTranslated.push_back("the theme's user-defined keywords for " + lt.attr("name") + " " + ws.attr("name")
                                                        + ": wxNote has no such list");
                        for (const auto& t : targets)
                        {
                            // Words wxNote's own list already has would only repeat it.
                            std::vector<std::string> builtIn;
                            for (const WxnKeywordSetName* k : wxnKeywordSetsOf(lexerOfLanguage(t.first)))
                                if (t.second == k->name)
                                {
                                    const auto lists = wxnBuiltinKeywordLists(t.first);
                                    if (const auto b = lists.find(k->slot); b != lists.end()) builtIn = splitWords(b->second);
                                }
                            const std::vector<std::string> add = without(words, builtIn);
                            if (!add.empty()) addTo(t.first, t.second, add);
                        }
                    }
    }

    // The languages.yaml document.
    ryml::Tree t;
    wxnyaml::MutNode root = wxnyaml::resetToMap(t);
    wxnyaml::MutNode langs = wxnyaml::addMap(root, "languages");
    auto edit = [&](wxnyaml::MutNode parent, const std::string& key, const Words& add) {   // {add: [...]}
        wxnyaml::MutNode m = wxnyaml::addMap(parent, key);
        wxnyaml::setOneLine(m);
        wxnyaml::MutNode seq = wxnyaml::addSeq(m, "add");
        for (const std::string& s : add) wxnyaml::setText(wxnyaml::addItem(seq), s);
    };
    for (const auto& [lang, e] : entries)
    {
        std::vector<std::pair<std::string, Words>> lists;
        for (const auto& l : e.lists) if (!l.second.empty()) lists.push_back(l);
        const bool ext = !e.ext.empty();
        if (!ext && !e.line && !e.block && lists.empty()) continue;
        wxnyaml::MutNode l = wxnyaml::addMap(langs, lang);
        if (ext) edit(l, "extensions", e.ext);
        if (e.line || e.block)
        {
            wxnyaml::MutNode c = wxnyaml::addMap(l, "comments");
            wxnyaml::setOneLine(c);
            if (e.line) wxnyaml::setText(wxnyaml::addKey(c, "line"), e.lineToken);
            if (e.block)
            {
                wxnyaml::MutNode b = wxnyaml::addSeq(c, "block");
                if (!e.open.empty() && !e.close.empty()) { wxnyaml::setText(wxnyaml::addItem(b), e.open); wxnyaml::setText(wxnyaml::addItem(b), e.close); }
            }
        }
        if (!lists.empty())
        {
            wxnyaml::MutNode k = wxnyaml::addMap(l, "keywords");
            for (const auto& list : lists) edit(k, list.first, list.second);
        }
        out.languages.push_back(lang);
    }
    if (out.languages.empty()) return true;
    std::string body;
    if (!wxnyaml::emit(t, body)) { if (err) *err = "could not write the language definitions"; return false; }
    out.yaml = "# Imported from Notepad++.\n" + body;
    return true;
}

}   // namespace nppcompat
