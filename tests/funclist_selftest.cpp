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

#include <chrono>   // --bench mode
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

// Membership probe for names() results - the one-line form of the bool-saw scans it replaces.
static bool contains(const std::vector<std::string>& v, const std::string& s)
{ return std::find(v.begin(), v.end(), s) != v.end(); }

// The symbol names flCollect extracts, in document order.
static std::vector<std::string> names(const std::string& text, const std::string& lang,
                                      const FLZones* prose = nullptr)
{
    std::vector<std::string> out;
    for (const FLSym& s : flCollect(text, lang, prose)) out.push_back(std::string(s.name.utf8_str()));
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

// Prose spans covering every occurrence of `needle` - stands in for what the live editor gets from the
// active lexer's style bytes (see flProseZones), so the lexer path is testable with no editor at all.
// Built left to right, so the result is sorted and non-overlapping, exactly as flProseZones guarantees.
static FLZones zonesOf(const std::string& text, const std::string& needle)
{
    FLZones z;
    for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1))
        z.push_back({ p, std::min(p + needle.size(), text.size()) });
    return z;
}

// Is `child` nested inside `parent`'s container body range? That is what drives the tree's shape.
static bool nestedIn(const std::string& text, const std::string& lang,
                     const std::string& parent, const std::string& child,
                     const FLZones* prose = nullptr)
{
    const std::vector<FLSym> syms = flCollect(text, lang, prose);
    const FLSym* p = nullptr;
    for (const FLSym& s : syms) if (std::string(s.name.utf8_str()) == parent) { p = &s; break; }
    if (!p || p->kind != 1) return false;
    for (const FLSym& s : syms)
        if (std::string(s.name.utf8_str()) == child) return s.pos > p->pos && s.pos < p->rangeEnd;
    return false;
}

