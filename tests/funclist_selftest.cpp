// SPDX-License-Identifier: Apache-2.0
//
// funclist_selftest - the Function List symbol scanner (flCollect / flRules / flCommentRe) and the
// autocomplete word harvester (wxnCollectWords), driven against plain strings with no editor.
//
// Both are free functions precisely so this is possible; until this suite existed the only checks on
// them were ad-hoc scratch scripts, and a full release shipped with the Kotlin container scan latching
// onto the wrong brace, the extension-less Makefile/Dockerfile branch dead, and a stray CR baked into
// every tree label on CRLF files. Every one of those is pinned below.
//
// It #includes src/main.cpp the way bridge_selftest does (wxIMPLEMENT_APP neutralized, own main), but
// unlike that suite it never constructs a frame and never calls wxEntry - it only calls free functions,
// so it needs no display and is registered as a "pure" ctest.
//
//   cmake --build build --target funclist_selftest && build/bin/funclist_selftest
//
#include <wx/app.h>
#include <wx/init.h>

// Neutralize the app-entry macro before pulling the application in - the same embedding path
// bridge_selftest documents. We supply the main() below.
#undef wxIMPLEMENT_APP
#define wxIMPLEMENT_APP(appname) /* neutralized: funclist_selftest provides its own main() */
#include "main.cpp"

#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

// --- Function List helpers -------------------------------------------------------------------------

// The symbol names flCollect extracts, in document order.
static std::vector<std::string> names(const std::string& text, const std::string& lang)
{
    std::vector<std::string> out;
    for (const FLSym& s : flCollect(text, lang)) out.push_back(std::string(s.name.utf8_str()));
    return out;
}

static void expectNames(const std::string& text, const std::string& lang,
                        const std::vector<std::string>& want, const std::string& what)
{
    const std::vector<std::string> got = names(text, lang);
    const bool ok = (got == want);
    check(ok, what);
    if (!ok) {
        std::printf("        want:"); for (const auto& s : want) std::printf(" [%s]", s.c_str());
        std::printf("\n        got :"); for (const auto& s : got)  std::printf(" [%s]", s.c_str());
        std::printf("\n");
    }
}

// Is `child` nested inside `parent`'s container body range? That is what drives the tree's shape.
static bool nestedIn(const std::string& text, const std::string& lang,
                     const std::string& parent, const std::string& child)
{
    const std::vector<FLSym> syms = flCollect(text, lang);
    const FLSym* p = nullptr;
    for (const FLSym& s : syms) if (std::string(s.name.utf8_str()) == parent) { p = &s; break; }
    if (!p || p->kind != 1) return false;
    for (const FLSym& s : syms)
        if (std::string(s.name.utf8_str()) == child) return s.pos > p->pos && s.pos < p->rangeEnd;
    return false;
}

