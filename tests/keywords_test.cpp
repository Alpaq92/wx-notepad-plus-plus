// SPDX-License-Identifier: Apache-2.0
//
// keywords_test - the keyword lists wxNote hands its lexers (src/keywords.h, src/keywords_scite.h,
// src/keywords_contrib.h), checked against the REAL Lexilla lexers and the real Language-menu table:
//   * every list names a menu language, and lands in a keyword slot that language's lexer takes;
//   * every menu language whose lexer describes keyword slots gets lists - apart from the named gaps,
//     the languages no list has been found for yet, which this suite lists so a newly filled one is noticed;
//   * spot checks that the lists landed where they should, and that the upper-case ones go to lexers
//     that upper-case what they look up;
//   * the lookup helpers: the generated table first, the extra table only for slots it leaves empty.
//
//   cmake --build build --target keywords_test && build/bin/keywords_test
//
#include "keywords.h"
#include "keyword_sets.h"
#include "lang_table.h"

#include "ILexer.h"
#include "Lexilla.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

// The Language menu's languages and their lexers (lang_table.h).
static std::map<std::string, std::string> menuLexers()
{
    std::map<std::string, std::string> out;
    std::size_t n;
    const WxnLang* t = wxnLangTable(n);
    for (std::size_t i = 0; i < n; ++i) out[t[i].name] = t[i].lexer;
    return out;
}

// Offer the lexer a one-word list: it answers -1 for a slot it has no list in. 1 takes, 0 does not,
// -1 no such lexer.
static int takesSlot(const std::string& lexer, int slot)
{
    Scintilla::ILexer5* lx = CreateLexer(lexer.c_str());
    if (!lx) return -1;
    const int r = lx->WordListSet(slot, "wxnote_probe") != -1 ? 1 : 0;
    lx->Release();
    return r;
}
static bool describesSlots(const std::string& lexer)
{
    Scintilla::ILexer5* lx = CreateLexer(lexer.c_str());
    if (!lx) return false;
    const char* d = lx->DescribeWordListSets();
    const bool any = d && *d;
    lx->Release();
    return any;
}

static bool hasWord(const std::string& language, int slot, const std::string& word)
{
    bool found = false;
    wxnForEachKeywordList(language, [&](const WxnKeywordList& k) {
        if (k.slot == slot && (" " + std::string(k.words) + " ").find(" " + word + " ") != std::string::npos) found = true;
    });
    return found;
}

