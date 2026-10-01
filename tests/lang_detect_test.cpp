// SPDX-License-Identifier: Apache-2.0
//
// lang_detect_test - which language a file opens as (src/lang_detect.h), driven through the REAL
// Scintillua engine and the lexer.lua the build fetched, against the real Language-menu table.
//
// Detection used to be a 16-group extension table: .html, .md, .toml, Makefile and Dockerfile all
// opened as plain text, and .go was handed to a Lexilla lexer that does not exist. It is now
// Scintillua's lexer.detect() behind wxNote's overrides, so this suite pins:
//   * the answers for the files people actually open - by name, by first line, by user mapping;
//   * the Style Configurator's two extension lines: the User ext. precedence (over everything, while
//     a theme's own ext attributes only fill gaps), and the Default ext. lists it shows;
//   * that every name the glue tables hand out is a real Language-menu entry;
//   * that lexer.lua still speaks the vocabulary those tables were written against. A language
//     Scintillua adds or renames fails here when the pin in CMakeLists.txt is bumped, instead of
//     silently opening as plain text;
//   * the invariant that makes detection safe to lean on: wherever the comment table has an opinion,
//     the language a file is HIGHLIGHTED as comments it the same way.
//
//   cmake --build build --target lang_detect_test && build/bin/lang_detect_test
//
#include "lang_detect.h"
#include "scintillua_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

static void expectEq(const std::string& got, const std::string& want, const std::string& what)
{
    const bool ok = (got == want);
    check(ok, what);
    if (!ok) std::printf("        want [%s]  got [%s]\n", want.c_str(), got.c_str());
}

static std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

static std::set<std::string> g_menu;           // wxnLangTable names
static scintillua::Engine*   g_eng = nullptr;

// wxnLangTable needs wx, and this suite links only the engine, so read menu_data_language.h as DATA
// (the comment_tokens_test pattern). Rows look like:  { kCmdLangPython,  "Python",  "python" },
static void loadMenuNames()
{
    const std::string text = readFile(std::string(SRC_DIR) + "/menu_data_language.h");
    for (std::size_t i = text.find("{ kCmdLang"); i != std::string::npos; i = text.find("{ kCmdLang", i + 1))
    {
        const std::size_t q1 = text.find('"', i), eol = text.find('\n', i);
        if (q1 == std::string::npos || (eol != std::string::npos && q1 > eol)) continue;
        const std::size_t q2 = text.find('"', q1 + 1);
        if (q2 != std::string::npos) g_menu.insert(text.substr(q1 + 1, q2 - q1 - 1));
    }
}

static std::string scDetect(const std::string& f, const std::string& l) { return g_eng->detect(f, l); }
static bool known(const std::string& n) { return g_menu.count(n) != 0; }

static std::string detect(const std::string& file, const std::string& head = std::string(),
                          const WxnUserExtMaps& user = WxnUserExtMaps{})
{
    return wxnDetectLanguage(file, head, user, scDetect, known);
}

static void expectLang(const std::string& file, const std::string& want, const std::string& head = std::string())
{
    std::string what = "\"" + file + "\"";
    if (!head.empty()) what += " starting [" + head.substr(0, head.find('\n')) + "]";
    expectEq(detect(file, head), want, what + " -> " + (want.empty() ? "Normal Text" : want));
}

// ---- the engine itself ----------------------------------------------------------------------------

static void testEngine()
{
    std::printf("\n-- Scintillua's lexer.detect() through the embedded engine --\n");
    check(g_eng->ok(), "engine loaded lexer.lua (" + g_eng->lastError() + ")");
    expectEq(g_eng->detect("main.go", ""), "go", "detect(main.go)");
    expectEq(g_eng->detect("Makefile", ""), "makefile", "detect(Makefile) - a whole file name");
    expectEq(g_eng->detect("", "#!/usr/bin/env python3"), "python", "detect(first line: python shebang)");
    expectEq(g_eng->detect("notes.unknownext", ""), "", "detect(unknown) -> nothing");
}

// ---- what people open -------------------------------------------------------------------------------

