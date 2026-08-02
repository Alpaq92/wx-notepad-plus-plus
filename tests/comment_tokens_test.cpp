// SPDX-License-Identifier: Apache-2.0
//
// comment_tokens_test - the per-language comment table behind Ctrl+/ and Stream Comment
// (src/comment_tokens.h), driven against plain strings with no editor.
//
// Until that table existed, every comment command wrote "//" and "/* */" into whatever buffer was
// in front. Ctrl+/ in a .py, .lua, .sql, .yaml or .ps1 file inserted "//" - not a comment in any of
// them - and nothing warned; the file was just quietly broken. What this suite exists to stop is
// that regression coming back one language at a time, so the token for each language family is
// pinned by NAME here, not merely spot-checked.
//
// Same shape as funclist_selftest: free functions over sample strings. It is cheaper, though -
// comment_tokens.h is a standalone header with no wx and no Scintilla, so unlike that suite this
// one does not embed main.cpp and links nothing at all.
//
//   cmake --build build --target comment_tokens_test && build/bin/comment_tokens_test
//
#include "comment_tokens.h"

#include <cstdio>
#include <fstream>
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

// ---- the checks a caller actually makes -----------------------------------------------------------

// The line-comment token a key hands out, in the exact form the editor inserts (Batch's "rem" and
// Forth's "\" carry their mandatory trailing space). "" = the language has no line-comment form.
static std::string lineTok(const std::string& key)
{ return wxnLineCommentInsert(wxnCommentStyleForKey(key)); }

// "open|close", or "" when the language has no block form.
static std::string blockTok(const std::string& key)
{
    const WxnCommentStyle cs = wxnCommentStyleForKey(key);
    return cs.hasBlock() ? std::string(cs.blockOpen) + "|" + cs.blockClose : std::string();
}

static void expectLine(const std::string& key, const std::string& want)
{ expectEq(lineTok(key), want, "line comment: " + key + " -> " + (want.empty() ? "(none)" : want)); }

static void expectBlock(const std::string& key, const std::string& want)
{ expectEq(blockTok(key), want, "block comment: " + key + " -> " + (want.empty() ? "(none)" : want)); }

static void expectFile(const std::string& base, const std::string& wantKey)
{ expectEq(wxnCommentLangKeyForFileName(base), wantKey, "file: " + base + " -> " + (wantKey.empty() ? "(unknown)" : wantKey)); }

// ---- the token table, per language ----------------------------------------------------------------

static void testHashLanguages()
{
    std::printf("\n-- '#' languages --\n");
    for (const char* k : { "python", "ruby", "perl", "yaml", "toml", "sh", "makefile", "cmake",
                           "dockerfile", "r", "nix", "powershell", "tcl", "raku", "elixir",
                           "gdscript", "nim", "julia", "conf", "props", "po", "coffeescript",
                           "mysql", "avs", "nncrontab" })
        expectLine(k, "#");
    // The bug report's exact reproduction list: none of these may ever hand back "//".
    for (const char* k : { "python", "lua", "sql", "yaml", "powershell" })
        check(lineTok(k) != "//", std::string("Ctrl+/ in a ") + k + " buffer does not insert //");
}

static void testDashLanguages()
{
    std::printf("\n-- '--' languages --\n");
    for (const char* k : { "sql", "lua", "haskell", "ada", "vhdl", "eiffel", "mssql", "asn1" })
        expectLine(k, "--");
    expectBlock("lua", "--[[|]]");
    expectBlock("haskell", "{-|-}");
    expectBlock("sql", "/*|*/");
    expectBlock("ada", "");        // Ada has no block form at all
    expectBlock("vhdl", "");
}

static void testSlashLanguages()
{
    std::printf("\n-- '//' languages --\n");
    for (const char* k : { "c", "cpp", "cs", "java", "js", "typescript", "go", "rust", "swift",
                           "kotlin", "dart", "zig", "objc", "php", "scala", "groovy", "d",
                           "verilog", "json5", "rc", "stata", "asciidoc" })
        expectLine(k, "//");
    for (const char* k : { "c", "cpp", "cs", "java", "js", "typescript", "go", "rust", "swift",
                           "kotlin", "dart", "objc", "php" })
        expectBlock(k, "/*|*/");
    expectBlock("zig", "");        // Zig deliberately has no block comment
}

