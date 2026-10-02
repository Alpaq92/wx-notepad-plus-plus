// SPDX-License-Identifier: GPL-3.0-or-later
//
// Self-test for npp-compat: the shortcuts.xml -> accelerator translator, the XML reader, and the
// translations of themes, config.xml, contextMenu.xml, sessions and workspaces into wxNote's YAML. No
// wxWidgets, Scintilla or host: plain strings in and out, so it runs on every CI leg as a "pure" ctest.
//   cmake --build build --target npp_compat_selftest && build/bin/npp_compat_selftest
// Exit code 0 = all checks passed.

#include "npp_shortcuts_parse.h"
#include "npp_shortcuts_accel.h"
#include "npp_session.h"
#include "npp_translate.h"
#include "npp_xml.h"
#include "lang_table.h"
#include "language_defs.h"
#include "settings_schema.h"

#include <cstdio>
#include <functional>
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

// =====================================================================================================
// The XML reader and the translations into wxNote's YAML (npp_xml.h, npp_translate.h, npp_session.h).

static void testXmlReader()
{
    XmlElement r;
    std::string err;
    check(parseXml("<?xml version=\"1.0\"?>\n<!DOCTYPE a [ <!ENTITY x \"PWNED\"> ]>\n"
                   "<a k=\"&x;&amp;&#x105;&#0;\">t&lt;<![CDATA[<raw>]]></a>", r, &err), "xml: parses past a DOCTYPE");
    check(r.attr("k") == "&x;&\xC4\x85\xEF\xBF\xBD", "xml: unknown entity stays literal; predefined and numeric decode; &#0; is U+FFFD");
    check(r.text == "t<<raw>", "xml: text and CDATA");
    std::string deep;
    for (int i = 0; i < 100; ++i) deep += "<a>";
    for (int i = 0; i < 100; ++i) deep += "</a>";
    check(!parseXml(deep, r, &err), "xml: nesting past 64 levels is refused, not recursed");
    check(!parseXml("<a><b></a></b>", r), "xml: mismatched tags fail");
    check(!parseXml("<a>", r), "xml: an unclosed element fails");
    check(!parseXml("<a/><b/>", r), "xml: a second root fails");
    check(!parseXml("", r), "xml: no root fails");
    check(parseXml("\xEF\xBB\xBF<a/>", r) && r.name == "a", "xml: a UTF-8 BOM is skipped");
    check(leadingXmlComment("<?xml version=\"1.0\"?>\n<!-- hello -->\n<a/>") == " hello ", "xml: the leading comment is found");
    check(leadingXmlComment("<a><!-- inside --></a>").empty(), "xml: a comment inside the root is not a leading one");

    // What a hostile file can cost is bounded: attributes and elements are counted, not just nesting.
    std::string many = "<a";
    for (int i = 0; i < 1000001; ++i) many += " b";
    many += "/>";
    check(!parseXml(many, r, &err) && err.find("attributes") != std::string::npos, "xml: over a million attributes are refused");
    std::string wide = "<a>";
    for (int i = 0; i < 500001; ++i) wide += "<b/>";
    wide += "</a>";
    check(!parseXml(wide, r, &err) && err.find("elements") != std::string::npos, "xml: over half a million elements are refused");
}