static void testEverydayFiles()
{
    std::printf("\n-- everyday files, by extension --\n");
    const char* rows[][2] = {
        { "main.cpp", "C++" }, { "util.hpp", "C++" }, { "main.c", "C" }, { "api.h", "C" },
        { "Program.cs", "C#" }, { "App.java", "Java" }, { "app.js", "JavaScript" }, { "app.mjs", "JavaScript" },
        { "view.jsx", "JavaScript" }, { "types.ts", "TypeScript" }, { "view.tsx", "TypeScript" },
        { "index.html", "HTML" }, { "page.htm", "HTML" }, { "index.php", "PHP" }, { "style.css", "CSS" },
        { "theme.scss", "CSS" }, { "README.md", "Markdown" }, { "Cargo.toml", "TOML" },
        { "config.yaml", "YAML" }, { "data.json", "JSON" }, { "settings.xml", "XML" }, { "logo.svg", "XML" },
        { "main.go", "Go" }, { "lib.rs", "Rust" }, { "script.py", "Python" }, { "tool.rb", "Ruby" },
        { "run.pl", "Perl" }, { "init.lua", "Lua" }, { "build.sh", "Shell" }, { "deploy.ps1", "PowerShell" },
        { "setup.bat", "Batch" }, { "query.sql", "SQL" }, { "Main.kt", "Kotlin" },
        { "build.gradle.kts", "Kotlin" }, { "App.swift", "Swift" }, { "main.dart", "Dart" },
        { "app.config.json5", "JSON5" }, { "fix.patch", "Diff" }, { "paper.tex", "LaTeX" },
        { "setup.iss", "Inno Setup" }, { "win.reg", "Registry" }, { "app.rc", "Resource file" },
        { "notes.txt", "" }, { "data.csv", "" },
    };
    for (const auto& r : rows) expectLang(r[0], r[1]);
}

static void testFileNames()
{
    std::printf("\n-- whole file names --\n");
    const char* rows[][2] = {
        { "Makefile", "Makefile" }, { "GNUmakefile", "Makefile" }, { "makefile", "Makefile" },
        { "CMakeLists.txt", "CMake" }, { "Dockerfile", "Dockerfile" }, { "dockerfile", "Dockerfile" },
        { "Dockerfile.dev", "Dockerfile" }, { "PKGBUILD", "Shell" }, { "Rakefile", "Ruby" },
        { "Gemfile", "Ruby" }, { ".bashrc", "Shell" }, { "nginx.service", "Properties" },
    };
    for (const auto& r : rows) expectLang(r[0], r[1]);
}

static void testCaseAndBackups()
{
    std::printf("\n-- upper-case names and backup copies --\n");
    const char* rows[][2] = {
        { "MAIN.CPP", "C++" }, { "README.MD", "Markdown" }, { "SCRIPT.PY", "Python" },
        { "model.R", "R" }, { "model.r", "R" },
        { "main.cpp.orig", "C++" }, { "main.py~", "Python" }, { "main.go.bak", "Go" },
        { "config.ini.old", "Properties" }, { "Makefile.bak", "Makefile" },
        { "plot.m~", "" },                       // the .m override survives the backup suffix
    };
    for (const auto& r : rows) expectLang(r[0], r[1]);
}

static void testOverrides()
{
    std::printf("\n-- where wxNote overrides Scintillua --\n");
    const char* rows[][2] = {
        { "plot.m", "" }, { "ws.sc", "" }, { "defs.inc", "" }, { "Doc.cls", "" }, { "prog.p", "" },
        { "key.asc", "" }, { "board.sch", "" }, { "pkg.changes", "" }, { "debian.sources", "" },
        { "tweak.reg", "Registry" }, { "task.vbs", "VBScript" }, { "player.gd", "GDScript" },
        { "calc.mac", "Maxima" }, { "defs.vh", "Verilog" },
        { "old.f", "Fortran (fixed form)" }, { "old.for", "Fortran (fixed form)" },
        { "new.f90", "Fortran (free form)" },
    };
    for (const auto& r : rows) expectLang(r[0], r[1]);
    // "" means the name decides nothing - the content still may.
    expectLang("plot.m", "MATLAB", "#!/usr/bin/octave\ndisp(1)");
}