static void testOtherFamilies()
{
    std::printf("\n-- other comment characters --\n");
    for (const char* k : { "vb", "vbscript", "asp", "freebasic" })  expectLine(k, "'");
    for (const char* k : { "lisp", "scheme", "asm", "ini", "registry", "au3", "nsis", "inno",
                           "purebasic", "blitzbasic", "csound", "rebol", "hollywood", "kix" })
        expectLine(k, ";");
    for (const char* k : { "tex", "latex", "metapost", "erlang", "matlab", "octave", "bib",
                           "ps", "mmixal", "txt2tags", "visualprolog" })
        expectLine(k, "%");
    // HTML-family: NO line comment, only the <!-- --> pair. Ctrl+/ wraps each line in it.
    for (const char* k : { "html", "xml", "markdown" }) { expectLine(k, ""); expectBlock(k, "<!--|-->"); }
    expectBlock("jsp", "<%--|--%>");     // an HTML comment would not nest inside a JSP scriptlet
    expectLine("cobol", "*>");
    expectLine("clarion", "!");
    expectLine("baan", "|");             // LexBaan.cxx
    expectLine("fortran", "!");
    expectLine("spice", "*");
    expectLine("batch", "rem ");         // a word token: inserted WITH its separating space
    expectLine("forth", "\\ ");
    expectBlock("powershell", "<#|#>");
    expectBlock("caml", "(*|*)");
    expectBlock("fsharp", "(*|*)");
    expectBlock("smalltalk", "\"|\"");
    expectBlock("julia", "#=|=#");
    expectBlock("cmake", "#[[|]]");
}

// The regression that motivates keeping CSS separate from its preprocessors: "//" is NOT a comment
// in plain CSS, so a shared "css" row would corrupt real stylesheets.
static void testCssIsNotScss()
{
    std::printf("\n-- CSS vs SCSS/LESS --\n");
    expectLine("css", "");
    expectBlock("css", "/*|*/");
    expectLine("scss", "//");
    expectLine("less", "//");
}

// Languages with no comment form at all must resolve to the empty style, which is what makes the
// callers report "nothing was changed" instead of inserting something wrong.
static void testLanguagesWithNoComments()
{
    std::printf("\n-- languages with no comment form --\n");
    for (const char* k : { "json", "diff", "ihex", "srec", "tehex" })
        check(wxnCommentStyleForKey(k).empty(), std::string(k) + " has no comment form (buffer left untouched)");
    check(wxnCommentStyleForKey("").empty(),           "an empty key resolves to the empty style");
    check(wxnCommentStyleForKey("nonesuch").empty(),   "an unknown key resolves to the empty style");
    check(wxnCommentLangForKey("nonesuch") == nullptr, "an unknown key has no row");
}

// ---- resolving a buffer to a language -------------------------------------------------------------

static void testFileNameResolution()
{
    std::printf("\n-- file name -> language key --\n");
    expectFile("main.cpp", "cpp");
    expectFile("main.c", "c");
    expectFile("script.py", "python");
    expectFile("init.lua", "lua");
    expectFile("schema.sql", "sql");
    expectFile("config.yaml", "yaml");
    expectFile("config.yml", "yaml");
    expectFile("build.ps1", "powershell");
    expectFile("styles.css", "css");
    expectFile("styles.scss", "scss");
    expectFile("page.html", "html");
    expectFile("readme.md", "markdown");
    // Extension-less names whose LANGUAGE is the file name.
    expectFile("makefile", "makefile");
    expectFile("gnumakefile", "makefile");
    expectFile("dockerfile", "dockerfile");
    expectFile("dockerfile.dev", "dockerfile");     // prefix-matched: the "extension" here is "dev"
    expectFile("cmakelists.txt", "cmake");
    expectFile("gemfile", "ruby");
    // Dotfiles: a leading dot is part of the name, not an extension separator.
    expectFile(".bashrc", "sh");
    expectFile(".gitignore", "conf");
    // An explicit extension always beats the name rules.
    expectFile("makefile.py", "python");
    // Unknown, and deliberately unknown.
    expectFile("notes.txt", "");
    expectFile("data.bin", "");
    check(wxnCommentLangKeyForFileName("model.m").empty(),
          "'.m' stays unmapped: MATLAB ('%') and Objective-C ('//') both claim it");
    // .ini keeps ';' while .conf/.cfg take '#' - flLangKey lumps them into one bucket, this must not.
    expectEq(lineTok(wxnCommentLangKeyForFileName("app.ini")),  ";", "app.ini  comments with ;");
    expectEq(lineTok(wxnCommentLangKeyForFileName("app.conf")), "#", "app.conf comments with #");
    expectEq(lineTok(wxnCommentLangKeyForFileName("app.toml")), "#", "app.toml comments with #");
}

