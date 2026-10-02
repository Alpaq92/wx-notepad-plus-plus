// SPDX-License-Identifier: Apache-2.0
//
// language_defs_test - languages.yaml (src/language_defs.h): every form the file takes, what a bad entry
// costs (itself only), and what the definitions do to keyword lists, user keyword groups, comment tokens
// and the file-name rules detection reads. The Language-menu table is read as data, as keywords_test does.
//
//   cmake --build build --target language_defs_test && build/bin/language_defs_test
//
#include "language_defs.h"
#include "lang_table.h"

#include <cstdio>
#include <map>
#include <regex>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

static std::map<std::string, std::string> g_lexers;   // Language-menu name -> lexer
static std::string canonical(const std::string& written)
{
    for (const auto& kv : g_lexers) if (wxnLangLower(kv.first) == wxnLangLower(written)) return kv.first;
    return std::string();
}
static std::string lexerOf(const std::string& name)
{
    const auto it = g_lexers.find(name);
    return it == g_lexers.end() ? std::string() : it->second;
}
static bool parse(const std::string& text, WxnLangDefs& defs, std::string* err = nullptr)
{
    return wxnParseLangDefs(text, defs, err, canonical, lexerOf);
}
static bool warned(const WxnLangDefs& d, const std::string& part)
{
    for (const std::string& w : d.warnings) if (w.find(part) != std::string::npos) return true;
    return false;
}
static bool hasWord(const std::string& words, const std::string& w)
{
    return (" " + words + " ").find(" " + w + " ") != std::string::npos;
}