static void testKnownButUnhighlightable()
{
    std::printf("\n-- a recognised language wxNote cannot highlight stays text --\n");
    expectLang("mix.exs", "", "#!/usr/bin/env elixir\nIO.puts 1");   // not "Shell" because of the shebang
    expectLang("build.groovy", "");
    expectLang("count.awk", "", "#!/usr/bin/awk -f\n{ n++ }");
}

static void testFirstLine()
{
    std::printf("\n-- the first line, when the name says nothing --\n");
    expectLang("script",    "Python",     "#!/usr/bin/env python3\nprint(1)");
    expectLang("run",       "Shell",      "#!/bin/sh\necho hi");
    expectLang("run",       "Shell",      "#!/bin/bash\necho hi");
    expectLang("run",       "Shell",      "#!/usr/bin/env zsh\necho hi");       // any other interpreter: shell
    expectLang("tool",      "JavaScript", "#!/usr/bin/env node\nconsole.log(1)");
    expectLang("tool",      "PowerShell", "#!/usr/bin/env pwsh\nWrite-Host 1");
    expectLang("tool",      "Perl",       "#!/usr/bin/perl -w\nprint 1;");
    expectLang("tool",      "PHP",        "#!/usr/bin/php\n<?php echo 1;");
    expectLang("doc",       "XML",        "<?xml version=\"1.0\"?>\n<root/>");
    expectLang("page",      "HTML",       "<!DOCTYPE html>\n<html></html>");
    expectLang("user-data", "YAML",       "#cloud-config\npackages: []");
    expectLang("blob",      "JSON",       "{\n  \"a\": 1\n}");
    expectLang("notes",     "",           "just some words");
    expectLang("NEWS",      "");
    // The name outranks the content: an XHTML page with a prolog is still HTML.
    expectLang("page.html", "HTML",       "<?xml version=\"1.0\"?>\n<html/>");
}

static void testUserMapping()
{
    std::printf("\n-- the user's functionList.conf `ext` mapping --\n");
    const std::map<std::string, std::string> keys = {
        { "foo", "python" }, { "cfg", "ini" }, { "tmpl", "html" }, { "h", "cpp" }, { "zz", "no-such-key" },
    };
    WxnUserExtMaps user; user.toKey = &keys;
    expectEq(detect("a.foo", "", user), "Python", "\"a.foo\" mapped to python -> Python");
    expectEq(detect("A.FOO", "", user), "Python", "\"A.FOO\" - the mapping is case-blind like the rest");
    expectEq(detect("a.cfg", "", user), "Properties", "\"a.cfg\" mapped to ini -> Properties");
    expectEq(detect("a.tmpl", "", user), "HTML", "\"a.tmpl\" mapped to html -> HTML");
    expectEq(detect("a.h", "", user), "C++", "\"a.h\" mapped to cpp beats wxNote's own .h override");
    expectEq(detect("a.zz", "", user), "", "\"a.zz\" mapped to an unknown key falls through (to nothing here)");
}