// A Language-menu pick hands back wxnLangTable's `name` verbatim, so every display name that menu
// can produce has to resolve here. These are the exact spellings from menu_data_language.h.
static void testMenuNameResolution()
{
    std::printf("\n-- Language-menu name -> language key --\n");
    expectEq(wxnCommentLangKeyForName("Python"),               "python",   "menu 'Python'");
    expectEq(wxnCommentLangKeyForName("C++"),                  "cpp",      "menu 'C++'");
    expectEq(wxnCommentLangKeyForName("C#"),                   "cs",       "menu 'C#'");
    expectEq(wxnCommentLangKeyForName("Shell"),                "sh",       "menu 'Shell'");
    expectEq(wxnCommentLangKeyForName("MS SQL"),               "mssql",    "menu 'MS SQL'");
    expectEq(wxnCommentLangKeyForName("Fortran (free form)"),  "fortran",  "menu 'Fortran (free form)'");
    expectEq(wxnCommentLangKeyForName("ABL (OpenEdge)"),       "abl",      "menu 'ABL (OpenEdge)'");
    expectEq(wxnCommentLangKeyForName("gettext PO"),           "po",       "menu 'gettext PO'");
    expectEq(wxnCommentLangKeyForName("Normal text file"),     "",         "forced Normal Text resolves to no language");
    expectEq(wxnCommentLangKeyForName(""),                     "",         "an empty name resolves to no language");
}

// ---- "is this line already commented?" -------------------------------------------------------------

static void testLineCommentDetection()
{
    std::printf("\n-- uncomment detection --\n");
    const WxnCommentStyle py = wxnCommentStyleForKey("python");
    const WxnCommentStyle c  = wxnCommentStyleForKey("cpp");
    const WxnCommentStyle bt = wxnCommentStyleForKey("batch");

    check(wxnLineCommentLen("#x", 0, py) == 1,          "'#x' -> strip 1 (the token alone)");
    check(wxnLineCommentLen("# x", 0, py) == 2,         "'# x' -> strip 2 (token + the one space we may have inserted)");
    check(wxnLineCommentLen("#  x", 0, py) == 2,        "'#  x' -> strip 2: only ONE space is ours, the rest is the user's");
    check(wxnLineCommentLen("#", 0, py) == 1,           "a bare '#' line is commented");
    check(wxnLineCommentLen("x = 1", 0, py) == 0,       "an uncommented line strips nothing");
    check(wxnLineCommentLen("    # x", 4, py) == 2,     "detection starts at the first non-blank, so indentation survives");
    check(wxnLineCommentLen("    # x", 0, py) == 0,     "...and does not match against the leading whitespace");
    check(wxnLineCommentLen("// x", 0, py) == 0,        "Python does not consider a '//' line commented");
    check(wxnLineCommentLen("# x", 0, c) == 0,          "C++ does not consider a '#' line commented (that is a preprocessor directive)");
    check(wxnLineCommentLen("//x", 0, c) == 2,          "'//x' -> strip 2");
    check(wxnLineCommentLen("/// doc", 0, c) == 2,      "a doc comment keeps its extra slash when uncommented");

    // Batch's "rem" is a word: case-insensitive, and only a comment when whitespace follows.
    check(wxnLineCommentLen("rem x", 0, bt) == 4,       "batch 'rem x' -> strip 4");
    check(wxnLineCommentLen("REM x", 0, bt) == 4,       "batch 'REM x' matches case-insensitively");
    check(wxnLineCommentLen("rem", 0, bt) == 3,         "batch bare 'rem' is a comment");
    check(wxnLineCommentLen("remark = 1", 0, bt) == 0,  "batch 'remark' is NOT a commented 'ark'");

    // The empty style must never claim a line is commented, whatever it holds.
    const WxnCommentStyle none = wxnCommentStyleForKey("json");
    check(wxnLineCommentLen("// x", 0, none) == 0,      "a language with no line form never reports a commented line");
    check(wxnLineCommentLen("", 0, py) == 0,            "an empty line is not commented");
    check(wxnLineCommentLen("#", 5, py) == 0,           "an out-of-range offset is handled, not read past");
}

