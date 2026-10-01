// SPDX-License-Identifier: Apache-2.0
//
// lang_detect_test - which language a file opens as (src/lang_detect.h), driven through the REAL
// Scintillua engine and the lexer.lua the build fetched, against the real Language-menu table.
//
// Detection used to be a 16-group extension table: .html, .md, .toml, Makefile and Dockerfile all
// opened as plain text, and .go was handed to a Lexilla lexer that does not exist. It is now
// Scintillua's lexer.detect() behind wxNote's overrides, so this suite pins:
//   * the answers for the files people actually open - by name, by first line, by user mapping;
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

#include <cctype>
#include <cstdio>
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

static std::string detect(const std::string& file, const std::string& head = std::string(),
                          const std::map<std::string, std::string>* user = nullptr)
{
    auto sc    = [](const std::string& f, const std::string& l) { return g_eng->detect(f, l); };
    auto known = [](const std::string& n) { return g_menu.count(n) != 0; };
    return wxnDetectLanguage(file, head, user, sc, known);
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
    const std::map<std::string, std::string> user = {
        { "foo", "python" }, { "cfg", "ini" }, { "tmpl", "html" }, { "h", "cpp" }, { "zz", "no-such-key" },
    };
    expectEq(detect("a.foo", "", &user), "Python", "\"a.foo\" mapped to python -> Python");
    expectEq(detect("A.FOO", "", &user), "Python", "\"A.FOO\" - the mapping is case-blind like the rest");
    expectEq(detect("a.cfg", "", &user), "Properties", "\"a.cfg\" mapped to ini -> Properties");
    expectEq(detect("a.tmpl", "", &user), "HTML", "\"a.tmpl\" mapped to html -> HTML");
    expectEq(detect("a.h", "", &user), "C++", "\"a.h\" mapped to cpp beats wxNote's own .h override");
    expectEq(detect("a.zz", "", &user), "", "\"a.zz\" mapped to an unknown key falls through (to nothing here)");
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
    testTablesNameMenuLanguages();
    testLexerLuaVocabulary();
    testHighlightMatchesComments();
    testSniff();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