int main()
{
    std::printf("language_defs_test\n");
    {
        std::size_t n;
        const WxnLang* t = wxnLangTable(n);
        for (std::size_t i = 0; i < n; ++i) g_lexers[t[i].name] = t[i].lexer;
    }
    check(g_lexers.size() > 100, "read the Language-menu table");

    std::printf("\n-- an empty file, comments only, and what does not parse --\n");
    {
        WxnLangDefs d;
        check(parse("", d) && d.langs.empty() && d.warnings.empty(), "an empty file changes nothing");
        check(parse("# just a header\n#   languages:\n", d) && d.langs.empty(), "a file of comments changes nothing");
        std::string err;
        check(!parse("languages: [unclosed", d, &err) && !err.empty(), "a file that does not parse is refused, with the reason");
        check(!parse("- a\n- b\n", d, &err) && err.find("not a map") != std::string::npos, "a top level that is not a map is refused");
        check(parse("\xEF\xBB\xBFlanguages:\n  Python:\n    keywords: {add: [match]}\n", d) && d.find("Python"),
              "a byte-order mark is fine");
    }

    std::printf("\n-- extensions, file names and the first line --\n");
    {
        WxnLangDefs d;
        check(parse("languages:\n"
                    "  php:\n"                                   // any case
                    "    extensions: {add: [inc, .TPL, '*.phtml5'], remove: [php3]}\n"
                    "  Python:\n"
                    "    extensions: [py, pyw]\n"
                    "    filenames: [SConstruct, SConscript]\n"
                    "    firstLine: '^#!.*\\bpython'\n"
                    "  Shell:\n"
                    "    extensions: 'sh bash'\n"
                    "    filenames: {remove: [PKGBUILD]}\n", d) && d.warnings.empty(),
              "the list and {add, remove} forms, as lists and as strings");
        const WxnLangDef* php = d.find("PHP");
        check(php && !php->extensions.replace && php->extensions.words == std::vector<std::string>({ "inc", "tpl", "phtml5" })
                  && php->extensions.remove == std::vector<std::string>({ "php3" }),
              "a language named in any case; extensions lower-cased, without dot or wildcard");
        const WxnLangDef* py = d.find("Python");
        check(py && py->extensions.replace && py->extensions.words.size() == 2, "a list replaces");
        check(py && py->filenames.words == std::vector<std::string>({ "sconstruct", "sconscript" }), "file names lower-cased");
        check(py && py->firstLine == "^#!.*\\bpython", "firstLine kept as written");
        const WxnLangDef* sh = d.find("Shell");
        check(sh && sh->extensions.replace && sh->extensions.words == std::vector<std::string>({ "sh", "bash" }), "a string is a list");

        const WxnLangFileRules r = wxnLangFileRulesFrom(d);
        check(r.extToLang.at("inc") == "PHP" && r.extToLang.at("pyw") == "Python", "rules: extensions map to their language");
        check(r.nameToLang.at("sconstruct") == "Python", "rules: whole names map to their language");
        check(r.extensionOff("PHP", "php3") && !r.extensionOff("PHP", "php"), "rules: a removed extension is off, the rest stay");
        check(r.extensionOff("Python", "pyi") && !r.extensionOff("Python", "pyw"), "rules: a replacing list turns off what it leaves out");
        check(r.nameOff("Shell", "pkgbuild") && !r.nameOff("Shell", "makefile"), "rules: a removed name is off");
        check(!r.extensionOff("C++", "cpp"), "rules: a language the file leaves alone keeps everything");
        check(r.firstLine.size() == 1 && r.firstLine[0].second == "Python"
                  && std::regex_search(std::string("#!/usr/bin/env python3"), r.firstLine[0].first),
              "rules: the first-line pattern matches");

        WxnLangDefs bad;
        parse("languages:\n  Python:\n    extensions: [py, 'a/b', 'x.y']\n    firstLine: '(unclosed'\n", bad);
        check(warned(bad, "\"a/b\" is not an extension") && warned(bad, "\"x.y\" is not an extension"),
              "what cannot be an extension is named and dropped");
        check(warned(bad, "firstLine is not a regular expression") && bad.find("Python")->firstLine.empty(),
              "a broken pattern is named and not used");
        check(bad.find("Python")->extensions.words == std::vector<std::string>({ "py" }), "...and the rest of the entry still counts");
    }

    std::printf("\n-- comments --\n");
    {
        WxnLangDefs d;
        check(parse("languages:\n"
                    "  Batch:\n    comments: {line: '::'}\n"
                    "  CSS:\n    comments: {line: '//'}\n"
                    "  Python:\n    comments: {block: ['\"\"\"', '\"\"\"']}\n"
                    "  C++:\n    comments: {line: '', block: []}\n"
                    "  SQL:\n    comments: {line: REM}\n", d) && d.warnings.empty(), "line and block, given and taken away");
        const WxnCommentStyle batch = wxnApplyCommentDef(wxnCommentStyleForKey("batch"), d.find("Batch"));
        check(std::string(batch.line) == "::" && !batch.lineNeedsSpace && !batch.lineCaseless,
              "Batch's rem becomes ::, which needs no space and keeps its case");
        const WxnCommentStyle css = wxnApplyCommentDef(wxnCommentStyleForKey("css"), d.find("CSS"));
        check(css.hasLine() && std::string(css.line) == "//" && css.hasBlock(), "CSS gains a line form and keeps its block form");
        const WxnCommentStyle py = wxnApplyCommentDef(wxnCommentStyleForKey("python"), d.find("Python"));
        check(py.hasBlock() && std::string(py.blockOpen) == "\"\"\"" && std::string(py.line) == "#" && py.colonOpensBlock,
              "Python gains a block form and keeps the rest");
        const WxnCommentStyle cpp = wxnApplyCommentDef(wxnCommentStyleForKey("cpp"), d.find("C++"));
        check(cpp.empty(), "'' and [] take both forms away");
        const WxnCommentStyle sql = wxnApplyCommentDef(wxnCommentStyleForKey("sql"), d.find("SQL"));
        check(sql.lineNeedsSpace, "a word-shaped token comments only before a space");
        const WxnCommentStyle same = wxnApplyCommentDef(wxnCommentStyleForKey("batch"), nullptr);
        check(std::string(same.line) == "rem" && same.lineCaseless, "no definition leaves the built-in style");
        const WxnLineCommentEdit e = wxnPlanLineComment("echo hi", batch, WxnCommentToggle);
        check(e.applies && !e.commented && e.open == "::", "Toggle Comment inserts the user's token");

        WxnLangDefs bad;
        parse("languages:\n  CSS:\n    comments: {block: ['/*'], line: [a]}\n  Lua:\n    comments: '--'\n", bad);
        check(warned(bad, "comments.block is not a pair") && warned(bad, "comments.line is not text")
                  && warned(bad, "Lua: comments is not a map"),
              "a half pair, a list for a token and a bare string are named");
        check(!bad.find("CSS")->hasBlockComment && !bad.find("CSS")->hasLineComment, "...and change nothing");
    }

    std::printf("\n-- keyword lists --\n");
    {
        WxnLangDefs d;
        check(parse("languages:\n"
                    "  C++:\n"
                    "    keywords:\n"
                    "      keywords: {add: [co_await, co_yield], remove: [goto]}\n"
                    "      types: [size_t, ssize_t]\n"
                    "      taskMarkers: 'TODO FIXME'\n"
                    "      userKeywords1: [Q_OBJECT, emit]\n"
                    "      userKeywords3: {add: [slots]}\n"
                    "  Python:\n"
                    "    keywords: {add: [match, case]}\n"
                    "  Batch:\n"
                    "    keywords: [foo]\n"
                    "  MySQL:\n"
                    "    keywords: {add: [frobnicate]}\n", d) && d.warnings.empty(), "named lists, groups and the short form");
        const auto cpp = wxnEffectiveKeywordLists("C++", "cpp", d.find("C++"));
        check(hasWord(cpp.at(0), "co_await") && hasWord(cpp.at(0), "constexpr") && !hasWord(cpp.at(0), "goto"),
              "an edit adds to and takes from the built-in list");
        check(cpp.at(1) == "size_t ssize_t", "a list fills a slot the built-in data leaves empty");
        check(cpp.at(5) == "TODO FIXME", "...as a string too");
        check(hasWord(cpp.at(2), "brief"), "a list the file leaves alone stays built-in");
        const auto py = wxnEffectiveKeywordLists("Python", "python", d.find("Python"));
        check(hasWord(py.at(0), "match") && hasWord(py.at(0), "lambda"), "the short form edits the \"keywords\" list");
        const auto bat = wxnEffectiveKeywordLists("Batch", "batch", d.find("Batch"));
        check(bat.at(0) == "foo", "...or the first list, for a lexer with no list called keywords (and a list replaces)");
        const auto my = wxnEffectiveKeywordLists("MySQL", "mysql", d.find("MySQL"));
        check(hasWord(my.at(1), "frobnicate") && !hasWord(my.at(0), "frobnicate"), "...MySQL's \"keywords\" is its second list");
        const auto groups = wxnUserKeywordGroups("C++", "cpp", d.find("C++"));
        check(groups.size() == 2 && groups[0].first.name == "userKeywords1" && groups[0].first.index == 0
                  && groups[0].second == "Q_OBJECT emit" && groups[1].first.index == 2,
              "user keyword groups: the named ones with words, at their place in the run");
        const auto none = wxnEffectiveKeywordLists("C++", "cpp", nullptr);
        check(none.at(0) == wxnBuiltinKeywordLists("C++").at(0), "no definition: the built-in lists");

        WxnLangDefs bad;
        parse("languages:\n  C++:\n    keywords: {nonsense: [a], types: {add: [x], drop: [y]}}\n"
              "  Diff:\n    keywords: [a]\n  Klingon:\n    extensions: [tlh]\n  Python:\n    colour: red\n", bad);
        check(warned(bad, "no keyword list \"nonsense\"") && warned(bad, "it has keywords, types"),
              "an unknown list is named, with the lists the language has");
        check(warned(bad, "keywords.types is neither a list nor {add, remove}"), "a map with other keys is not an edit");
        check(warned(bad, "Diff has no keyword lists"), "a language without lists says so");
        check(warned(bad, "unknown language \"Klingon\"") && !bad.find("Klingon"), "an unknown language is named and skipped");
        check(warned(bad, "Python: unknown entry \"colour\""), "an unknown entry is named");

        WxnLangDefs twice;
        parse("languages:\n  Python:\n    extensions: [py]\n  python:\n    extensions: [pyw]\n", twice);
        check(warned(twice, "Python is given twice") && twice.find("Python")->extensions.words[0] == "py",
              "a language given twice: the first counts");
        WxnLangDefs other;
        parse("version: 2\nlanguages:\n  Python:\n    keywords: {add: [x]}\n", other);
        check(warned(other, "unknown entry \"version\"") && other.find("Python"), "an unknown top-level entry is named, the rest used");
    }

    std::printf("\n-- nothing, NULL, and an entry given twice --\n");
    {
        WxnLangDefs d;
        check(parse("languages:\n  C++:\n  Python:\n    extensions:\n    comments:\n    keywords:\n"
                    "  Lua:\n    keywords:\n      functions1: ~\n      user1: {add: [love]}\n", d)
                  && d.warnings.empty() && !d.find("C++") && d.find("Python") && !d.find("Python")->extensions.given
                  && !d.find("Python")->hasLineComment && d.find("Python")->keywords.empty()
                  && d.find("Lua") && !d.find("Lua")->keywords.count("functions1") && d.find("Lua")->keywords.count("user1"),
              "nothing - a key with no value, or ~ - says nothing, and is no mistake");
        WxnLangDefs n;
        check(parse("languages:\n  C:\n    keywords:\n      keywords: {add: [NULL, null, Null, nullptr]}\n      types: NULL\n"
                    "      docKeywords: {add: [~, todo]}\n", n) && n.warnings.empty(), "NULL in a list parses without a word of warning");
        const WxnLangDef* c = n.find("C");
        check(c && c->keywords.at("keywords").words == std::vector<std::string>({ "NULL", "null", "Null", "nullptr" }),
              "...and null, Null and NULL are words there, as nullptr is");
        check(c && c->keywords.at("types").replace && c->keywords.at("types").words == std::vector<std::string>({ "NULL" }),
              "...also standing alone, as the list");
        check(c && c->keywords.at("docKeywords").words == std::vector<std::string>({ "todo" }), "...while ~ stays nothing");

        WxnLangDefs twice;
        parse("languages:\n  Python:\n    extensions: [py]\n    extensions: [pyw]\n    comments: {line: '#', line: '//'}\n"
              "    keywords:\n      keywords: {add: [a]}\n      keywords: {add: [b]}\n", twice);
        const WxnLangDef* tp = twice.find("Python");
        check(tp && tp->extensions.words == std::vector<std::string>({ "py" }) && warned(twice, "Python: extensions is given twice"),
              "an entry given twice: the first counts - as the writers read it - and the second is named");
        check(tp && tp->lineComment == "#" && warned(twice, "Python: comments.line is given twice"), "...in comments too");
        check(tp && tp->keywords.count("keywords") && tp->keywords.at("keywords").words == std::vector<std::string>({ "a" })
                  && warned(twice, "Python: keywords.keywords is given twice"), "...and a keyword list given twice");
    }

    std::printf("\n-- the Style Configurator's edits, before they are saved --\n");
    {
        WxnLangDefs s;
        parse("languages:\n  C++:\n    keywords:\n      types: {add: [a], remove: [b]}\n      userKeywords1: [x]\n", s);
        wxnLangDefsSetWords(s, "C++", "types", { "c" });
        wxnLangDefsSetWords(s, "C++", "userKeywords1", { "y", "z" });
        wxnLangDefsSetWords(s, "Python", "keywords", { "match" });
        wxnLangDefsSetWords(s, "Lua", "user1", {});
        const WxnLangDef* cpp = s.find("C++");
        check(cpp && cpp->keywords.at("types").words == std::vector<std::string>({ "c" }) && !cpp->keywords.at("types").replace
                  && cpp->keywords.at("types").remove == std::vector<std::string>({ "b" }), "a list's new words, its removals kept");
        check(cpp && cpp->keywords.at("userKeywords1").replace && cpp->keywords.at("userKeywords1").words == std::vector<std::string>({ "y", "z" }),
              "a group written as a list stays one");
        check(s.find("Python") && s.find("Python")->keywords.at("keywords").words == std::vector<std::string>({ "match" }),
              "a language the file lacks is added");
        check(!s.find("Lua"), "...but not for no words");
        wxnLangDefsSetWords(s, "C++", "userKeywords1", {});
        wxnLangDefsSetWords(s, "C++", "types", {});
        check(!cpp->keywords.count("userKeywords1") && cpp->keywords.count("types") && cpp->keywords.at("types").words.empty(),
              "a list left saying nothing goes; one that still takes words away stays");
    }

    std::printf("\n-- writing what the Style Configurator's User-defined keywords box holds --\n");
    {
        auto set = [&](std::string& text, const std::string& lang, const std::string& list, std::vector<std::string> words, std::string* err = nullptr) {
            return wxnLangDefsSetAdded(text, lang, list, wxnMainKeywordList(lexerOf(lang)), words, err, canonical);
        };
        auto reread = [&](const std::string& text, const std::string& lang, const std::string& list) {
            WxnLangDefs d;
            if (!parse(text, d) || !d.warnings.empty()) return std::string("(unreadable)");
            const WxnLangDef* def = d.find(lang);
            if (!def || !def->keywords.count(list)) return std::string("(none)");
            const WxnListEdit& e = def->keywords.at(list);
            return std::string(e.replace ? "=" : "+") + wxnJoinWords(e.words) + (e.remove.empty() ? "" : " -" + wxnJoinWords(e.remove));
        };
        std::string text = wxnLanguagesYamlTemplate();
        WxnLangDefs fresh;
        check(parse(text, fresh) && fresh.warnings.empty() && fresh.langs.empty(), "the template parses, defines nothing and warns of nothing");
        check(set(text, "C++", "types", { "size_t", "ssize_t" }) && reread(text, "C++", "types") == "+size_t ssize_t",
              "a first addition fills the template's empty languages:");
        check(text.find("# languages.yaml - your changes") == 0, "...keeping its opening comments");
        check(set(text, "C++", "userKeywords1", { "Q_OBJECT" }) && reread(text, "C++", "types") == "+size_t ssize_t"
                  && reread(text, "C++", "userKeywords1") == "+Q_OBJECT", "a second list sits beside the first");
        check(set(text, "C++", "types", { "uint8_t" }) && reread(text, "C++", "types") == "+uint8_t", "setting a list again replaces what it adds");
        check(set(text, "C++", "types", {}) && reread(text, "C++", "types") == "(none)" && reread(text, "C++", "userKeywords1") == "+Q_OBJECT",
              "no words drop the addition, and the empty entry with it");
        WxnLangDefs gone;
        check(set(text, "C++", "userKeywords1", {}) && parse(text, gone) && gone.warnings.empty() && !gone.find("C++"),
              "...and the language once nothing is left");

        std::string mine = "# mine\nlanguages:\n  python:   # any case\n    extensions: [py]\n"
                           "    keywords: {add: [match], remove: [print]}\n  Ruby:\n    keywords: [puts]\n";
        check(set(mine, "Python", "identifiers", { "self" }), "the short form is a list of its own...");
        check(reread(mine, "Python", "keywords") == "+match -print" && reread(mine, "Python", "identifiers") == "+self",
              "...moved under its name, with its removals, beside the new list");
        check(set(mine, "Python", "keywords", { "case" }) && reread(mine, "Python", "keywords") == "+case -print",
              "an addition changes, the removals stay");
        WxnLangDefs back;
        check(parse(mine, back) && back.find("Python")->extensions.words == std::vector<std::string>({ "py" }), "other entries stay as they were");
        std::string err;
        check(!set(mine, "Ruby", "keywords", { "x" }, &err) && err.find("replaces") != std::string::npos
                  && reread(mine, "Ruby", "keywords") == "=puts", "a replaced list is not edited, and says why");
        std::string groups = "languages:\n  C++:\n    keywords:\n      types: {add: [a]}\n      userKeywords2: [Q_OBJECT, emit]\n";
        check(wxnLangDefsSetAdded(groups, "C++", "userKeywords2", "keywords", { "signals" }, &err, canonical, true)
                  && reread(groups, "C++", "userKeywords2") == "=signals" && reread(groups, "C++", "types") == "+a",
              "a user group written as a list stays a list, with the new words");
        check(wxnLangDefsSetAdded(groups, "C++", "userKeywords2", "keywords", {}, &err, canonical, true)
                  && reread(groups, "C++", "userKeywords2") == "(none)" && reread(groups, "C++", "types") == "+a",
              "...and goes when it has none");
        std::string broken = "languages: [";
        check(!set(broken, "C++", "types", { "x" }, &err) && broken == "languages: [", "a file that does not parse is left alone");
        std::string empty;
        check(set(empty, "Lua", "userKeywords5", { "love" }) && reread(empty, "Lua", "userKeywords5") == "+love", "an empty file gets its first entry");

        std::string nothing = "languages:\n  C++:\n    keywords:\n      types:\n      keywords: {add: [a]}\n";
        check(set(nothing, "C++", "types", { "size_t" }) && reread(nothing, "C++", "types") == "+size_t"
                  && reread(nothing, "C++", "keywords") == "+a", "a list with no value takes the words...");
        check(nothing.find("size_t") < nothing.find("[a]"), "...in its place");
        std::string nothingKw = "languages:\n  C++:\n    extensions: [cpp]\n    keywords:\n    filenames: [x.cc]\n";
        check(set(nothingKw, "C++", "types", { "size_t" }) && reread(nothingKw, "C++", "types") == "+size_t"
                  && nothingKw.find("size_t") < nothingKw.find("x.cc"), "...as does keywords: with no value");
        std::string nothingLang = "languages:\n  Lua:\n  C++:\n    extensions: [cpp]\n";
        check(set(nothingLang, "Lua", "user1", { "love" }) && reread(nothingLang, "Lua", "user1") == "+love"
                  && nothingLang.find("love") < nothingLang.find("C++"), "...and a language with no value");
        std::string unchanged = "languages:\n  C++:\n    keywords:\n      types:\n";
        check(set(unchanged, "C++", "types", {}) && unchanged == "languages:\n  C++:\n    keywords:\n      types:\n",
              "no words for a list with no value: the file is left as it is");
        std::string nullWord = "languages:\n  C:\n    keywords:\n      types: NULL\n";
        check(!set(nullWord, "C", "types", { "x" }, &err) && reread(nullWord, "C", "types") == "=NULL",
              "the word NULL alone is a list the file replaces, not nothing");
    }

    std::printf("\n-- laying an import over the user's file --\n");
    {
        auto merge = [&](std::string& text, const std::string& overlay) {
            std::string err;
            return wxnLangDefsMerge(text, overlay, &err, canonical, lexerOf);
        };
        auto entry = [&](const std::string& text, const std::string& lang) {
            WxnLangDefs d;
            const WxnLangDef* def = (parse(text, d) && d.warnings.empty()) ? d.find(lang) : nullptr;
            return def ? *def : WxnLangDef();
        };
        std::string mine = "# my definitions\nlanguages:\n"
                           "  python:\n    extensions: {add: [sage]}\n    comments: {line: '#', block: ['\"\"\"', '\"\"\"']}\n"
                           "    keywords: {add: [match]}\n"
                           "  Ruby:\n    filenames: [Brewfile]\n";
        const std::string imported = "languages:\n"
                                     "  Python:\n    extensions: {add: [pyx], remove: [pyw]}\n    comments: {line: '##'}\n"
                                     "    keywords:\n      identifiers: {add: [self]}\n"
                                     "  C++:\n    keywords:\n      types: {add: [size_t]}\n";
        check(merge(mine, imported), "an import lays over the user's file");
        const WxnLangDef py = entry(mine, "Python");
        check(py.extensions.words == std::vector<std::string>({ "pyx" }) && py.extensions.remove == std::vector<std::string>({ "pyw" }),
              "an entry the import sets is the import's");
        check(py.hasLineComment && py.lineComment == "##" && py.hasBlockComment && py.blockOpen == "\"\"\"",
              "comments: the import's line token, the user's block pair");
        check(py.keywords.count("keywords") && py.keywords.at("keywords").words == std::vector<std::string>({ "match" })
                  && py.keywords.count("identifiers"), "keywords: the user's short form kept under its name, the import's list beside it");
        check(entry(mine, "Ruby").filenames.words == std::vector<std::string>({ "brewfile" }), "a language the import leaves out stays");
        check(entry(mine, "C++").keywords.count("types"), "a language only the import has is added");
        check(mine.find("# my definitions") == 0, "the opening comments stay");
        std::string empty;
        check(merge(empty, imported) && entry(empty, "C++").keywords.count("types"), "into an empty file");
        std::string named = "languages:\n  Python:\n    keywords:\n      keywords: {add: [match]}\n      identifiers: {add: [self]}\n";
        check(merge(named, "languages:\n  Python:\n    keywords: {add: [case]}\n")
                  && entry(named, "Python").keywords.count("keywords") && entry(named, "Python").keywords.count("identifiers")
                  && entry(named, "Python").keywords.at("keywords").words == std::vector<std::string>({ "case" }),
              "an import's short form stands for the main list, and leaves the other lists alone");
        std::string broken = "languages: [";
        check(!merge(broken, imported) && broken == "languages: [", "a file that does not parse is left alone");
    }

    std::printf("\n-- edits --\n");
    {
        WxnListEdit add;  add.given = true; add.words = { "c", "a" }; add.remove = { "b" };
        check(add.apply({ "a", "b", "d" }) == std::vector<std::string>({ "a", "d", "c" }), "edit: base less removals, plus additions, each once");
        WxnListEdit rep;  rep.given = true; rep.replace = true; rep.words = { "x", "x", "y" };
        check(rep.apply({ "a" }) == std::vector<std::string>({ "x", "y" }), "replacement: the new list, each word once");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