static void testThemeTranslation()
{
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
        "<!--\n    My Theme - credits line\n    second line\n-->\n"
        "<NotepadPlus>\n"
        "  <LexerStyles>\n"
        "    <LexerType name=\"cpp\" desc=\"C++\" ext=\"cc2\">\n"
        "      <WordsStyle name=\"KEYWORD\" styleID=\"5\" fgColor=\"0000FF\" bgColor=\"\" fontName=\"\" fontStyle=\"10\" fontSize=\"\" keywordClass=\"instre1\">extra words</WordsStyle>\n"
        "      <WordsStyle name=\"BAD\" styleID=\"x\" fgColor=\"FF0000\" />\n"
        "      <WordsStyle name=\"HEAVY\" styleID=\"6\" fontWeight=\"600\" fontSize=\"12\" fontName=\"Hack\" />\n"
        "    </LexerType>\n"
        "  </LexerStyles>\n"
        "  <GlobalStyles>\n"
        "    <WidgetStyle name=\"Default Style\" styleID=\"32\" fgColor=\"000000\" bgColor=\"FFFFFF\" fontName=\"Cascadia Mono\" fontStyle=\"1\" fontSize=\"11\" />\n"
        "  </GlobalStyles>\n"
        "</NotepadPlus>\n";
    check(detectNppFile(xml) == NppFileKind::Theme, "theme: detected");
    {
        // A credits comment that is mostly blank lines is trimmed in one pass - one line at a time was
        // quadratic, and 200,000 of them would not finish.
        wxntheme::Theme blank;
        const std::string padded = "<!--" + std::string(200000, '\n') + "credits-->"
                                   "<NotepadPlus><GlobalStyles><WidgetStyle name=\"Default Style\" styleID=\"32\" /></GlobalStyles></NotepadPlus>";
        check(themeFromNpp(padded, blank) && blank.header == "# credits\n", "theme: a comment of 200,000 blank lines is trimmed at once");
    }
    wxntheme::Theme t;
    std::string err;
    check(themeFromNpp(xml, t, &err), "theme: translates");
    check(t.header == "# My Theme - credits line\n# second line\n", "theme: the credits comment becomes the YAML header");
    const wxntheme::Lexer* cpp = t.lexer("cpp");
    check(cpp && cpp->description == "C++" && cpp->extensions == "cc2", "theme: lexer name, description and extensions");
    check(cpp && cpp->styles.size() == 2, "theme: a style with no usable id is dropped");
    check(cpp && cpp->styles.size() == 2 && cpp->styles[0].fg == 0x0000FF && cpp->styles[0].bg == -1
              && cpp->styles[0].fontStyle == 2, "theme: colours, an empty colour inherits, fontStyle keeps its bold/italic/underline bits");
    check(cpp && cpp->styles.size() == 2 && cpp->styles[1].weight == 600 && cpp->styles[1].size == 12
              && cpp->styles[1].font == "Hack", "theme: font, size and wxNote's fontWeight");
    const wxntheme::Style* def = t.global("Default Style");
    check(def && def->fg == 0 && def->bg == 0xFFFFFF && def->font == "Cascadia Mono" && def->size == 11 && def->fontStyle == 0,
          "theme: a global style keeps colours, face and size");

    const std::string yaml = wxntheme::emit(t);
    check(yaml.find("keywordClass") == std::string::npos && yaml.find("extra words") == std::string::npos,
          "theme: Notepad++-only attributes and keyword text are left behind");
    wxntheme::Theme back;
    check(wxntheme::parse(yaml, back), "theme: the YAML written reads back");
    const wxntheme::Lexer* bcpp = back.lexer("cpp");
    check(back.header == t.header && bcpp && bcpp->styles.size() == 2 && bcpp->styles[0].fontStyle == 2
              && bcpp->styles[1].weight == 600 && back.global("Default Style") && back.global("Default Style")->size == 11,
          "theme: ...exactly");
    check(!themeFromNpp("<NotepadPlus><GUIConfigs/></NotepadPlus>", t, &err), "theme: a file that is not one is refused");
}

static std::string valueOf(const ConfigTranslation& t, const std::string& id)
{
    for (const auto& kv : t.settings) if (kv.first == id) return kv.second;
    return "<unset>";
}