static void testStyleConfiguratorExtensions()
{
    std::printf("\n-- the Style Configurator's User ext., and a theme's ext attributes --\n");
    const std::map<std::string, std::string> mine = {
        { "inc", "PHP" }, { "txt", "Python" }, { "h", "C++" }, { "bak", "XML" }, { "zz", "No Such Language" },
    };
    const std::map<std::string, std::string> keys  = { { "inc", "pascal" }, { "tpl", "html" } };
    // Twilight's real stray values (bash ext="po", xml ext="wpl"), plus one for an ambiguous extension.
    const std::map<std::string, std::string> theme = { { "po", "Shell" }, { "wpl", "XML" }, { "m", "MATLAB" }, { "inc", "Perl" } };
    WxnUserExtMaps all; all.toLang = &mine; all.toKey = &keys; all.themeToLang = &theme;
    expectEq(detect("defs.inc", "", all), "PHP", "User ext. beats functionList.conf and the theme");
    expectEq(detect("notes.txt", "", all), "Python", "User ext. on an extension no table knows");
    expectEq(detect("api.h", "", all), "C++", "User ext. beats wxNote's own .h override");
    expectEq(detect("NOTES.TXT", "", all), "Python", "case-blind, like every other rule");
    expectEq(detect("x.zz", "", all), "", "a mapping to a language the menu lacks falls through");
    expectEq(detect("page.tpl", "", all), "HTML", "functionList.conf still applies under it");
    expectEq(detect("main.cpp.bak", "", all), "XML", "a mapped \"bak\" wins over looking under the suffix");
    expectEq(detect("main.cpp.orig", "", all), "C++", "an unmapped backup suffix is still looked under");

    WxnUserExtMaps themeOnly; themeOnly.themeToLang = &theme;
    expectEq(detect("messages.po", "", themeOnly), "gettext PO", "a theme's stray .po -> bash does not override detection");
    expectEq(detect("list.wpl", "", themeOnly), "XML", "a theme maps an extension nothing else places");
    expectEq(detect("plot.m", "", themeOnly), "MATLAB", "a theme settles an extension the tables leave ambiguous");
    expectEq(detect("defs.inc", "", themeOnly), "Perl", "...as does the theme for .inc");
    expectEq(detect("plot.m", "#!/usr/bin/octave\n", WxnUserExtMaps{}), "MATLAB", "without it, .m still goes by content");
}

static void testUserExtParsing()
{
    std::printf("\n-- what the User ext. field accepts --\n");
    expectEq(wxnUserExtNormalize("inc"), "inc", "plain");
    expectEq(wxnUserExtNormalize(".INC"), "inc", "leading dot, upper case");
    expectEq(wxnUserExtNormalize("*.Tpl"), "tpl", "a wildcard spelling");
    expectEq(wxnUserExtNormalize("  c++ "), "c++", "padding; punctuation that extensions use is kept");
    expectEq(wxnUserExtNormalize("tar.gz"), "", "a dotted name is not an extension");
    expectEq(wxnUserExtNormalize("a/b"), "", "a path is not an extension");
    expectEq(wxnUserExtNormalize("*"), "", "nor is a bare wildcard");
    expectEq(wxnUserExtNormalize(""), "", "nor is nothing");
    expectEq(wxnUserExtJoin(wxnUserExtParse("tpl, .INC;phtml  tpl\t*.x")), "tpl inc phtml x",
             "spaces, commas and semicolons separate; order kept; duplicates dropped");
    expectEq(wxnUserExtJoin(wxnUserExtParse("ok bad/one also.bad")), "ok", "entries that are not extensions are dropped");
}

// Every LexerType a shipped theme declares is either a language the Language menu has, or one of the
// blocks deliberately listed as not a language - so a User ext. typed under any entry has somewhere to go.
static void testNppLexerTypes()
{
    std::printf("\n-- Notepad++ LexerType names -> Language-menu names --\n");
    std::size_t n; int bad = 0;
    const WxnLangNppRow* t = wxnLangNppTable(n);
    std::set<std::string> rows;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (*t[i].lang && !g_menu.count(t[i].lang)) { ++bad; std::printf("        [%s] -> [%s] is not a menu language\n", t[i].npp, t[i].lang); }
        if (!rows.insert(t[i].npp).second) { ++bad; std::printf("        [%s] listed twice\n", t[i].npp); }
    }
    check(bad == 0, "the table names only menu languages, each LexerType once");

    namespace fs = std::filesystem;
    std::vector<fs::path> themes = { fs::u8path(std::string(RESOURCES_DIR) + "/stylers.model.xml") };
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(std::string(RESOURCES_DIR) + "/themes"), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".xml") themes.push_back(it->path());
    int files = 0, missing = 0;
    std::set<std::string> reported;
    for (const fs::path& f : themes)
    {
        const std::string xml = readFile(f.u8string());
        if (xml.empty()) continue;
        ++files;
        for (std::size_t i = xml.find("<LexerType name=\""); i != std::string::npos; i = xml.find("<LexerType name=\"", i + 1))
        {
            const std::size_t a = i + 17, b = xml.find('"', a);
            const std::string name = xml.substr(a, b - a);
            if (!rows.count(name) && reported.insert(name).second)
            { ++missing; std::printf("        %s: LexerType [%s] has no row\n", f.filename().u8string().c_str(), name.c_str()); }
        }
    }
    check(files > 20, "read " + std::to_string(files) + " shipped theme files");
    check(missing == 0, "every LexerType they declare has a row");
    expectEq(wxnLangForNppLexerType("cpp"), "C++", "cpp -> C++");
    expectEq(wxnLangForNppLexerType("javascript.js"), "JavaScript", "javascript.js -> JavaScript");
    expectEq(wxnLangForNppLexerType("javascript"), "", "javascript (embedded in HTML) -> not a language");
    expectEq(wxnLangForNppLexerType("fortran77"), "Fortran (fixed form)", "fortran77 -> Fortran (fixed form)");
    expectEq(wxnLangForNppLexerType("bash"), "Shell", "bash -> Shell");
    expectEq(wxnLangForNppLexerType("ini"), "", "ini -> none: .ini opens as Properties but comments with ';', Properties' own files with '#'");
    expectEq(wxnLangForNppLexerType("props"), "Properties", "props -> Properties");
}

