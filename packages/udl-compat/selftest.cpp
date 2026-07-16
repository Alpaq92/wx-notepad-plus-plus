// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone self-test for the UDL -> Scintillua translator. Builds and runs with
// no wxWidgets / Scintilla / Lua dependency (just the C++ standard library), so it
// can gate the pure translation logic in CI before the runtime engine exists.
//   cl /std:c++17 /EHsc selftest.cpp udl_scintillua.cpp && selftest.exe
// Exit code 0 = all checks passed.

#include "udl_scintillua.h"
#include "udl_parse.h"

#include <cstdio>
#include <string>

static int g_fail = 0;

static void check(bool cond, const char* what)
{
    if (!cond) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

static bool contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    using namespace udlcompat;

    // A small but representative UDL: two keyword groups (one case-insensitive
    // request), a line + block comment, a double- and single-quoted string, an
    // apostrophe-in-a-keyword edge case, numbers, and operators.
    UdlDef u;
    u.name = "My Lang 1!";                         // deliberately not a clean identifier
    u.keywords[0] = {"if", "else", "while"};
    u.keywords[2] = {"true", "false", "it's"};     // Keywords3, with an apostrophe
    u.keywordsCaseInsensitive = true;
    u.lineComment = "//";
    u.blockCommentOpen = "/*";
    u.blockCommentClose = "*/";
    u.delimiters = { {"\"", "\"", ""}, {"'", "'", ""} };
    u.operators = "+-*/=<>";
    u.hasNumbers = true;

    const std::string lua = translateUdlToScintillua(u);
    std::printf("---- generated lexer ----\n%s\n-------------------------\n", lua.c_str());

    // Structure
    check(contains(lua, "local M = lexer.new('My_Lang_1_')"), "name sanitized to identifier");
    check(contains(lua, "M:add_rule('whitespace'"), "whitespace rule present");
    check(contains(lua, "return M"), "returns the lexer");

    // Keywords: group 1 -> lexer.KEYWORD, group 3 -> its own 'keyword3' tag, both case-insensitive
    check(contains(lua, "M:tag(lexer.KEYWORD, lexer.word_match('if else while', true))"),
          "Keywords1 -> KEYWORD, case-insensitive");
    check(contains(lua, "M:add_rule('keyword3', M:tag('keyword3', lexer.word_match("),
          "Keywords3 -> distinct keyword3 tag");
    check(!contains(lua, "M:add_rule('keyword2'"), "empty Keywords2 emits nothing");

    // Lua escaping: the apostrophe in "it's" must be backslash-escaped in the Lua literal.
    check(contains(lua, "true false it\\'s"), "apostrophe escaped in word_match literal");

    // Comments: one rule combining to_eol + range
    check(contains(lua, "M:tag(lexer.COMMENT, lexer.to_eol('//') + lexer.range('/*', '*/'))"),
          "line + block comment combined");

    // Strings: two ranges combined
    check(contains(lua, "M:tag(lexer.STRING, lexer.range('\"', '\"') + lexer.range('\\'', '\\''))"),
          "double + single quoted string ranges");

    // Numbers + operators
    check(contains(lua, "M:tag(lexer.NUMBER, lexer.number)"), "number rule");
    check(contains(lua, "M:tag(lexer.OPERATOR, lpeg.S('+-*/=<>'))"), "operator rule");

    // A minimal UDL (name only) must still yield a valid, returnable lexer.
    UdlDef empty;
    empty.name = "bare";
    empty.hasNumbers = false;
    const std::string bare = translateUdlToScintillua(empty);
    check(contains(bare, "lexer.new('bare')") && contains(bare, "return M"),
          "empty UDL still yields a returnable lexer");
    check(!contains(bare, "word_match") && !contains(bare, "lexer.number"),
          "empty UDL emits no keyword/number rules");

    // Regression (code-review): a block-comment OPEN marker with NO matching CLOSE (and no line
    // comment) must not emit a comment rule at all - never an empty `M:tag(lexer.COMMENT, )`, which
    // is invalid Lua and would make the whole generated lexer fail to load.
    UdlDef openOnly;
    openOnly.name = "openonly";
    openOnly.blockCommentOpen = "/*";           // deliberately no blockCommentClose, no lineComment
    const std::string oo = translateUdlToScintillua(openOnly);
    check(!contains(oo, "lexer.COMMENT"), "block-open with no close emits no comment rule");
    check(!contains(oo, "M:tag(lexer.COMMENT, )"), "no empty comment-tag argument emitted");
    // ...but a line comment alone, or a complete block pair, still emits the rule.
    UdlDef lineOnly; lineOnly.name = "lineonly"; lineOnly.lineComment = "#";
    check(contains(translateUdlToScintillua(lineOnly), "lexer.to_eol('#')"), "line-only comment still emitted");

    // ---- parse a realistic userDefineLang.xml, then translate it end to end ----
    const std::string xml =
        "<NotepadPlus>\n"
        "  <UserLang name=\"MyLang\" ext=\"myl ml\" udlVersion=\"2.1\">\n"
        "    <Settings>\n"
        "      <Global caseIgnored=\"yes\" allowFoldOfComments=\"no\" foldCompact=\"no\" forcePureLC=\"0\" decimalSeparator=\"0\" />\n"
        "      <Prefix Keywords1=\"no\" />\n"
        "    </Settings>\n"
        "    <KeywordLists>\n"
        "      <Keywords name=\"Comments\">00// 01/* 02*/</Keywords>\n"
        "      <Keywords name=\"Operators1\">+ - * / = &lt; &gt;</Keywords>\n"
        "      <Keywords name=\"Delimiters\">00&quot; 01\\ 02&quot; 03&apos; 04 05&apos;</Keywords>\n"
        "      <Keywords name=\"Keywords1\">if else while for</Keywords>\n"
        "      <Keywords name=\"Keywords2\">true false null</Keywords>\n"
        "    </KeywordLists>\n"
        "    <Styles><WordsStyle name=\"DEFAULT\" fgColor=\"000000\" bgColor=\"FFFFFF\" /></Styles>\n"
        "  </UserLang>\n"
        "</NotepadPlus>\n";

    UdlDef parsed;
    std::string perr;
    const bool ok = parseUserDefineLangXml(xml, parsed, &perr);
    check(ok, "parse succeeds");
    if (!ok) std::printf("  parse error: %s\n", perr.c_str());

    check(parsed.name == "MyLang", "parsed name");
    check(parsed.keywordsCaseInsensitive, "parsed caseIgnored=yes");
    check(parsed.keywords[0].size() == 4 && parsed.keywords[0][0] == "if" && parsed.keywords[0][3] == "for",
          "parsed Keywords1");
    check(parsed.keywords[1].size() == 3 && parsed.keywords[1][2] == "null", "parsed Keywords2");
    check(parsed.lineComment == "//" && parsed.blockCommentOpen == "/*" && parsed.blockCommentClose == "*/",
          "parsed + decoded Comments (00/01/02)");
    check(parsed.operators == "+-*/=<>", "parsed Operators1 to char set (entities unescaped)");
    // Delimiters: set0 open=\" close=\" (escape=\\), set1 open=' close=' -> 2 spans
    check(parsed.delimiters.size() == 2, "parsed Delimiters -> 2 sets with a non-empty open");
    if (parsed.delimiters.size() == 2) {
        check(parsed.delimiters[0].open == "\"" && parsed.delimiters[0].close == "\"" && parsed.delimiters[0].escape == "\\",
              "delimiter set 0 open/close/escape decoded (index/3=set, index%3=slot)");
        check(parsed.delimiters[1].open == "'" && parsed.delimiters[1].close == "'",
              "delimiter set 1 open/close decoded");
    }

    const std::string lua2 = translateUdlToScintillua(parsed);
    check(contains(lua2, "lexer.new('MyLang')"), "e2e: lexer name");
    check(contains(lua2, "lexer.word_match('if else while for', true)"), "e2e: Keywords1 case-insensitive");
    check(contains(lua2, "lexer.word_match('true false null', true)"), "e2e: Keywords2 case-insensitive");
    check(contains(lua2, "lexer.to_eol('//') + lexer.range('/*', '*/')"), "e2e: comments");
    check(contains(lua2, "lexer.range('\"', '\"', false, true) + lexer.range('\\'', '\\'')"),
          "e2e: two string delimiters (backslash-escaped double-quote, plain single-quote)");
    check(contains(lua2, "lpeg.S('+-*/=<>')"), "e2e: operators");

    // Bare <UserLang> root (single-language export) must also parse.
    UdlDef bareParsed;
    check(parseUserDefineLangXml("<UserLang name=\"Solo\" ext=\"s\"><KeywordLists>"
                                 "<Keywords name=\"Keywords1\">x y</Keywords></KeywordLists></UserLang>",
                                 bareParsed) &&
              bareParsed.name == "Solo" && bareParsed.keywords[0].size() == 2,
          "bare <UserLang> root parses");

    // Malformed input fails cleanly.
    UdlDef junk;
    check(!parseUserDefineLangXml("not xml at all", junk), "non-UDL input rejected");

    if (g_fail == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_fail);
    return 1;
}