static void testConfigTranslation()
{
    const std::string xml =
        "<NotepadPlus><GUIConfigs>"
        "<GUIConfig name=\"TabSetting\" replaceBySpace=\"yes\" size=\"2\" />"
        "<GUIConfig name=\"ScintillaPrimaryView\" lineNumberMargin=\"hide\" Wrap=\"yes\" currentLineIndicator=\"0\" edgeMultiColumnPos=\"80 120\" zoom=\"2\" />"
        "<GUIConfig name=\"NewDocDefaultSettings\" format=\"2\" encoding=\"4\" lang=\"22\" />"
        "<GUIConfig name=\"auto-insert\" parentheses=\"yes\" htmlXmlTag=\"yes\" />"
        "<GUIConfig name=\"StatusBar\">hide</GUIConfig>"
        "<GUIConfig name=\"MultiInstance\" setting=\"2\" />"
        "<GUIConfig name=\"SearchEngine\" searchEngineChoice=\"3\" />"
        "<GUIConfig name=\"DarkMode\" enable=\"yes\" />"
        "<GUIConfig name=\"stylerTheme\" path=\"C:\\Users\\x\\AppData\\Roaming\\Notepad++\\themes\\Monokai.xml\" />"
        "<GUIConfig name=\"Caret\" width=\"9\" blinkRate=\"600\" />"
        "</GUIConfigs><History nbMaxFile=\"15\" /></NotepadPlus>";
    check(detectNppFile(xml) == NppFileKind::Config, "config: detected");
    ConfigTranslation t;
    check(settingsFromConfig(xml, t), "config: translates");
    check(valueOf(t, "editor.tabSize") == "2" && valueOf(t, "editor.useTabs") == "false", "config: tab size, and spaces for tabs");
    check(valueOf(t, "editor.lineNumbers") == "false" && valueOf(t, "editor.wordWrap") == "true", "config: line numbers and wrap");
    check(valueOf(t, "editor.highlightCurrentLine") == "false" && valueOf(t, "editor.edgeColumn") == "80", "config: current line and the first edge column");
    check(valueOf(t, "files.newDocument.eol") == "lf" && valueOf(t, "files.newDocument.encoding") == "utf-8"
              && valueOf(t, "files.newDocument.language") == "Python", "config: the new-document EOL, encoding and language (LangType 22)");
    check(valueOf(t, "editor.autoClosePairs") == "true" && valueOf(t, "ui.statusBar.visible") == "false", "config: auto-close and the status bar");
    check(valueOf(t, "window.reuseInstance") == "false" && valueOf(t, "search.webEngine") == "bing", "config: instances and the search engine");
    check(valueOf(t, "ui.themeMode") == "dark" && valueOf(t, "ui.colorTheme") == "Monokai", "config: dark mode and the theme by name");
    check(valueOf(t, "files.maxRecentFiles") == "15", "config: recent-files length from <History>");
    check(valueOf(t, "editor.caretWidth") == "<unset>" && valueOf(t, "editor.caretBlinkMs") == "600", "config: an out-of-range value is not translated");
    bool zoom = false, tags = false;
    for (const std::string& s : t.notTranslated) { zoom = zoom || s.find("zoom") != std::string::npos; tags = tags || s.find("HTML/XML") != std::string::npos; }
    check(zoom && tags, "config: what has no equivalent is listed, not dropped silently");

    // Every value must be one wxNote's own schema accepts: the host refuses the rest anyway.
    bool allValid = true;
    for (const auto& kv : t.settings)
    {
        const wxnsettings::Def* d = wxnsettings::findDef(kv.first);
        bool ok = d != nullptr;
        if (ok && d->kind == wxnsettings::Kind::Bool) ok = kv.second == "true" || kv.second == "false";
        if (ok && d->kind == wxnsettings::Kind::Int) { const long long n = std::stoll(kv.second); ok = n >= d->lo && n <= d->hi; }
        if (ok && d->kind == wxnsettings::Kind::Choice)
        {
            bool found = false;
            for (const std::string& n : wxnsettings::choiceNames(*d)) found = found || n == kv.second;
            ok = found;
        }
        if (!ok) { std::printf("        not accepted: %s = %s\n", kv.first.c_str(), kv.second.c_str()); allValid = false; }
    }
    check(allValid, "config: every translated value is one the settings schema accepts");
    check(!settingsFromConfig("<NotepadPlus><Session/></NotepadPlus>", t), "config: a file that is not one is refused");
}