int main()
{
    std::printf("keywords_test\n");
    const auto lexers = menuLexers();
    check(lexers.size() > 100, "read the Language-menu table (" + std::to_string(lexers.size()) + " languages)");

    std::printf("\n-- every list reaches a real language, in a slot its lexer takes --\n");
    for (const char* table : { "SciTE", "extra" })
    {
        std::size_t n;
        const WxnKeywordList* t = std::string(table) == "SciTE" ? wxnSciteKeywordLists(n) : wxnExtraKeywordLists(n);
        int bad = 0;
        std::set<std::pair<std::string, int>> seen;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto lx = lexers.find(t[i].language);
            if (lx == lexers.end()) { ++bad; std::printf("        [%s] is not a menu language\n", t[i].language); continue; }
            if (takesSlot(lx->second, t[i].slot) != 1) { ++bad; std::printf("        %s slot %d: lexer \"%s\" takes no list there\n", t[i].language, t[i].slot, lx->second.c_str()); }
            if (!seen.insert({ t[i].language, t[i].slot }).second) { ++bad; std::printf("        %s slot %d listed twice\n", t[i].language, t[i].slot); }
            if (!*t[i].words) { ++bad; std::printf("        %s slot %d is empty\n", t[i].language, t[i].slot); }
        }
        check(n > 0 && bad == 0, std::string(table) + " table: " + std::to_string(n) + " lists, all valid");
    }

    std::printf("\n-- coverage: languages whose lexer describes keyword slots --\n");
    // Languages nothing has been found for: neither SciTE nor Lexilla has lists for them and no other
    // source has been taken (keywords_contrib.h holds the ones that were). None are left. XML is not one:
    // SciTE deliberately gives it no tag list, so every tag counts as known.
    static const std::set<std::string> kGaps = {};
    int missing = 0, filledGap = 0, covered = 0;
    for (const auto& [language, lexer] : lexers)
    {
        if (lexer.empty() || !describesSlots(lexer)) continue;
        bool any = false;
        wxnForEachKeywordList(language, [&](const WxnKeywordList&) { any = true; });
        if (any) ++covered;
        if (!any && !kGaps.count(language)) { ++missing; std::printf("        %s (%s) gets no keyword list\n", language.c_str(), lexer.c_str()); }
        if (any && kGaps.count(language)) { ++filledGap; std::printf("        %s now has lists - take it off kGaps\n", language.c_str()); }
    }
    check(missing == 0, "every keyword-taking language has lists, or is a named gap (" + std::to_string(covered) + " covered)");
    check(filledGap == 0, "the named gaps are all still gaps");

    std::printf("\n-- the lists landed where they should --\n");
    const struct { const char* lang; int slot; const char* word; } spots[] = {
        { "C++", 0, "constexpr" }, { "C", 0, "struct" }, { "C#", 0, "namespace" }, { "Java", 0, "synchronized" },
        { "JavaScript", 0, "function" }, { "TypeScript", 0, "function" }, { "Go", 0, "func" }, { "Swift", 0, "func" },
        { "Makefile", 0, "ifdef" },
        { "Python", 0, "lambda" }, { "Pascal", 0, "begin" }, { "Fortran (free form)", 1, "abs" },
        { "Fortran (fixed form)", 0, "subroutine" }, { "SQL", 0, "select" }, { "Shell", 0, "elif" },
        { "Lua", 0, "elseif" }, { "Rust", 0, "impl" }, { "Visual Basic", 0, "dim" }, { "Ada", 0, "procedure" },
        { "TCL", 0, "proc" }, { "VHDL", 0, "entity" }, { "CSS", 0, "color" }, { "HTML", 0, "div" },
        { "HTML", 1, "function" }, { "PHP", 4, "echo" }, { "YAML", 0, "true" }, { "Kotlin", 0, "fun" },
        // Lexilla's own lists, for what SciTE's files leave out
        { "Dart", 0, "async" }, { "Dart", 2, "Future" }, { "Nix", 2, "builtins" }, { "TOML", 0, "inf" },
        { "Zig", 1, "usize" },
        // other editors' lists (keywords_contrib.h); Clarion's slot 2 is runtime expressions, 3 built-ins
        { "Clarion", 0, "PROCEDURE" }, { "Clarion", 2, "EVALUATE" }, { "Clarion", 3, "MESSAGE" },
        { "Clarion", 4, "ELLIPSE" }, { "Clarion", 6, "EVENT:" }, { "Gui4Cli", 1, "XBUTTON" },
        { "Gui4Cli", 3, "ENDIF" }, { "Gui4Cli", 4, "GUIOPEN" },
        { "ABL (OpenEdge)", 0, "def(ine" }, { "ABL (OpenEdge)", 1, "proce(dure" }, { "ABL (OpenEdge)", 3, "TODO" },
        { "BibTeX", 0, "article" }, { "CoffeeScript", 0, "unless" }, { "CoffeeScript", 3, "Promise" },
        { "GDScript", 0, "func" }, { "GDScript", 1, "Vector2" }, { "Julia", 0, "function" },
        { "Julia", 1, "Int64" }, { "Julia", 3, "println" }, { "MS SQL", 0, "select" }, { "MS SQL", 5, "sp_who" },
        { "MySQL", 0, "select" }, { "MySQL", 3, "concat" }, { "Stata", 0, "regress" }, { "Stata", 0, "margins" },
        { "Stata", 1, "strL" },
    };
    for (const auto& s : spots)
        check(hasWord(s.lang, s.slot, s.word), std::string(s.lang) + " slot " + std::to_string(s.slot) + " has \"" + s.word + "\"");
    check(!hasWord("Clarion", 4, "ELLISPE"), "Clarion: the source's ELLISPE typo is corrected");

    // clarionnocase and gui4cli upper-case the word they look up, so a list word with a lower-case letter
    // could never match.
    int lowerCase = 0;
    for (const char* lang : { "Clarion", "Gui4Cli" })
        wxnForEachKeywordList(lang, [&](const WxnKeywordList& k) {
            for (const char* p = k.words; *p; ++p)
                if (*p >= 'a' && *p <= 'z') { ++lowerCase; std::printf("        %s slot %d has lower case\n", lang, k.slot); break; }
        });
    check(lowerCase == 0, "Clarion and Gui4Cli lists are upper case, as their lexers look words up");
    // ...and abl (bar its task markers), bib, mssql and mysql lower-case it, so an upper-case letter could
    // never match there.
    int upperCase = 0;
    for (const char* lang : { "ABL (OpenEdge)", "BibTeX", "MS SQL", "MySQL" })
        wxnForEachKeywordList(lang, [&](const WxnKeywordList& k) {
            if (std::string(lang) == "ABL (OpenEdge)" && k.slot == 3) return;
            for (const char* p = k.words; *p; ++p)
                if (*p >= 'A' && *p <= 'Z') { ++upperCase; std::printf("        %s slot %d has upper case\n", lang, k.slot); break; }
        });
    check(upperCase == 0, "ABL, BibTeX, MS SQL and MySQL lists are lower case, as their lexers look words up");
    check(lexers.count("Clarion") && lexers.at("Clarion") == "clarionnocase",
          "Clarion uses the case-insensitive lexer its upper-case lists are written for");

    std::printf("\n-- the lists' names (keyword_sets.h) --\n");
    {
        // Every list wxNote fills has a name, every name is a slot its lexer describes or that wxNote
        // fills, and no lexer has a name twice.
        std::set<std::pair<std::string, int>> named, filled;
        std::size_t n;
        const WxnKeywordSetName* names = wxnKeywordSetNames(n);
        int badName = 0;
        std::set<std::pair<std::string, std::string>> seenName;
        for (std::size_t i = 0; i < n; ++i)
        {
            named.insert({ names[i].lexer, names[i].slot });
            if (!seenName.insert({ names[i].lexer, names[i].name }).second) { ++badName; std::printf("        %s: \"%s\" twice\n", names[i].lexer, names[i].name); }
            for (const char* p = names[i].name; *p; ++p)
                if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
                { ++badName; std::printf("        %s: \"%s\" is not one word\n", names[i].lexer, names[i].name); break; }
        }
        for (const auto& [language, lexer] : lexers)
            wxnForEachKeywordList(language, [&](const WxnKeywordList& k) { filled.insert({ lexer, k.slot }); });
        int unnamed = 0, stray = 0;
        for (const auto& f : filled) if (!named.count(f)) { ++unnamed; std::printf("        %s slot %d has a list but no name\n", f.first.c_str(), f.second); }
        // LexVerilog describes no lists at run time, though its source names all six it reads.
        const std::set<std::pair<std::string, int>> readButUndescribed = { { "verilog", 1 }, { "verilog", 3 }, { "verilog", 5 } };
        for (const auto& nm : named)
        {
            if (readButUndescribed.count(nm)) continue;
            Scintilla::ILexer5* lx = CreateLexer(nm.first.c_str());
            int described = 0;
            if (lx) { if (const char* d = lx->DescribeWordListSets()) { described = *d ? 1 : 0; for (; *d; ++d) if (*d == '\n') ++described; } lx->Release(); }
            if (takesSlot(nm.first, nm.second) != 1 || (nm.second >= described && !filled.count(nm)))
            { ++stray; std::printf("        %s slot %d is named but neither described nor filled\n", nm.first.c_str(), nm.second); }
        }
        check(badName == 0, "list names are single words, once per lexer");
        check(unnamed == 0, "every list wxNote fills has a name");
        check(stray == 0, "every name is a list its lexer describes, or one wxNote fills");
        check(wxnKeywordSetsOf("cpp").size() == 6 && std::string(wxnKeywordSetsOf("cpp")[1]->name) == "types",
              "C++'s second list is \"types\", where Notepad++ keeps them");

        // The user keyword groups land on the style numbers the themes colour: 128 up, or 192 up for the
        // HTML lexer, run after run in table order.
        int badRun = 0;
        for (const char* lexer : { "cpp", "python", "gdscript", "lua", "bash", "hypertext", "xml" })
        {
            Scintilla::ILexer5* lx = CreateLexer(lexer);
            if (!lx) { ++badRun; continue; }
            int expect = std::string(lexer) == "hypertext" || std::string(lexer) == "xml" ? 192 : 128;
            for (const WxnSubstyleRun* r : wxnSubstyleAllocation(lexer))
            {
                const int first = lx->AllocateSubStyles(r->base, r->count);
                if (first != expect) { ++badRun; std::printf("        %s base %d: first style %d, expected %d\n", lexer, r->base, first, expect); }
                expect += r->count;
            }
            lx->Release();
        }
        check(badRun == 0, "user keyword groups are allocated at the themes' style numbers");
        check(wxnSubstyleGroupsOf("hypertext", "PHP").size() == 16 && wxnSubstyleGroupsOf("hypertext", "PHP")[8].run->base == 121,
              "PHP's userKeywords are PHP words, after the tag and attribute groups");
        check(wxnSubstyleGroupsOf("lua", "Lua").front().name == "userKeywords5", "Lua's groups are USER KEYWORDS 5-8");
    }

    std::printf("\n-- lookups --\n");
    std::string cpp;
    wxnForEachKeywordList("C++", [&](const WxnKeywordList& k) { cpp += std::string(k.words) + " "; });
    check(cpp.find("constexpr") != std::string::npos, "C++'s lists hold its words");
    int noLists = 0;
    for (const char* none : { "", "Normal Text" }) wxnForEachKeywordList(none, [&](const WxnKeywordList&) { ++noLists; });
    check(noLists == 0, "...and no language, or Normal Text, has none");
    int ktRows = 0;
    wxnForEachKeywordList("Kotlin", [&](const WxnKeywordList&) { ++ktRows; });
    check(ktRows == 1, "a language SciTE lacks takes the extra table's list");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