// ---- the per-line edit plan, applied ---------------------------------------------------------------

// What main.cpp's applyLineComments does to one line, minus the Scintilla calls. The plan itself
// comes from wxnPlanLineComment - the SAME call the editor makes - so these expectations are about
// the shipped decision logic, not a re-implementation of it. The five lines below are exactly the
// mechanical half that stays in the editor: strip the tail before the head, insert the close before
// the open, both offsets taken from the plan.
static std::string applyToLine(const std::string& lineNoEol, const std::string& key, WxnCommentMode mode)
{
    const WxnLineCommentEdit e = wxnPlanLineComment(lineNoEol, wxnCommentStyleForKey(key), mode);
    if (!e.applies) return lineNoEol;
    std::string s = lineNoEol;
    if (e.commented) { if (e.tailLen) s.erase(e.end - e.tailLen, e.tailLen); s.erase(e.at, e.headLen); }
    else             { if (!e.close.empty()) s.insert(e.end, e.close); s.insert(e.at, e.open); }
    return s;
}

static void expectLineEdit(const std::string& in, const std::string& key, WxnCommentMode mode,
                           const std::string& want, const std::string& what)
{ expectEq(applyToLine(in, key, mode), want, what); }

static void testLineEditPlan()
{
    std::printf("\n-- commenting one line, end to end --\n");
    // The exact reproductions from the bug report: none of these may gain a "//".
    expectLineEdit("def f():", "python", WxnCommentToggle, "#def f():", "python: 'def f():' -> '#def f():'");
    expectLineEdit("local x = 1", "lua", WxnCommentToggle, "--local x = 1", "lua: takes '--', not '//'");
    expectLineEdit("select 1", "sql", WxnCommentToggle, "--select 1", "sql: takes '--', not '//'");
    expectLineEdit("$x = 1", "powershell", WxnCommentToggle, "#$x = 1", "powershell: takes '#', not '//'");
    expectLineEdit("key: value", "yaml", WxnCommentToggle, "#key: value", "yaml: takes '#', not '//'");
    expectLineEdit("int x;", "cpp", WxnCommentToggle, "//int x;", "cpp: still takes '//'");

    // Indentation and trailing whitespace are structure, not content: neither moves.
    expectLineEdit("    return 1", "python", WxnCommentToggle, "    #return 1",
                   "the token goes after the indent, so the indentation survives");
    expectLineEdit("    x   ", "python", WxnCommentToggle, "    #x   ",
                   "trailing whitespace is left exactly as it was");

    // Round-trip: comment then uncomment must be the identity.
    for (const char* k : { "python", "cpp", "lua", "sql", "yaml", "batch", "html", "css", "forth" })
    {
        const std::string src = "  value = 1  ";
        const std::string there = applyToLine(src, k, WxnCommentAdd);
        const std::string back  = applyToLine(there, k, WxnCommentRemove);
        check(back == src && there != src, std::string(k) + ": comment then uncomment is the identity");
        if (back != src || there == src) std::printf("        [%s] -> [%s] -> [%s]\n", src.c_str(), there.c_str(), back.c_str());
    }

    // No line form, but a block form: wrap the line instead. The closing token lands at the last
    // non-blank, never after the trailing whitespace (and never after the EOL - the caller strips it).
    expectLineEdit("  <p>hi</p>", "html", WxnCommentToggle, "  <!--<p>hi</p>-->", "html: wraps the line in <!-- -->");
    expectLineEdit("  <!--<p>hi</p>-->", "html", WxnCommentToggle, "  <p>hi</p>", "html: unwraps exactly");
    expectLineEdit("a { color: red }", "css", WxnCommentToggle, "/*a { color: red }*/", "css: wraps in /* */");
    expectLineEdit("/*a*/", "css", WxnCommentToggle, "a", "css: unwraps /* */");

    // Blank lines are skipped so an empty line never collects a stray token.
    check(!wxnPlanLineComment("", wxnCommentStyleForKey("python"), WxnCommentToggle).applies,
          "an empty line is skipped");
    check(!wxnPlanLineComment("   \t ", wxnCommentStyleForKey("python"), WxnCommentToggle).applies,
          "a whitespace-only line is skipped");

    // add/remove are idempotent where toggle flips.
    check(!wxnPlanLineComment("#a", wxnCommentStyleForKey("python"), WxnCommentAdd).applies,
          "add: an already-commented line is left alone");
    check(!wxnPlanLineComment("a", wxnCommentStyleForKey("python"), WxnCommentRemove).applies,
          "remove: an uncommented line is left alone");
    expectLineEdit("#a", "python", WxnCommentToggle, "a", "toggle: a commented line flips back");

    // A language with no comment form at all must produce no edit, whatever the mode.
    for (WxnCommentMode m : { WxnCommentToggle, WxnCommentAdd, WxnCommentRemove })
        check(!wxnPlanLineComment("{\"a\": 1}", wxnCommentStyleForKey("json"), m).applies,
              "json: no edit is ever planned (the buffer is left untouched)");
    check(!wxnPlanLineComment("hello", wxnCommentStyleForKey("nonesuch"), WxnCommentToggle).applies,
          "an unknown language: no edit is ever planned");

    // Batch's word token, through the full path.
    expectLineEdit("echo hi", "batch", WxnCommentToggle, "rem echo hi", "batch: inserts 'rem ' with its space");
    expectLineEdit("REM echo hi", "batch", WxnCommentToggle, "echo hi", "batch: uncomments 'REM ' too");
    expectLineEdit("remark=1", "batch", WxnCommentToggle, "rem remark=1", "batch: 'remark=1' is code, so it gets commented");
}