static void testContextMenuTranslation()
{
    const std::string xml =
        "<NotepadPlus><ScintillaContextMenu>"
        "<Item id=\"42001\" MenuEntryName=\"Edit\" MenuItemName=\"Cut\" />"
        "<Item id=\"0\" />"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Undo\" />"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Turn It Up To Eleven\" />"
        "<Item FolderName=\"Style token\" id=\"43022\" />"
        "<Item PluginEntryName=\"MIME Tools\" PluginCommandItemName=\"Base64 Encode\" ItemNameAs=\"Encode\" />"
        "</ScintillaContextMenu></NotepadPlus>";
    check(detectNppFile(xml) == NppFileKind::ContextMenu, "context menu: detected");
    std::string yaml;
    std::vector<std::string> notTranslated;
    check(contextMenuFromNpp(xml, yaml, notTranslated), "context menu: translates");
    // An item as a line: a number, '-', or "menu:<label>[...]" / "plugin:<p>/<c>" / "<n>=<label>".
    std::function<std::string(wxnyaml::Node)> show = [&](wxnyaml::Node n) -> std::string {
        std::string s;
        if (wxnyaml::getText(n, s)) return s;
        if (!wxnyaml::isMap(n)) return "?";
        std::string label = wxnyaml::textOr(wxnyaml::child(n, "label"), std::string());
        std::string out;
        if (wxnyaml::child(n, "menu").readable())
        {
            out = "menu:" + wxnyaml::textOr(wxnyaml::child(n, "menu"), std::string()) + "[";
            for (wxnyaml::Node c : wxnyaml::child(n, "items").children()) out += show(c) + ",";
            return out + "]";
        }
        if (wxnyaml::child(n, "plugin").readable())
            out = "plugin:" + wxnyaml::textOr(wxnyaml::child(n, "plugin"), std::string()) + "/" + wxnyaml::textOr(wxnyaml::child(n, "command"), std::string());
        else out = wxnyaml::textOr(wxnyaml::child(n, "command"), std::string());
        return label.empty() ? out : out + "=" + label;
    };
    auto itemsOf = [&](const std::string& y) {
        wxnyaml::Doc d;
        std::vector<std::string> out;
        if (wxnyaml::parse(y, d) && wxnyaml::isSeq(wxnyaml::child(d.root(), "items")))
            for (wxnyaml::Node n : wxnyaml::child(d.root(), "items").children()) out.push_back(show(n));
        return out;
    };
    check(itemsOf(yaml) == std::vector<std::string>({ "42001", "-", "42003", "menu:Style token[43022,]", "plugin:MIME Tools/Base64 Encode=Encode" }),
          "context menu: numbers, names, separators, a folder and a plugin command with its own label carry over");
    check(notTranslated.size() == 1, "context menu: only the name Notepad++ has no item for is reported");

    // Notepad++'s own contextMenu.xml (as 8.9 ships it): named edit items, its three style folders with their
    // TranslateIDs, a plugin folder with an own label - all of it must come across.
    const std::string stock =
        "<NotepadPlus><ScintillaContextMenu>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Cut\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"Copy\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Paste\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"Delete\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Select all\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"Begin/End Select\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Begin/End Select in Column Mode\"/><Item id=\"0\"/>"
        "<Item FolderName=\"Style all occurrences of token\" TranslateID=\"contextMenu-styleAlloccurrencesOfToken\" id=\"43022\"/>"
        "<Item FolderName=\"Style all occurrences of token\" TranslateID=\"contextMenu-styleAlloccurrencesOfToken\" id=\"43024\"/>"
        "<Item FolderName=\"Style one token\" TranslateID=\"contextMenu-styleOneToken\" id=\"43062\"/>"
        "<Item FolderName=\"Clear style\" TranslateID=\"contextMenu-clearStyle\" id=\"43023\"/>"
        "<Item FolderName=\"Clear style\" TranslateID=\"contextMenu-clearStyle\" id=\"43032\"/><Item id=\"0\"/>"
        "<Item FolderName=\"Plugin commands\" TranslateID=\"contextMenu-PluginCommands\" PluginEntryName=\"MIME Tools\" PluginCommandItemName=\"Base64 Encode\"/>"
        "<Item FolderName=\"Plugin commands\" TranslateID=\"contextMenu-PluginCommands\" PluginEntryName=\"NppExport\" "
        "PluginCommandItemName=\"Copy all formats to clipboard\" ItemNameAs=\"Copy Text with Syntax Highlighting\"/><Item id=\"0\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"UPPERCASE\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"lowercase\"/><Item id=\"0\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Open File\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"Search on Internet\"/><Item id=\"0\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Toggle Single Line Comment\"/><Item MenuEntryName=\"Edit\" MenuItemName=\"Block Comment\"/>"
        "<Item MenuEntryName=\"Edit\" MenuItemName=\"Block Uncomment\"/><Item id=\"0\"/>"
        "<Item MenuEntryName=\"View\" MenuItemName=\"Hide lines\"/>"
        "</ScintillaContextMenu></NotepadPlus>";
    check(contextMenuFromNpp(stock, yaml, notTranslated) && notTranslated.empty(), "stock context menu: nothing is left out");
    check(itemsOf(yaml) == std::vector<std::string>({
              "42001", "42002", "42005", "42006", "42007", "42020", "42089", "-",
              "menu:Style &All Occurrences of Token[43022,43024,]", "menu:Style &One Token[43062,]", "menu:Clear Style[43023,43032,]", "-",
              "menu:Plugin commands[plugin:MIME Tools/Base64 Encode,plugin:NppExport/Copy all formats to clipboard=Copy Text with Syntax Highlighting,]", "-",
              "42016", "42017", "-", "42073", "42075", "-", "42022", "42023", "42047", "-", "44042" }),
          "stock context menu: every item, folder, plugin command and label comes across, in order");

    // A folder name the user changed is theirs, TranslateID or not; the same name in two menus resolves
    // within the menu given.
    check(contextMenuFromNpp("<NotepadPlus><ScintillaContextMenu>"
                             "<Item FolderName=\"My styles\" TranslateID=\"contextMenu-clearStyle\" id=\"43023\"/>"
                             "<Item MenuEntryName=\"View\" MenuItemName=\"Folder as Workspace\"/>"
                             "<Item MenuEntryName=\"File\" MenuItemName=\"Folder as Workspace\"/>"
                             "</ScintillaContextMenu></NotepadPlus>", yaml, notTranslated)
          && itemsOf(yaml) == std::vector<std::string>({ "menu:My styles[43023,]", "44085", "41025" }),
          "context menu: a renamed folder keeps its name; a name in two menus resolves within the one given");
}