int main()
{
    std::printf("funclist_selftest\n");

    // ---- CRLF: line-bounded captures must not swallow the carriage return ---------------------------
    // Shipped broken: `[^\n]+` put a \r into every markdown/dockerfile label on a CRLF file.
    expectNames("# Title\r\n\r\n## Section two\r\n\r\n### Deep\r\n", "markdown",
                { "Title", "Section two", "Deep" }, "markdown headings on a CRLF file carry no stray CR");
    expectNames("# Title\n## Section two\n", "markdown",
                { "Title", "Section two" }, "markdown headings on an LF file");
    expectNames("FROM node:20 AS builder\r\nRUN x\r\nFROM alpine\r\n", "dockerfile",
                { "node:20", "alpine" }, "dockerfile stages on CRLF: no CR, and AS <stage> still stripped");

    // ---- Kotlin: a body-less declaration is a LEAF, not a container ---------------------------------
    // Shipped broken: Kotlin has no statement terminator, so the scan ran on to the NEXT declaration's
    // brace and nested the whole rest of the file under `User`.
    {
        const std::string kt =
            "package com.example.demo\n\n"
            "data class User(val name: String, val age: Int)\n\n"
            "class Marker\n\n"
            "sealed interface Shape\n\n"
            "class Store(private val db: String) {\n"
            "    fun load(id: Int): User? {\n        return null\n    }\n"
            "    suspend fun save(u: User) {\n    }\n"
            "}\n\n"
            "fun String.slugify(): String = lowercase()\n";
        check(!nestedIn(kt, "kotlin", "User", "Marker"), "kotlin: body-less `data class` swallows nothing");
        check(!nestedIn(kt, "kotlin", "Marker", "Shape"), "kotlin: body-less `class` swallows nothing");
        check(!nestedIn(kt, "kotlin", "Shape", "Store"), "kotlin: body-less `interface` swallows nothing");
        check(nestedIn(kt, "kotlin", "Store", "load"),    "kotlin: a real class body DOES contain its funs");
        check(nestedIn(kt, "kotlin", "Store", "save"),    "kotlin: ... both of them");
        check(!nestedIn(kt, "kotlin", "Store", "slugify"),"kotlin: a top-level fun after the class is NOT nested");
    }
    // Brace-on-next-line must still nest - the guard allows a newline whose next non-blank char is `{`.
    check(nestedIn("class Store\n{\n    fun a() {}\n}\n", "kotlin", "Store", "a"),
          "kotlin: brace-on-next-line style still nests");
    check(nestedIn("data class User(\n  val n: String\n) {\n    fun a() {}\n}\n", "kotlin", "User", "a"),
          "kotlin: multi-line constructor list then a brace still nests");

    // C-family must be UNAFFECTED by that guard (it is keyed on the language, via flBodyStyle).
    // NOTE the bodies: the cpp rules deliberately match DEFINITIONS, not declarations - every function
    // pattern requires a trailing `{` - so `void m();` would legitimately not be listed at all.
    check(nestedIn("class Foo :\n    public Bar\n{\n    void m() {}\n};\n", "cpp", "Foo", "m"),
          "cpp: multi-line base-clause still nests (the EOL guard is kotlin-only)");

    // ---- Swift: `class func` is a method, not a type named "func" -----------------------------------
    {
        const std::string sw =
            "public struct Point {\n"
            "    func distance() -> Double { return 0 }\n"
            "    class func origin() -> Point { return Point() }\n"
            "}\n";
        const std::vector<std::string> got = names(sw, "swift");
        bool sawFunc = false; for (const auto& n : got) if (n == "func") sawFunc = true;
        check(!sawFunc, "swift: `class func` is not read as a type named \"func\"");
        bool sawOrigin = false; for (const auto& n : got) if (n == "origin") sawOrigin = true;
        check(sawOrigin, "swift: `class func origin()` is listed as the function `origin`");
    }

    // ---- Makefile: pattern / suffix / variable targets, and no variable assignments -----------------
    {
        const std::string mk = "CC = gcc\nall: build test\n\tcc\nbuild/%.o: src/%.c\n\tcc\n"
                               ".c.o:\n\tcc\n$(BIN): main.o\n\tld\nVAR := 1\n";
        expectNames(mk, "makefile", { "all", "build/%.o", ".c.o", "$(BIN)" },
                    "makefile: pattern/suffix/variable targets listed, assignments skipped");
    }

    // ---- Comment masks: a brace inside a comment or string must not open a container body -----------
    check(!nestedIn("// class Fake {\nclass Real {\n    void m() {}\n};\n", "cpp", "Fake", "m"),
          "cpp: a declaration inside a // comment is masked out entirely");
    check(nestedIn("/* } */\nclass Real {\n    void m() {}\n};\n", "cpp", "Real", "m"),
          "cpp: a stray `}` inside a block comment does not close the body early");
    // SCSS/LESS share the "css" key and support // comments - unmasked, a `{` there corrupts nesting.
    {
        const std::vector<std::string> got = names("// a { \n.btn { color: red; }\n", "css");
        bool sawA = false; for (const auto& n : got) if (n == "a") sawA = true;
        check(!sawA, "css: `//` comments are masked (SCSS/LESS share this key)");
    }
    // Kotlin/Swift raw strings: a brace inside """...""" must not be counted by the brace scan.
    check(!nestedIn("val t = \"\"\"{ class Fake\"\"\"\nclass Real {\n    fun m() {}\n}\n",
                    "kotlin", "Fake", "m"),
          "kotlin: a `{` inside a \"\"\"raw string\"\"\" is masked");

    // ---- YAML: 2- and 4-space nesting both resolve --------------------------------------------------
    expectNames("jobs:\n  test:\n  deploy:\n", "yaml", { "jobs", "test", "deploy" },
                "yaml: 2-space nested keys");
    expectNames("jobs:\n    test:\n    deploy:\n", "yaml", { "jobs", "test", "deploy" },
                "yaml: 4-space nested keys (was: none at all)");

    // ---- ini / perl ---------------------------------------------------------------------------------
    expectNames("[core]\nx=1\n[[bin]]\n[remote \"origin\"]\n", "ini",
                { "core", "bin", "remote \"origin\"" }, "ini/toml: [section] and [[array-of-table]]");
    expectNames("package My::Mod;\nsub new { }\nsub run_it { }\n", "perl",
                { "My::Mod", "new", "run_it" }, "perl: package + subs, flat");

    // ---- An unknown language yields nothing, and must not throw --------------------------------------
    check(flCollect("whatever\n", "no-such-language").empty(), "unknown language -> no symbols, no throw");
    check(flCollect("", "cpp").empty(), "empty buffer -> no symbols");

    // ---- wxnCollectWords: prefix match, dedupe, and the style filter ---------------------------------
    {
        const std::string doc = "alpha alphabet alpha beta\n";
        std::set<std::string, std::less<>> out;
        std::bitset<256> noSkip;
        wxnCollectWords(doc, "alph", out, noSkip, [](size_t){ return 0; });
        check(out.size() == 2 && out.count("alpha") && out.count("alphabet"),
              "collectWords: prefix-matched words, deduped");

        out.clear();
        wxnCollectWords(doc, "alpha", out, noSkip, [](size_t){ return 0; });
        check(out.size() == 1 && out.count("alphabet"),
              "collectWords: a word equal to the prefix is not offered (strictly longer only)");

        // Style filter: mark style 5 as prose, and report style 5 for everything past byte 6 - so
        // "alphabet" (at 6) and the second "alpha" (at 15) are dropped, the first "alpha" (at 0) stays.
        out.clear();
        std::bitset<256> skip; skip.set(5);
        wxnCollectWords(doc, "alph", out, skip, [](size_t p){ return p >= 6 ? 5 : 0; });
        check(out.size() == 1 && out.count("alpha"),
              "collectWords: candidates whose start position is a prose style are dropped");

        // An out-of-range style byte must not index the bitset out of bounds.
        out.clear();
        wxnCollectWords(doc, "alph", out, skip, [](size_t){ return 9999; });
        check(out.size() == 2, "collectWords: an out-of-range style is treated as not-prose, not a crash");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
