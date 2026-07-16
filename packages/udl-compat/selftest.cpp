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
        "      <Keywords name=\"Folders in code1, open\">{</Keywords>\n"
        "      <Keywords name=\"Folders in code1, close\">}</Keywords>\n"
        "      <Keywords name=\"Folders in code2, open\">if</Keywords>\n"
        "      <Keywords name=\"Folders in code2, middle\">else</Keywords>\n"
        "      <Keywords name=\"Folders in code2, close\">endif</Keywords>\n"
        "      <Keywords name=\"Folders in comment, open\">{{{</Keywords>\n"
        "      <Keywords name=\"Folders in comment, close\">}}}</Keywords>\n"
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

    // Fold keywords: parsed into the fold model, then emitted as a 'fold' tagging rule + +1/-1 points.
    check(parsed.foldCode1.open.size() == 1 && parsed.foldCode1.open[0] == "{", "parsed Folders in code1 open");
    check(parsed.foldCode1.close.size() == 1 && parsed.foldCode1.close[0] == "}", "parsed Folders in code1 close");
    check(parsed.foldCode2.open.size() == 1 && parsed.foldCode2.open[0] == "if", "parsed Folders in code2 open");
    check(parsed.foldCode2.middle.size() == 1 && parsed.foldCode2.middle[0] == "else", "parsed Folders in code2 middle");
    check(parsed.foldComment.open.size() == 1 && parsed.foldComment.open[0] == "{{{", "parsed Folders in comment open");
    // Fold markers are split into two tags so the host colours them apart: symbol markers ({ }) as
    // operators (foldsym), word markers (if/end) as keywords (foldkw).
    check(contains(lua2, "M:add_rule('foldsym', M:tag('foldsym', "), "e2e: symbol fold rule emitted");
    check(contains(lua2, "M:add_rule('foldkw', M:tag('foldkw', "), "e2e: word fold rule emitted");
    check(contains(lua2, "M:add_fold_point('foldsym', '{', 1)"), "e2e: code1 open -> +1 fold point");
    check(contains(lua2, "M:add_fold_point('foldsym', '}', -1)"), "e2e: code1 close -> -1 fold point");
    check(contains(lua2, "M:add_fold_point('foldkw', 'if', 1)"), "e2e: code2 open -> +1 fold point");
    check(contains(lua2, "M:add_fold_point('foldkw', 'endif', -1)"), "e2e: code2 close -> -1 fold point");
    // Word markers go through word_match (whole-word) so they never fire inside a larger identifier;
    // symbol markers stay lpeg.P.
    check(contains(lua2, "M:tag('foldkw', lexer.word_match('if else endif'))"), "e2e: word markers via whole-word word_match");
    check(contains(lua2, "M:tag('foldsym', lpeg.P('{') + lpeg.P('}'))"), "e2e: symbol markers via lpeg.P");
    check(!contains(lua2, "lpeg.P('if')") && !contains(lua2, "lpeg.P('else')"),
          "e2e: word markers are NOT emitted as boundary-less lpeg.P");
    check(!contains(lua2, "M:add_fold_point('foldkw', 'else'"), "e2e: middle keyword carries no fold delta (depth-neutral)");
    check(contains(lua2, "M:add_fold_point(lexer.COMMENT, '{{{', 1)"), "e2e: comment fold open under lexer.COMMENT");
    check(contains(lua2, "M:add_fold_point(lexer.COMMENT, '}}}', -1)"), "e2e: comment fold close under lexer.COMMENT");
    // Rule order (first match wins): comment/string BEFORE the fold rules (so a marker inside a
    // comment/string is not stolen), fold BEFORE keywords (so a code marker that is also a keyword
    // still tags as a fold tag).
    check(lua2.find("M:add_rule('comment'") < lua2.find("M:add_rule('foldsym'"), "e2e: comment rule precedes fold rules");
    check(lua2.find("M:add_rule('foldsym'") < lua2.find("M:add_rule('keyword"), "e2e: fold rules precede keyword rules");
    // Fold points registered longest-symbol-first, so a longer comment marker ('{{{') is not shadowed
    // by a shorter code marker ('{') via fold()'s per-line range guard.
    check(lua2.find("M:add_fold_point(lexer.COMMENT, '{{{'") < lua2.find("M:add_fold_point('foldsym', '{'"),
          "e2e: longer fold symbol registered before shorter one");

    // A UDL with no fold keywords must emit neither a fold rule nor fold points.
    check(!contains(bare, "add_fold_point") && !contains(bare, "M:add_rule('fold"),
          "empty UDL emits no fold rule or fold points");
    // Direct fold model (braces only, no keywords): the simplest foldable language -> symbol fold rule.
    UdlDef braces; braces.name = "braces"; braces.hasNumbers = false;
    braces.foldCode1.open = {"{"}; braces.foldCode1.close = {"}"};
    const std::string bl = translateUdlToScintillua(braces);
    check(contains(bl, "M:add_rule('foldsym', M:tag('foldsym', lpeg.P('{') + lpeg.P('}')))"), "braces: symbol fold rule");
    check(contains(bl, "M:add_fold_point('foldsym', '{', 1)") && contains(bl, "M:add_fold_point('foldsym', '}', -1)"),
          "braces: +1/-1 fold points");

    // Bare <UserLang> root (single-language export) must also parse.
    UdlDef bareParsed;
    check(parseUserDefineLangXml("<UserLang name=\"Solo\" ext=\"s\"><KeywordLists>"
                                 "<Keywords name=\"Keywords1\">x y</Keywords></KeywordLists></UserLang>",
                                 bareParsed) &&
              bareParsed.name == "Solo" && bareParsed.keywords[0].size() == 2,
          "bare <UserLang> root parses");

    // Multiple <UserLang> in one file (N++'s main userDefineLang.xml shape): parse them ALL.
    const std::string multi =
        "<NotepadPlus>\n"
        "  <UserLang name=\"Alpha\" ext=\"a\"><KeywordLists>"
        "<Keywords name=\"Keywords1\">one two</Keywords></KeywordLists></UserLang>\n"
        "  <UserLang name=\"Beta\" ext=\"b\"><KeywordLists>"
        "<Keywords name=\"Keywords1\">three</Keywords></KeywordLists></UserLang>\n"
        "</NotepadPlus>\n";
    const auto many = parseAllUserDefineLangs(multi);
    check(many.size() == 2, "parseAll: two <UserLang> -> two defs");
    if (many.size() == 2) {
        check(many[0].name == "Alpha" && many[0].ext == "a" && many[0].keywords[0].size() == 2, "parseAll: first def parsed");
        check(many[1].name == "Beta" && many[1].keywords[0].size() == 1 && many[1].keywords[0][0] == "three",
              "parseAll: second def parsed (not dropped)");
    }
    // parseUserDefineLangXml still returns only the first (back-compat for single-def callers).
    UdlDef firstOnly;
    check(parseUserDefineLangXml(multi, firstOnly) && firstOnly.name == "Alpha", "single-parse still returns first def");
    check(parseAllUserDefineLangs("<NotepadPlus></NotepadPlus>").empty(), "parseAll: no <UserLang> -> empty");
    // A nameless <UserLang> must be skipped, not abort the loop and drop its (valid) later siblings.
    {
        const auto af = parseAllUserDefineLangs(
            "<NotepadPlus>"
            "<UserLang ext=\"x\"><KeywordLists><Keywords name=\"Keywords1\">a</Keywords></KeywordLists></UserLang>"
            "<UserLang name=\"Good\" ext=\"g\"><KeywordLists><Keywords name=\"Keywords1\">b</Keywords></KeywordLists></UserLang>"
            "</NotepadPlus>");
        check(af.size() == 1 && af[0].name == "Good", "parseAll: nameless <UserLang> skipped, later sibling still parsed");
    }

    // Keyword "prefix mode": a prefix group emits a Cmt whole-word prefix matcher, not word_match.
    UdlDef pfx; pfx.name = "pfx"; pfx.hasNumbers = false;
    pfx.keywords[0] = {"IDM_", "IDD_"}; pfx.keywordPrefix[0] = true;
    pfx.keywords[1] = {"if", "else"};   // group 2 stays normal (word_match)
    const std::string pl = translateUdlToScintillua(pfx);
    check(contains(pl, "lpeg.Cmt((lexer.alnum + '_')^1, function(_, _, w) return w:sub(1, 4) == 'IDM_' or w:sub(1, 4) == 'IDD_' end)"),
          "prefix-mode group -> Cmt whole-word prefix matcher");
    check(!contains(pl, "word_match('IDM_"), "prefix-mode group does NOT use word_match");
    check(contains(pl, "lexer.word_match('if else')"), "non-prefix group still uses word_match");
    // Case-insensitive prefix compares lowercased.
    UdlDef pfi; pfi.name = "pfi"; pfi.hasNumbers = false;
    pfi.keywords[0] = {"REM"}; pfi.keywordPrefix[0] = true; pfi.keywordsCaseInsensitive = true;
    check(contains(translateUdlToScintillua(pfi), "w:sub(1, 3):lower() == 'rem'"), "case-insensitive prefix lowercases");
    // A prefix with non-word chars ("gl-", "$") must extend the captured word class, else it can never match.
    UdlDef pfsym; pfsym.name = "pfsym"; pfsym.hasNumbers = false;
    pfsym.keywords[0] = {"gl-", "$"}; pfsym.keywordPrefix[0] = true;
    check(contains(translateUdlToScintillua(pfsym), "lpeg.Cmt((lexer.alnum + '_' + lpeg.S('-$'))^1"),
          "symbol-containing prefix extends the Cmt word class");
    // <Prefix> parsing populates the per-group flags.
    UdlDef pp;
    check(parseUserDefineLangXml(
              "<UserLang name=\"P\"><Settings><Prefix Keywords1=\"yes\" Keywords2=\"no\"/></Settings>"
              "<KeywordLists><Keywords name=\"Keywords1\">a b</Keywords></KeywordLists></UserLang>", pp) &&
              pp.keywordPrefix[0] && !pp.keywordPrefix[1],
          "parsed <Prefix> per-group flags");

    // Malformed input fails cleanly.
    UdlDef junk;
    check(!parseUserDefineLangXml("not xml at all", junk), "non-UDL input rejected");

    if (g_fail == 0) { std::printf("ALL CHECKS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_fail);
    return 1;
}