static void testSessionAndWorkspace()
{
    const std::string session =
        "<NotepadPlus><Session activeView=\"0\">"
        "<mainView activeIndex=\"1\"><File filename=\"C:\\a.txt\" /><File filename=\"C:\\b &amp; c.txt\" /></mainView>"
        "<subView activeIndex=\"0\"><File filename=\"/home/u/d.md\" /></subView>"
        "</Session></NotepadPlus>";
    check(detectNppFile(session) == NppFileKind::Session, "session: detected");
    std::vector<std::string> files;
    check(sessionFilesFromNpp(session, files) && files == std::vector<std::string>({ "C:\\a.txt", "C:\\b & c.txt", "/home/u/d.md" }),
          "session: main view's files, then the sub view's");
    check(!sessionFilesFromNpp("<NotepadPlus><Project/></NotepadPlus>", files), "session: a file that is not one is refused");

    const std::vector<std::string> views[2] = { { "C:\\x \"q\" <&>.txt", "C:\\y.txt" }, { "D:\\z.txt" } };
    const int active[2] = { 1, 0 };
    const std::string written = nppSessionXml(views, active, 1);
    check(written.find("<NotepadPlus>") != std::string::npos && written.find("activeView=\"1\"") != std::string::npos,
          "session writer: Notepad++'s shape");
    check(sessionFilesFromNpp(written, files) && files == std::vector<std::string>({ views[0][0], views[0][1], views[1][0] }),
          "session writer: reads back, quotes and angle brackets in paths included");

    // Positions, bookmarks and active tabs, both ways - what the bridge carries between Notepad++ plugins'
    // session files and the host's own (wxNote YAML) ones.
    const std::string withPos =
        "<NotepadPlus><Session activeView=\"1\">"
        "<mainView activeIndex=\"0\"><File filename=\"C:\\a.txt\" startPos=\"42\" firstVisibleLine=\"7\">"
        "<Mark line=\"3\" /><Mark line=\"9\" /></File></mainView>"
        "<subView activeIndex=\"0\"><File filename=\"D:\\b.txt\" /></subView></Session></NotepadPlus>";
    NppSession s;
    check(sessionFromNpp(withPos, s) && s.views[0].size() == 1 && s.views[0][0].caret == 42 && s.views[0][0].firstLine == 7
              && s.views[0][0].marks == std::vector<long long>({ 3, 9 }) && s.active[1] == 0 && s.activeView == 1,
          "session: caret, first line, bookmarks and active tabs read");
    NppSession viaYaml;
    check(sessionFromWxnote(wxnoteSessionYaml(s), viaYaml) && viaYaml.views[0][0].caret == 42 && viaYaml.views[0][0].firstLine == 7
              && viaYaml.views[0][0].marks == s.views[0][0].marks && viaYaml.views[1].size() == 1 && viaYaml.activeView == 1,
          "session: through wxNote's YAML and back, nothing lost");
    NppSession again;
    check(sessionFromNpp(nppSessionXml(viaYaml), again) && again.views[0][0].marks == s.views[0][0].marks
              && again.views[0][0].caret == 42 && again.active[0] == 0,
          "session: written as Notepad++ XML again, with its <Mark> lines");
    NppSessionFile odd;
    odd.path = std::string("C:\\tab\there\x01.txt");
    NppSession oddSession;
    oddSession.views[0].push_back(odd);
    check(sessionFromNpp(nppSessionXml(oddSession), again) && again.views[0].size() == 1 && again.views[0][0].path == "C:\\tab\there.txt",
          "session writer: a tab survives as a character reference; a control character XML cannot hold is left out");

    const std::string ws =
        "<NotepadPlus><Project name=\"P\"><Folder name=\"src\"><File name=\"a.cpp\" /></Folder>"
        "<File name=\"C:\\abs\\b.txt\" /></Project><Project name=\"Q\"><File name=\"q.txt\" /></Project></NotepadPlus>";
    check(detectNppFile(ws) == NppFileKind::Workspace, "workspace: detected");
    std::string yaml;
    check(workspaceFromNpp(ws, "D:/w", yaml), "workspace: translates");
    wxnyaml::Doc d;
    check(wxnyaml::parse(yaml, d) && wxnyaml::textOr(wxnyaml::child(d.root(), "name"), std::string()) == "P", "workspace: the first project names it");
    const wxnyaml::Node items = wxnyaml::child(d.root(), "items");
    check(wxnyaml::isSeq(items) && items.num_children() == 3, "workspace: a folder, a file, and the second project as a folder");
    if (wxnyaml::isSeq(items) && items.num_children() == 3)
    {
        const wxnyaml::Node src = items[0];
        check(wxnyaml::textOr(wxnyaml::child(src, "folder"), std::string()) == "src"
                  && wxnyaml::textOr(wxnyaml::child(wxnyaml::child(src, "items")[0], "file"), std::string()) == "D:/w/a.cpp",
              "workspace: a relative path resolves against the workspace's folder");
        check(wxnyaml::textOr(wxnyaml::child(items[1], "file"), std::string()) == "C:\\abs\\b.txt", "workspace: an absolute path is kept");
        check(wxnyaml::textOr(wxnyaml::child(items[2], "folder"), std::string()) == "Q", "workspace: another project becomes a folder");
    }
    check(detectNppFile("<NotepadPlus><Whatever/></NotepadPlus>") == NppFileKind::Unknown, "detect: anything else is unknown");
    check(detectNppFile(kXml) == NppFileKind::Shortcuts, "detect: shortcuts.xml");
}