// ---- auto-indent: which languages let a trailing ':' open a block ---------------------------------

static void testColonOpensBlock()
{
    std::printf("\n-- ':' opens an indented block --\n");
    for (const char* k : { "python", "yaml", "gdscript", "nim", "coffeescript" })
        check(wxnCommentStyleForKey(k).colonOpensBlock, std::string(k) + ": a trailing ':' indents the next line");
    // The regression this flag fixes: `public:` / `case x:` / a split ternary used to push the
    // following C++ line out a level and leave it there.
    for (const char* k : { "cpp", "c", "cs", "java", "js", "go", "rust", "sh", "sql", "markdown" })
        check(!wxnCommentStyleForKey(k).colonOpensBlock, std::string(k) + ": a trailing ':' does NOT indent");
    check(!wxnCommentStyleForKey("").colonOpensBlock,
          "plain text: a trailing ':' does NOT indent (every line ending in 'Note:' used to)");
}

// ---- the table's own invariants --------------------------------------------------------------------

static void testTableWellFormed()
{
    std::printf("\n-- table invariants --\n");
    std::size_t n; const WxnCommentLang* t = wxnCommentLangTable(n);
    check(n > 100, "the table covers the built-in languages (" + std::to_string(n) + " rows)");
    int dupKeys = 0, dupNames = 0, badStyle = 0, emptyField = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!t[i].key || !t[i].key[0] || !t[i].name || !t[i].name[0]) { ++emptyField; continue; }
        for (std::size_t j = i + 1; j < n; ++j)
        {
            if (std::string(t[i].key)  == t[j].key)  { ++dupKeys;  std::printf("        duplicate key  [%s]\n", t[i].key); }
            if (std::string(t[i].name) == t[j].name) { ++dupNames; std::printf("        duplicate name [%s]\n", t[i].name); }
        }
        // A half-declared block pair would make the stream command insert an opener with no closer.
        const WxnCommentStyle& s = t[i].style;
        const bool o = s.blockOpen && s.blockOpen[0], c = s.blockClose && s.blockClose[0];
        if (o != c) { ++badStyle; std::printf("        half a block pair on [%s]\n", t[i].key); }
    }
    check(emptyField == 0, "every row has a key and a display name");
    check(dupKeys == 0,    "keys are unique (a duplicate would shadow a language silently)");
    check(dupNames == 0,   "display names are unique (they are how a Language-menu pick resolves)");
    check(badStyle == 0,   "no row declares half a block-comment pair");

    // Every key is reachable both ways round.
    int unreachable = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (wxnCommentLangKeyForName(t[i].name) != t[i].key ||
            wxnCommentLangForKey(t[i].key) == nullptr) { ++unreachable; std::printf("        unreachable [%s]\n", t[i].key); }
    check(unreachable == 0, "every row resolves by key and by display name");
}