// The Style Configurator's "Default ext." list, built from the engine's keys exactly as the editor does.
static void testDefaultExtensions()
{
    std::printf("\n-- \"Default ext.\": what each language opens by itself --\n");
    const std::vector<std::string> keys = g_eng->detectionKeys();
    check(keys.size() > 300, "engine lists detect()'s keys (" + std::to_string(keys.size()) + ")");
    check(std::find(keys.begin(), keys.end(), "Makefile") != keys.end(), "...whole file names among them");
    const auto def = wxnDefaultExtensions(wxnLangCandidateExts(keys), scDetect, known);
    auto has = [&](const std::string& lang, const std::string& ext) {
        const auto it = def.find(lang);
        return it != def.end() && std::find(it->second.begin(), it->second.end(), ext) != it->second.end();
    };
    check(has("Python", "py") && has("Python", "pyw"), "Python: py, pyw");
    check(has("C", "h") && !has("C++", "h"), "C: h (wxNote's override), not C++");
    check(has("Kotlin", "kt"), "Kotlin: kt (from the comment table - Scintillua has no Kotlin)");
    check(has("Fortran (fixed form)", "f") && has("Fortran (free form)", "f90"), "Fortran: f fixed form, f90 free form");
    check(has("HTML", "html") && has("PHP", "php") && has("Markdown", "md"), "HTML, PHP and Markdown have theirs");
    bool incAnywhere = false, dotted = false;
    std::set<std::string> seen; int twice = 0;
    for (const auto& kv : def)
        for (const std::string& e : kv.second)
        {
            if (e == "inc" || e == "m") incAnywhere = true;
            if (e.find('.') != std::string::npos) dotted = true;
            if (!seen.insert(e).second) ++twice;
        }
    check(!incAnywhere, "the ambiguous .inc and .m belong to no language");
    check(!dotted, "no dotted names in the lists");
    check(twice == 0, "no extension is listed under two languages");
    std::size_t total = 0; for (const auto& kv : def) total += kv.second.size();
    // 260 over 89 at the pinned lexer.lua: its 338 keys include whole names and languages wxNote cannot
    // highlight, which list nowhere. A big drop means candidates or detection broke.
    check(def.size() > 80 && total > 230, std::to_string(total) + " extensions over " + std::to_string(def.size()) + " languages");
}

// ---- the tables -------------------------------------------------------------------------------------