// langs.xml (against langs.model.xml) and the theme's user-defined keywords -> languages.yaml, read back
// the way wxNote reads it.
static void testLanguagesTranslation()
{
    auto canonical = [](const std::string& w) {
        size_t n;
        const WxnLang* t = wxnLangTable(n);
        for (size_t i = 0; i < n; ++i) if (wxnLangLower(t[i].name) == wxnLangLower(w)) return std::string(t[i].name);
        return std::string();
    };
    auto lexerOf = [](const std::string& name) { const WxnLang* l = wxnLangFindByName(name); return l ? std::string(l->lexer) : std::string(); };
    auto words = [](const WxnLangDefs& d, const std::string& lang, const std::string& list) {
        const WxnLangDef* def = d.find(lang);
        if (!def || !def->keywords.count(list)) return std::string("(none)");
        std::string s;
        for (const std::string& w : def->keywords.at(list).words) s += (s.empty() ? "" : " ") + w;
        return s;
    };
    const std::string model =
        "<NotepadPlus><Languages>"
        "<Language name=\"python\" ext=\"py pyw\" commentLine=\"#\" tabSettings=\"132\"><Keywords name=\"instre1\">and def lambda</Keywords>"
        "<Keywords name=\"substyle1\"></Keywords></Language>"
        "<Language name=\"cpp\" ext=\"cpp h\" commentLine=\"//\" commentStart=\"/*\" commentEnd=\"*/\">"
        "<Keywords name=\"instre1\">int return</Keywords><Keywords name=\"type1\">size_t</Keywords>"
        "<Keywords name=\"instre2\">vector</Keywords><Keywords name=\"type2\">brief</Keywords></Language>"
        "<Language name=\"go\" ext=\"go\" commentLine=\"//\"><Keywords name=\"instre1\">func</Keywords></Language>"
        "<Language name=\"html\" ext=\"html\" commentStart=\"&lt;!--\" commentEnd=\"--&gt;\"><Keywords name=\"instre1\">div</Keywords></Language>"
        "<Language name=\"php\" ext=\"php\"><Keywords name=\"instre1\">echo</Keywords></Language>"
        "<Language name=\"xml\" ext=\"xml\"><Keywords name=\"instre1\">DOCTYPE</Keywords></Language>"
        "<Language name=\"ini\" ext=\"ini\" commentLine=\";\"/>"
        "<Language name=\"props\" ext=\"properties\" commentLine=\"#\"/>"
        "<Language name=\"fcST\" ext=\"st\"/>"
        "<Language name=\"searchResult\" ext=\"\"/>"
        "</Languages></NotepadPlus>";
    const std::string user =
        "<NotepadPlus><Languages>"
        "<Language name=\"python\" ext=\"py pyw sage\" commentLine=\"##\" tabSettings=\"136\"><Keywords name=\"instre1\">and def match</Keywords>"
        "<Keywords name=\"substyle1\">self cls</Keywords></Language>"
        "<Language name=\"cpp\" ext=\"cpp h ipp\" commentLine=\"//\" commentStart=\"/*\" commentEnd=\"*/\">"
        "<Keywords name=\"instre1\">int return co_await</Keywords><Keywords name=\"type1\">size_t ssize_t</Keywords>"
        "<Keywords name=\"instre2\">vector QString</Keywords><Keywords name=\"type2\">brief mytag</Keywords></Language>"
        "<Language name=\"go\" ext=\"go\" commentLine=\"//\"><Keywords name=\"instre1\">func</Keywords></Language>"
        "<Language name=\"html\" ext=\"html\" commentStart=\"&lt;!--\" commentEnd=\"--&gt;\"><Keywords name=\"instre1\">div my-element</Keywords></Language>"
        "<Language name=\"php\" ext=\"php\"><Keywords name=\"instre1\">echo frobnicate</Keywords></Language>"
        "<Language name=\"xml\" ext=\"xml\"><Keywords name=\"instre1\">DOCTYPE MYDECL</Keywords></Language>"
        "<Language name=\"ini\" ext=\"ini cnf\" commentLine=\"#\"/>"
        "<Language name=\"props\" ext=\"properties prp\" commentLine=\"#\"/>"
        "<Language name=\"lua\" ext=\"lua\" commentLine=\"--\"><Keywords name=\"instre1\">and break</Keywords></Language>"
        "<Language name=\"fcST\" ext=\"st stx\"/>"
        "<Language name=\"searchResult\" ext=\"xyz\"/>"
        "</Languages></NotepadPlus>";
    const std::string theme =
        "<NotepadPlus><LexerStyles>"
        "<LexerType name=\"perl\"><WordsStyle name=\"INSTRUCTION WORD\" styleID=\"5\" keywordClass=\"instre1\">carp croak</WordsStyle></LexerType>"
        "<LexerType name=\"html\"><WordsStyle name=\"USER ATTRIBUTES1\" styleID=\"196\" keywordClass=\"substyle5\">download</WordsStyle></LexerType>"
        "<LexerType name=\"cpp\"><WordsStyle name=\"INSTRUCTION WORD\" styleID=\"5\" keywordClass=\"instre1\">constexpr my_kw</WordsStyle></LexerType>"
        "</LexerStyles></NotepadPlus>";

    LanguagesTranslation t;
    std::string err;
    check(languagesFromNpp(user, model, theme, t, &err) && t.compared && !t.yaml.empty(), "languages: translates against the model");
    WxnLangDefs d;
    check(wxnParseLangDefs(t.yaml, d, &err, canonical, lexerOf) && d.warnings.empty(), "languages: wxNote reads what it wrote");

    const WxnLangDef* py = d.find("Python");
    check(py && py->extensions.words == std::vector<std::string>({ "sage" }) && !py->extensions.replace && py->extensions.remove.empty(),
          "languages: an added extension is added");
    check(py && py->hasLineComment && py->lineComment == "##" && !py->hasBlockComment, "languages: a changed comment token comes across, alone");
    check(words(d, "Python", "keywords") == "match", "languages: an added keyword is added, a removed one is not taken away");
    check(words(d, "Python", "userKeywords1") == "self cls", "languages: substyle1 is the first user keyword group");
    check(words(d, "C++", "keywords") == "co_await my_kw", "languages: C++ instre1 is its keywords (theme's user keywords added, known ones not)");
    check(words(d, "C++", "types") == "ssize_t", "languages: the C family's type1 is its types list");
    check(words(d, "C++", "globalClasses") == "QString", "languages: ...and instre2 its global classes");
    check(words(d, "C", "docKeywords") == "mytag" && words(d, "Java", "docKeywords") == "mytag" && !d.find("Go"),
          "languages: C++'s doxygen words go to every C-family language but Go");
    check(d.find("C++") && d.find("C++")->extensions.words == std::vector<std::string>({ "ipp" }), "languages: C++'s added extension");
    for (const char* l : { "HTML", "PHP", "ASP", "JSP" })
        check(words(d, l, "html") == "my-element" && words(d, l, "php") == "frobnicate" && words(d, l, "userAttributes1") == "download",
              "languages: the HTML family shares the markup's and the scripts' lists, and the theme's attribute group");
    check(words(d, "XML", "sgml") == "MYDECL", "languages: XML's instre1 is its SGML list");
    check(words(d, "Perl", "keywords") == "carp croak", "languages: the theme's user-defined keywords are added");
    check(d.find("Properties") && d.find("Properties")->extensions.words == std::vector<std::string>({ "cnf", "prp" })
              && !d.find("Properties")->hasLineComment, "languages: ini's extensions go to Properties with props', its comment does not");
    check(!d.find("Lua"), "languages: a language langs.model.xml lacks brings nothing - not its extensions, comments or keywords");
    auto noted = [&](const std::string& part) {
        for (const std::string& s : t.notTranslated) if (contains(s, part)) return true;
        return false;
    };
    check(noted("ini comment tokens"), "languages: ...which is reported");
    check(noted("fcST extensions: wxNote has no such language"), "languages: a language wxNote lacks is reported");
    check(noted("lua: not in langs.model.xml"), "languages: ...and so is one the model lacks");
    check(noted("python: 1 extension(s) or keyword(s)") , "languages: what langs.xml lacks is counted, not imported");
    check(noted("Python's indentation (tabSettings 136)"), "languages: changed indentation is pointed at settings.yaml");
    check(!contains(t.yaml, "xyz"), "languages: the search-results pane is no language");

    LanguagesTranslation noModel;
    check(languagesFromNpp(user, "", theme, noModel, &err) && !noModel.compared, "languages: without the model");
    WxnLangDefs nd;
    check(wxnParseLangDefs(noModel.yaml, nd, &err, canonical, lexerOf) && !nd.find("Python") && words(nd, "Perl", "keywords") == "carp croak",
          "languages: ...only the theme's user-defined keywords come across");
    check(!noModel.notTranslated.empty() && contains(noModel.notTranslated[0], "langs.model.xml"), "languages: ...and the report says why");

    LanguagesTranslation same;
    check(languagesFromNpp(model, model, "", same, &err) && same.yaml.empty(), "languages: an unchanged langs.xml brings nothing");
    check(!languagesFromNpp("<NotepadPlus/>", model, "", same, &err), "languages: a file without <Languages> is refused");
    check(detectNppFile(model) == NppFileKind::Languages, "detect: langs.xml");
    check(activeThemeFromConfig("<NotepadPlus><GUIConfigs><GUIConfig name=\"stylerTheme\" path=\"C:\\x\\themes\\Zenburn.xml\" /></GUIConfigs></NotepadPlus>")
              == "C:\\x\\themes\\Zenburn.xml"
              && activeThemeFromConfig("<NotepadPlus><GUIConfigs><GUIConfig name=\"stylerTheme\" path=\"C:\\x\\stylers.xml\" /></GUIConfigs></NotepadPlus>").empty(),
          "config.xml: the active theme, none for stylers.xml");
    check(nppFileNameOf("C:\\x\\themes\\Zenburn.xml") == "Zenburn.xml" && nppFileNameOf("/home/x/themes/Zenburn.xml") == "Zenburn.xml"
              && nppFileNameOf("Zenburn.xml") == "Zenburn.xml", "config.xml: a theme path's file name, Windows path or not");
}

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

    testXmlReader();
    testThemeTranslation();
    testConfigTranslation();
    testContextMenuTranslation();
    testSessionAndWorkspace();
    testLanguagesTranslation();

    if (g_fail == 0) std::printf("ALL PASS (npp-compat selftest)\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