// Every key the extension table names must exist as a row - a typo there would silently downgrade
// that file type to "no comment syntax" instead of failing loudly.
static void testEveryMappedExtensionHasARow()
{
    std::printf("\n-- extension table points at real rows --\n");
    static const char* kProbes[] = {
        "a.c", "a.h", "a.cpp", "a.cc", "a.cxx", "a.hpp", "a.hh", "a.ino", "a.cs", "a.java", "a.rc",
        "a.mm", "a.js", "a.jsx", "a.mjs", "a.ts", "a.tsx", "a.json", "a.json5", "a.py", "a.pyw",
        "a.rb", "a.rake", "a.pl", "a.pm", "a.raku", "a.php", "a.go", "a.rs", "a.swift", "a.kt",
        "a.dart", "a.zig", "a.d", "a.jl", "a.nim", "a.scala", "a.groovy", "a.ex", "a.lua", "a.sql",
        "a.sh", "a.bash", "a.zsh", "a.fish", "a.ps1", "a.bat", "a.cmd", "a.yml", "a.toml", "a.ini",
        "a.reg", "a.cfg", "a.conf", "a.properties", "a.md", "a.adoc", "a.html", "a.vue", "a.xml",
        "a.svg", "a.plist", "a.jsp", "a.asp", "a.css", "a.scss", "a.sass", "a.less", "a.mk",
        "a.cmake", "a.r", "a.nix", "a.hs", "a.ada", "a.vhd", "a.sv", "a.erl", "a.tex", "a.bib",
        "a.mp", "a.lisp", "a.el", "a.clj", "a.scm", "a.rkt", "a.asm", "a.s", "a.vb", "a.bas",
        "a.vbs", "a.f", "a.f90", "a.pas", "a.ml", "a.fs", "a.tcl", "a.matlab", "a.coffee", "a.gd",
        "a.po", "a.e", "a.st", "a.cob", "a.do", "a.sas", "a.mac", "a.pov", "a.clw", "a.il", "a.nsi",
        "a.iss", "a.au3", "a.avs", "a.diff", "a.hex", "a.srec", "a.mod", "a.as", "a.eps",
    };
    int dangling = 0;
    for (const char* probe : kProbes)
    {
        const std::string key = wxnCommentLangKeyForFileName(probe);
        if (key.empty() || !wxnCommentLangForKey(key))
        { ++dangling; std::printf("        %s -> [%s] has no row\n", probe, key.c_str()); }
    }
    check(dangling == 0, "every extension the table maps resolves to a real language row");
}

// Every language the Language menu can put on a buffer must have a row here, or picking it leaves
// Ctrl+/ doing nothing with no way for the user to tell why. wxnLangTable is the source of truth for
// that list, so read menu_data_language.h as DATA (the catalog_selftest pattern, SRC_DIR injected by
// CMake) rather than including it - it needs wx, and this suite deliberately links nothing.
static void testEveryMenuLanguageHasTokens()
{
    std::printf("\n-- every Language-menu entry has comment tokens --\n");
    const std::string path = std::string(SRC_DIR) + "/menu_data_language.h";
    std::ifstream in(path, std::ios::binary);
    if (!in) { check(false, "read " + path); return; }
    std::ostringstream ss; ss << in.rdbuf();
    const std::string text = ss.str();

    // Rows look like:  { kCmdLangPython,  "Python",  "python" },
    int found = 0, missing = 0;
    for (std::size_t i = text.find("{ kCmdLang"); i != std::string::npos; i = text.find("{ kCmdLang", i + 1))
    {
        const std::size_t q1 = text.find('"', i);
        const std::size_t eol = text.find('\n', i);
        if (q1 == std::string::npos || (eol != std::string::npos && q1 > eol)) continue;   // no name on this row
        const std::size_t q2 = text.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        const std::string name = text.substr(q1 + 1, q2 - q1 - 1);
        ++found;
        if (wxnCommentLangKeyForName(name).empty())
        { ++missing; std::printf("        no comment tokens for menu language [%s]\n", name.c_str()); }
    }
    check(found > 100, "parsed wxnLangTable out of menu_data_language.h (" + std::to_string(found) + " languages)");
    check(missing == 0, "every Language-menu entry resolves to a comment-token row");
}

int main()
{
    std::printf("comment_tokens_test\n");
    testHashLanguages();
    testDashLanguages();
    testSlashLanguages();
    testOtherFamilies();
    testCssIsNotScss();
    testLanguagesWithNoComments();
    testFileNameResolution();
    testMenuNameResolution();
    testLineCommentDetection();
    testLineEditPlan();
    testColonOpensBlock();
    testTableWellFormed();
    testEveryMappedExtensionHasARow();
    testEveryMenuLanguageHasTokens();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
