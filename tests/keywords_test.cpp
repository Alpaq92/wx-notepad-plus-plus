// SPDX-License-Identifier: Apache-2.0
//
// keywords_test - the keyword lists wxNote hands its lexers (src/keywords.h, src/keywords_scite.h),
// checked against the REAL Lexilla lexers and the real Language-menu table:
//   * every list names a menu language, and lands in a keyword slot that language's lexer takes;
//   * every menu language whose lexer describes keyword slots gets lists - apart from the named gaps,
//     the languages SciTE has none for, which this suite lists so a newly filled one is noticed;
//   * spot checks that SciTE's lists landed where they should;
//   * the lookup helpers: the generated table first, the extra table only for slots it leaves empty.
//
//   cmake --build build --target keywords_test && build/bin/keywords_test
//
#include "keywords.h"

#include "ILexer.h"
#include "Lexilla.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

// menu_data_language.h needs wx, so it is read as DATA (the lang_detect_test pattern):
//   { kCmdLangPascal, "Pascal", "pascal" },
static std::map<std::string, std::string> menuLexers()
{
    std::ifstream in(std::string(SRC_DIR) + "/menu_data_language.h", std::ios::binary);
    std::ostringstream s; s << in.rdbuf();
    const std::string text = s.str();
    std::map<std::string, std::string> out;
    static const std::regex row(R"re(\{\s*kCmdLang\w+\s*,\s*"([^"]+)"\s*,\s*"([^"]*)")re");
    for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) out[(*it)[1]] = (*it)[2];
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
    // Nothing found for these yet: neither SciTE nor Lexilla has lists for them (being filled from other
    // sources). XML is not one: SciTE deliberately gives it no tag list, so every tag counts as known.
    static const std::set<std::string> kGaps = {
        "ABL (OpenEdge)", "BibTeX", "Clarion", "CoffeeScript", "GDScript", "Gui4Cli", "Julia",
        "MS SQL", "MySQL", "Stata",
    };
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

    std::printf("\n-- SciTE's lists landed where they should --\n");
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
    };
    for (const auto& s : spots)
        check(hasWord(s.lang, s.slot, s.word), std::string(s.lang) + " slot " + std::to_string(s.slot) + " has \"" + s.word + "\"");

    std::printf("\n-- lookups --\n");
    const char* cpp = wxnKeywordWords("C++");
    check(cpp && cpp == wxnKeywordWords("C++"), "wxnKeywordWords: built once, the same pointer every time");
    check(cpp && std::string(cpp).find("constexpr") != std::string::npos, "...holding the language's words");
    check(wxnKeywordWords("") == nullptr && wxnKeywordWords("Normal Text") == nullptr, "...and nullptr for no language");
    int ktRows = 0;
    wxnForEachKeywordList("Kotlin", [&](const WxnKeywordList&) { ++ktRows; });
    check(ktRows == 1, "a language SciTE lacks takes the extra table's list");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