// `funclist_selftest --bench`: measure the per-keystroke autocomplete harvest, old shape vs new.
// Not a ctest (timings assert nothing); it exists so the windowed-harvest change is justified by a
// number anyone can reproduce, not by an adjective. Copy + scan per rep, mirroring the real path
// (rangeText copies the range, wxnCollectWords walks it).
static void bench()
{
    std::string doc; doc.reserve(64u << 20);
    for (unsigned k = 0; doc.size() < (64u << 20); ++k)   // dense, repeating identifiers; "wor" matches all
        { doc += "word"; doc += std::to_string(k % 10000); doc += ' '; }
    auto run = [&](const char* label, size_t bytes, int reps) {
        const auto t0 = std::chrono::steady_clock::now();
        size_t found = 0;
        for (int r = 0; r < reps; ++r) {
            std::set<std::string, std::less<>> out;
            const std::string win = doc.substr(0, bytes);              // the copy rangeText would make
            wxnCollectWords(win, "wor", out, std::bitset<256>(), [](size_t){ return 0; });
            found = out.size();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
        std::printf("  %-34s %8.2f ms/keystroke   (%zu candidates)\n", label, ms, found);
    };
    std::printf("autocomplete harvest, copy+scan per keystroke:\n");
    run("whole doc, 64 MiB (old, unbounded)", 64u << 20, 5);
    run("whole doc, 16 MiB (old, mid band)",  16u << 20, 10);
    run("window,     1 MiB (new)",             1u << 20, 50);
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--bench") { bench(); return 0; }
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
        check(!contains(got, "func"), "swift: `class func` is not read as a type named \"func\"");
        check(contains(got, "origin"), "swift: `class func origin()` is listed as the function `origin`");
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

    // ---- wxnCollectWords edge clipping: a window cut mid-word must not offer the fragment ------------
    // On documents past the harvest cap, autoComplete scans a window around the caret. A cut edge can
    // land inside a word - the tail of "mybuffer" reads as a perfectly plausible "buffer" that exists
    // nowhere in the document - so runs touching a clipped edge are dropped, and ONLY those.
    {
        std::set<std::string, std::less<>> out;
        std::bitset<256> noSkip;
        auto st0 = [](size_t){ return 0; };

        // Head edge cut mid-word: window text begins inside "...mybuffer". Fragment dropped; rest kept.
        wxnCollectWords("buffer buffet bufalo\n", "buf", out, noSkip, st0, /*clipHead=*/true, /*clipTail=*/false);
        check(out.size() == 2 && out.count("buffet") && out.count("bufalo") && !out.count("buffer"),
              "collectWords: a head-cut word fragment is dropped, interior words kept");

        // Tail edge cut mid-word: the final run touches the window end. Dropped; earlier words kept.
        out.clear();
        wxnCollectWords("buffet bufalo buffe", "buf", out, noSkip, st0, /*clipHead=*/false, /*clipTail=*/true);
        check(out.size() == 2 && out.count("buffet") && out.count("bufalo") && !out.count("buffe"),
              "collectWords: a tail-cut word fragment is dropped, earlier words kept");

        // Clip flags set, but the edges land on non-word bytes: nothing is truncated, nothing dropped.
        out.clear();
        wxnCollectWords(" buffet bufalo \n", "buf", out, noSkip, st0, /*clipHead=*/true, /*clipTail=*/true);
        check(out.size() == 2 && out.count("buffet") && out.count("bufalo"),
              "collectWords: clip flags drop nothing when the cut edges land on whitespace");

        // Both edges cut in a two-word window: everything is a fragment, nothing offered.
        out.clear();
        wxnCollectWords("buffer buffe", "buf", out, noSkip, st0, /*clipHead=*/true, /*clipTail=*/true);
        check(out.empty(), "collectWords: a window whose every run touches a cut edge offers nothing");

        // Default arguments preserve the pre-window behaviour (the four tests above this block).
        out.clear();
        wxnCollectWords("buffer buffe", "buf", out, noSkip, st0);
        check(out.size() == 2, "collectWords: without clip flags, edge runs are ordinary candidates");
    }

    // ---- wxnHarvestWindow: the caret-centered window is exactly cap bytes, clamped, redistributed ----
    {
        auto win = [](long long docLen, long long caret, long long cap) {
            return wxnHarvestWindow(docLen, caret, cap);
        };
        check(win(500, 250, 1000) == std::make_pair(0LL, 500LL),
              "harvestWindow: a document within the cap is harvested whole");
        check(win(1000, 250, 1000) == std::make_pair(0LL, 1000LL),
              "harvestWindow: a document exactly at the cap is harvested whole");
        check(win(10000, 5000, 1000) == std::make_pair(4500LL, 5500LL),
              "harvestWindow: mid-document, the window is centered on the caret");
        check(win(10000, 100, 1000) == std::make_pair(0LL, 1000LL),
              "harvestWindow: near the top, the head surplus is redistributed to the tail");
        check(win(10000, 9900, 1000) == std::make_pair(9000LL, 10000LL),
              "harvestWindow: near the bottom, the tail surplus is redistributed to the head");
        check(win(10000, 0, 1000) == std::make_pair(0LL, 1000LL), "harvestWindow: caret at byte 0");
        check(win(10000, 10000, 1000) == std::make_pair(9000LL, 10000LL), "harvestWindow: caret at EOF");
        const auto [ws, we] = win(10000, 5000, 1000);
        check(we - ws == 1000, "harvestWindow: the window spans exactly the cap");
    }

    // ---- wxnBackupThrottleMs: the crash-backup cadence stretch for big buffers -----------------------
    {
        const long long MiB = 1ll << 20;
        check(wxnBackupThrottleMs(0)        == 0, "backupThrottle: an empty buffer is never throttled");
        check(wxnBackupThrottleMs(16 * MiB) == 0, "backupThrottle: 16 MiB keeps every 30 s tick");
        check(wxnBackupThrottleMs(32 * MiB) == 0, "backupThrottle: 32 MiB is the last unthrottled size");
        check(wxnBackupThrottleMs(33 * MiB) == 30000, "backupThrottle: just past the knee waits one extra tick");
        check(wxnBackupThrottleMs(64 * MiB) == 60000, "backupThrottle: 64 MiB backs up every other tick");
        check(wxnBackupThrottleMs(320 * MiB) == 300000, "backupThrottle: the stretch caps at 5 minutes");
        check(wxnBackupThrottleMs(4096 * MiB) == 300000, "backupThrottle: 4 GiB still caps at 5 minutes");
    }

    // ---- prose mask: the lexer's comment/string verdict beats flCollect's own regex approximation ----
    // The live editor passes flProseMask() here; these feed the same shape by hand. The point of the
    // mask is the braces it hides: the regex only knows the comment/string forms someone wrote a pattern
    // for, so a brace inside anything else is counted as real nesting and swallows the rest of the file.
    {
        // A MULTI-LINE C++ raw string holding an unbalanced '{'. The regex mask cannot reach this: its
        // string alternative is "(?:\\.|[^"\\\n])* ", which by construction cannot cross a newline, so
        // the brace is counted, Outer's body never closes, and everything after it becomes its child.
        // (A single-line raw string is NOT a good test here - it happens to read as an ordinary quoted
        // string, so the regex masks it by luck and the case passes with or without the fix.)
        const std::string lit = "R\"(\n    { unbalanced\n)\"";
        const std::string raw =
            "class Outer {\n"
            "    const char* s = " + lit + ";\n"
            "    void a() {}\n"
            "};\n"
            "void after() {}\n";
        const FLZones litZones = zonesOf(raw, lit);
        check(!nestedIn(raw, "cpp", "Outer", "after", &litZones),
              "prose mask: a brace inside a multi-line raw string does not extend the class body");
        check(nestedIn(raw, "cpp", "Outer", "a", &litZones),
              "prose mask: the class's own method is still nested inside it");
        // The contrast that justifies the mask: unmasked, the same buffer mis-nests.
        check(nestedIn(raw, "cpp", "Outer", "after"),
              "prose mask: without it, the regex mask really does mis-nest this buffer");

        // A masked span also suppresses extraction, not just brace counting.
        const std::string commented =
            "void real() {}\n"
            "// void fake() {}\n";
        const FLZones cz = zonesOf(commented, "// void fake() {}");
        const std::vector<std::string> got = names(commented, "cpp", &cz);
        check(got.size() == 1 && got[0] == "real", "prose mask: a symbol inside a masked span is not extracted");

        // The presence test is the POINTER, not emptiness. An empty-but-supplied list is the lexer
        // positively reporting "this file has no comments or strings", and must NOT quietly fall back
        // to the regex - otherwise a file the lexer cleared would still be second-guessed by a pattern.
        // A stray brace inside a // comment discriminates the two: the regex fallback masks it, an
        // empty supplied list does not, so the nesting differs and the branch actually taken is visible.
        const std::string braceInComment =
            "class Outer {\n"
            "    // a stray brace { lives in this comment\n"
            "    void a() {}\n"
            "};\n"
            "void after() {}\n";
        const FLZones none;
        check(nestedIn(braceInComment, "cpp", "Outer", "after", &none),
              "prose mask: an EMPTY supplied zone list means 'no comments' - the brace is counted");
        check(!nestedIn(braceInComment, "cpp", "Outer", "after"),
              "prose mask: no zone list at all falls back to the regex, which masks that comment");
    }

    // ---- plugin state (plugins.dat): disabled + queued-for-uninstall round-trip -------------------
    {
        std::set<std::string> dis, uni, d2, u2;
        dis.insert("udl_compat.dll");
        dis.insert("my plugin with spaces.dll");   // file names may contain spaces: the payload is the
        uni.insert("old_thing.dll");               // rest of the line, not the next token
        check(wxnParsePluginState(wxnSerializePluginState(dis, uni), d2, u2),
              "plugins.dat: current format version parses");
        check(d2 == dis, "plugins.dat: the disabled set round-trips, spaces in file names included");
        check(u2 == uni, "plugins.dat: the queued-uninstall set round-trips");

        // Keys are compared lowercased everywhere (nibIsDisabled lowercases before lookup), so a file
        // recorded in mixed case must come back lowercased or a disable would silently stop matching.
        std::set<std::string> mixed, back, none2;
        mixed.insert("MiXeD.DLL");
        wxnParsePluginState("wxn-plugins 1\nD MiXeD.DLL\n", back, none2);
        check(back.size() == 1 && *back.begin() == "mixed.dll", "plugins.dat: keys normalise to lowercase");

        // A newer format version is refused outright rather than partly read - the caller then leaves
        // the file alone instead of rewriting it and dropping what it could not represent.
        std::set<std::string> a, b;
        check(!wxnParsePluginState("wxn-plugins 2\nD x.dll\n", a, b), "plugins.dat: a newer version is refused");
        check(a.empty() && b.empty(), "plugins.dat: ... and yields nothing rather than a partial set");

        // Junk is skipped, and an unknown leading tag contributes nothing.
        std::set<std::string> c, d;
        check(wxnParsePluginState("wxn-plugins 1\nnonsense\nX y.dll\nD ok.dll\n", c, d),
              "plugins.dat: junk and unknown tags are skipped");
        check(c.size() == 1 && *c.begin() == "ok.dll" && d.empty(), "plugins.dat: ... leaving only the valid row");
    }

    // ---- saved Run commands: the runcommands.dat format round-trips -------------------------------
    // Free functions precisely so this needs no frame. The fields are base64'd because either may hold
    // a space, a quote or a newline, and the format is line-oriented - so those are what get tested.
    {
        std::vector<SavedRun> in = {
            { 1, "Build",              "cmake --build build" },
            { 7, "Open in \"Notepad\"", "notepad \"$(FULL_CURRENT_PATH)\"" },
            { 9, "Multi\nline name",   "echo a\nb" },
        };
        std::vector<SavedRun> out; long nextUid = 0;
        check(wxnParseRuns(wxnSerializeRuns(in, 42), out, nextUid), "runs: current format version parses");
        check(out.size() == 3, "runs: all three commands survive the round trip");
        check(nextUid == 42, "runs: nextUid is carried through");
        bool same = out.size() == in.size();
        for (size_t i = 0; same && i < in.size(); ++i)
            same = out[i].uid == in[i].uid && out[i].name == in[i].name && out[i].cmd == in[i].cmd;
        check(same, "runs: uid, name and command all round-trip verbatim (spaces, quotes, newlines)");

        // A uid on disk at or above the stored nextUid must push nextUid past it, or the next saved
        // command would reuse a uid and inherit a shortcut bound to the old one.
        std::vector<SavedRun> hi = { { 500, "X", "x" } };
        std::vector<SavedRun> back; long n2 = 0;
        wxnParseRuns(wxnSerializeRuns(hi, 1), back, n2);
        check(n2 == 501, "runs: nextUid is pulled ahead of the highest uid on disk");

        // A newer format version is refused rather than silently truncated - the caller marks the set
        // read-only so saving cannot drop commands it could not represent.
        std::vector<SavedRun> none; long n3 = 0;
        check(!wxnParseRuns("wxn-runs 2\nnext 5\n", none, n3), "runs: a newer format version is refused");
        check(none.empty(), "runs: ... and yields no commands rather than a partial set");

        // Garbage lines are skipped, not fatal.
        std::vector<SavedRun> ok; long n4 = 0;
        check(wxnParseRuns("wxn-runs 1\nnonsense\nR notanumber zz zz\n", ok, n4), "runs: junk lines are skipped");
        check(ok.empty(), "runs: ... and contribute no commands");
    }

    // ---- call-tip signatures: the extractor shared by the keystroke path and the workspace index ---
    {
        const std::string doc =
            "int add(int a, int b) { return a + b; }\n"
            "static void log_line(const char* msg,\n"
            "                     int level)\n"
            "{\n}\n"
            "void caller() { add(1, 2); }\n";

        // wxnSigAt: the '(' position and the identifier start are the caller's job; this normalizes.
        const size_t op = doc.find("add(");
        check(wxnSigAt(doc, op, op + 3) == "int add(int a, int b)",
              "sigAt: picks up the preceding return type");

        // A signature split across lines collapses to one line - a call tip is one line.
        const size_t lp = doc.find("log_line(");
        check(wxnSigAt(doc, lp, lp + 8) == "void log_line(const char* msg, int level)",
              "sigAt: a multi-line parameter list collapses to single spaces");

        // Unbalanced parens yield nothing rather than a truncated half-signature.
        const std::string bad = "void oops(int a\n";
        check(wxnSigAt(bad, 5, 9).empty(), "sigAt: an unclosed paren yields no signature");

        // wxnIndexSigs: one pass, every name.
        std::map<std::string, std::vector<std::string>> idx;
        wxnIndexSigs(doc, idx);
        check(idx.count("add") == 1,      "indexSigs: finds a definition");
        check(idx.count("log_line") == 1, "indexSigs: finds a multi-line definition");
        check(idx.count("caller") == 1,   "indexSigs: finds every name, not just the first");
        // The CALL site "add(1, 2)" is a second signature for the same name - both are kept, and the
        // definition is what makes the tip useful, so it must not be crowded out.
        check(std::find(idx["add"].begin(), idx["add"].end(), "int add(int a, int b)") != idx["add"].end(),
              "indexSigs: the definition survives alongside the call site");

        // "3(x)" is not a call; a bare "(" with no identifier is not either.
        std::map<std::string, std::vector<std::string>> idx2;
        wxnIndexSigs("x = 3(y);\nz = (a + b);\n", idx2);
        check(idx2.find("3") == idx2.end(), "indexSigs: a number before '(' is not a call");
        check(idx2.empty(), "indexSigs: a parenthesised expression contributes nothing");

        // The per-name overload cap holds, so one heavily-overloaded name cannot grow without bound.
        std::string many;
        for (int i = 0; i < 30; ++i) many += "void f(int a" + std::to_string(i) + ") {}\n";
        std::map<std::string, std::vector<std::string>> idx3;
        wxnIndexSigs(many, idx3, /*perName=*/8);
        check(idx3["f"].size() <= 8, "indexSigs: the per-name overload cap is enforced");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