static void testTablesNameMenuLanguages()
{
    std::printf("\n-- every name the glue tables hand out is a Language-menu entry --\n");
    check(g_menu.size() > 100, "read wxnLangTable out of menu_data_language.h (" + std::to_string(g_menu.size()) + " languages)");
    std::size_t n; int bad = 0;
    std::set<std::string> seen;
    const WxnLangScRow* sc = wxnLangScintilluaTable(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!g_menu.count(sc[i].lang)) { ++bad; std::printf("        [%s] -> [%s] is not a menu language\n", sc[i].sc, sc[i].lang); }
        if (!seen.insert(sc[i].sc).second) { ++bad; std::printf("        [%s] listed twice\n", sc[i].sc); }
    }
    seen.clear();
    const WxnLangExtRow* ov = wxnLangExtOverrideTable(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (*ov[i].lang && !g_menu.count(ov[i].lang)) { ++bad; std::printf("        .%s -> [%s] is not a menu language\n", ov[i].ext, ov[i].lang); }
        if (wxnLangLower(ov[i].ext) != ov[i].ext) { ++bad; std::printf("        .%s is not lower-case\n", ov[i].ext); }
        if (!seen.insert(ov[i].ext).second) { ++bad; std::printf("        .%s listed twice\n", ov[i].ext); }
    }
    check(bad == 0, "Scintillua map and overrides name only menu languages, once each");
}

// The names inside lexer.lua's detect(): the right-hand sides of its local `extensions` and `patterns`
// tables, keyed by the left-hand side (extension, whole file name, or pattern).
static std::vector<std::pair<std::string, std::string>> luaTable(const std::string& lua, const char* head, const char* stop)
{
    std::vector<std::pair<std::string, std::string>> out;
    const std::size_t a = lua.find(head), b = lua.find(stop, a == std::string::npos ? 0 : a);
    if (a == std::string::npos || b == std::string::npos) return out;
    const std::string body = lua.substr(a, b - a);
    for (std::size_t eq = body.find("= '"); eq != std::string::npos; eq = body.find("= '", eq + 1))
    {
        const std::size_t v1 = eq + 3, v2 = body.find('\'', v1);
        if (v2 == std::string::npos) break;
        // key: [ 'x' ] or a bare identifier, immediately left of the '='
        std::size_t k2 = eq; while (k2 > 0 && body[k2 - 1] == ' ') --k2;
        std::string key;
        if (k2 > 0 && body[k2 - 1] == ']')
        {
            const std::size_t q2 = body.rfind('\'', k2 - 1), q1 = body.rfind('\'', q2 - 1);
            key = body.substr(q1 + 1, q2 - q1 - 1);
        }
        else
        {
            std::size_t k1 = k2;
            while (k1 > 0 && (std::isalnum(static_cast<unsigned char>(body[k1 - 1])) || body[k1 - 1] == '_')) --k1;
            key = body.substr(k1, k2 - k1);
        }
        out.emplace_back(key, body.substr(v1, v2 - v1));
    }
    return out;
}

static void testLexerLuaVocabulary()
{
    std::printf("\n-- lexer.lua still speaks the vocabulary the map was written against --\n");
    const std::string lua = readFile(std::string(SCINTILLUA_LEXER_DIR) + "/lexer.lua");
    check(lua.find("function M.detect(filename, line)") != std::string::npos, "lexer.lua has M.detect(filename, line)");
    const auto ext = luaTable(lua, "local extensions = {", "local patterns = {");
    const auto pat = luaTable(lua, "local patterns = {", "for patt, name in pairs(M.detect_patterns)");
    check(ext.size() > 300, "parsed detect()'s extension table (" + std::to_string(ext.size()) + " entries)");
    check(pat.size() > 10, "parsed detect()'s first-line patterns (" + std::to_string(pat.size()) + " entries)");

    // Scintillua languages with no Lexilla-highlighted equivalent in wxNote, reviewed one by one.
    static const std::set<std::string> kUnmapped = {
        "antlr", "apdl", "apl", "applescript", "autohotkey", "awk", "boo", "chuck", "crystal", "dot",
        "elixir", "elm", "factor", "fantom", "faust", "fennel", "fstab", "gap", "gemini", "gherkin",
        "gleam", "glsl", "gnuplot", "groovy", "gtkrc", "hare", "icon", "idl", "inform", "io_lang",
        "janet", "jq", "ledger", "lilypond", "litcoffee", "logtalk", "meson", "moonscript", "myrddin",
        "nemerle", "objeck", "odin", "org", "pico8", "pike", "pony", "prolog", "protobuf", "pure",
        "reason", "rest", "rexx", "routeros", "rpmspec", "scala", "sml", "snobol4", "spin", "taskpaper",
        "texinfo", "todotxt", "troff", "vala", "vcard", "xs", "xtend",
    };
    int unknown = 0;
    std::set<std::string> reported;
    for (const auto* t : { &ext, &pat })
        for (const auto& kv : *t)
            if (!*wxnLangForScintillua(kv.second) && !kUnmapped.count(kv.second) && reported.insert(kv.second).second)
            { ++unknown; std::printf("        Scintillua language [%s] is neither mapped nor listed as unmapped\n", kv.second.c_str()); }
    check(unknown == 0, "every language detect() can return is mapped or deliberately left as text");
}

// Wherever the comment table places a file in a MENU language, the language detection highlights it
// as must comment it the same way - otherwise Ctrl+/ writes one language's comment into a buffer
// coloured as another. (Its extension-only rows - INI's ';', SCSS's '//' - are finer than the lexer
// they share, and win in activeCommentLang anyway, so they are not compared.)
static void testHighlightMatchesComments()
{
    std::printf("\n-- highlighted language comments like the comment table says --\n");
    const std::string lua = readFile(std::string(SCINTILLUA_LEXER_DIR) + "/lexer.lua");
    const auto ext = luaTable(lua, "local extensions = {", "local patterns = {");
    int compared = 0, bad = 0;
    for (const auto& kv : ext)
        for (const std::string& file : { "x." + kv.first, kv.first })
        {
            const std::string lang = detect(file);
            const std::string key  = wxnCommentLangKeyForFileName(wxnLangLower(file));
            const WxnCommentLang* row = wxnCommentLangForKey(key);
            if (lang.empty() || !row || !g_menu.count(row->name)) continue;
            ++compared;
            const WxnCommentStyle a = row->style, b = wxnCommentStyleForKey(wxnCommentLangKeyForName(lang));
            if (std::string(a.line) != b.line || std::string(a.blockOpen) != b.blockOpen || std::string(a.blockClose) != b.blockClose)
            { ++bad; std::printf("        %-16s highlighted as [%s] but commented as [%s]\n", file.c_str(), lang.c_str(), row->name); }
        }
    check(compared > 120, "compared " + std::to_string(compared) + " names the two tables both place");
    check(bad == 0, "no file is highlighted as one language and commented as another");
}

static void testSniff()
{
    std::printf("\n-- wxnSniffExtFromContent --\n");
    expectEq(wxnSniffExtFromContent("#!/usr/bin/env  python3 -u\n"), "py", "env with two spaces before the interpreter");
    expectEq(wxnSniffExtFromContent("  <?XML version='1.0'?>"), "xml", "prolog after whitespace, any case");
    expectEq(wxnSniffExtFromContent("<HTML><body>"), "html", "<html> opener");
    expectEq(wxnSniffExtFromContent("[1, 2, 3]"), "", "a bracket without a quoted key is not JSON-shaped");
    expectEq(wxnSniffExtFromContent("[{\"k\": 1}]"), "json", "array of objects");
    expectEq(wxnSniffExtFromContent(""), "", "empty buffer");
}

int main()
{
    std::printf("lang_detect_test\n");
    loadMenuNames();
    scintillua::Engine eng(SCINTILLUA_LEXER_DIR, BUILD_DIR);
    g_eng = &eng;
    testEngine();
    if (!eng.ok()) { std::printf("\nengine unavailable - cannot continue\n%d passed, %d failed\n", g_pass, g_fail + 1); return 1; }
    testEverydayFiles();
    testFileNames();
    testCaseAndBackups();
    testOverrides();
    testKnownButUnhighlightable();
    testFirstLine();
    testUserMapping();
    testStyleConfiguratorExtensions();
    testUserExtParsing();
    testNppLexerTypes();
    testDefaultExtensions();
    testTablesNameMenuLanguages();
    testLexerLuaVocabulary();
    testHighlightMatchesComments();
    testSniff();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
