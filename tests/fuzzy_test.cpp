// SPDX-License-Identifier: Apache-2.0
//
// Pure unit test for src/fuzzy_match.h - no wx, no Scintilla, no display. Mirrors the shape of
// tests/hash_test.cpp and tests/diff_myers_test.cpp so it runs in the same pure-ctest loop.
//
// What is pinned here is RANKING, not exact scores: the constants are tunable, but the orderings
// below are the behaviour users actually feel, so a tuning change that reverses one of them is a
// regression even if every individual number still looks reasonable.

#include "fuzzy_match.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const char* what)
{
    if (cond) { ++g_pass; std::printf("ok    %s\n", what); }
    else      { ++g_fail; std::printf("FAIL  %s\n", what); }
}

static int sc(const char* needle, const char* hay, bool isPath = false)
{
    const fuzzy::Result r = fuzzy::matchOne(needle, hay, isPath);
    return r.ok ? r.score : -1;
}

int main()
{
    // ---- (a) acronyms outrank scattered matches -------------------------------------------------
    // The headline behaviour: initials should find the thing you meant.
    check(sc("otv", "OpenTheVault") > sc("otv", "outvoted"),
          "acronym OpenTheVault beats scattered outvoted");
    // NOT asserted: camel vs separator acronyms. "o_t_v_..." is an equally good acronym match, and
    // kCamel == kBoundary by design, so which wins comes down to gap tightness - an arbitrary tie to
    // pin. What must hold is that both beat a non-acronym match, which the assertions here cover.
    check(sc("otv", "o_t_v_lowercase_noise") > sc("otv", "outvoted"),
          "separator acronym also beats a scattered in-word match");
    check(sc("cp", "Command Palette") > sc("cp", "Copy Path Component"),
          "two-word initials beat an in-word match");

    // ---- (b) the filename matters more than the directory ---------------------------------------
    check(sc("main", "src/main.cpp", true) > sc("main", "mainframe/util.h", true),
          "match in the filename beats match in a directory");
    check(sc("fuzzy", "src/fuzzy_match.h", true) > sc("fuzzy", "fuzzy/deep/nested/other.cpp", true),
          "filename hit outranks a directory hit at the same depth");

    // ---- (c) consecutive runs beat scattered characters ------------------------------------------
    check(sc("func", "funclist_selftest.cpp") > sc("func", "f_u_n_c.txt"),
          "consecutive run beats separator-scattered");
    check(sc("abc", "abcdef") > sc("abc", "aXbXcX"),
          "tight run beats gapped run");

    // ---- (d) non-subsequences do not match --------------------------------------------------------
    check(!fuzzy::matchOne("xyz", "OpenTheVault").ok, "non-subsequence rejected");
    check(!fuzzy::matchOne("cba", "abc").ok,          "order matters - reversed needle rejected");
    check(fuzzy::matchOne("", "anything").ok,         "empty needle matches");

    // ---- (e) smart case ---------------------------------------------------------------------------
    check(!fuzzy::matchOne("Main", "mainframe").ok, "uppercase needle is case-sensitive");
    check(fuzzy::matchOne("Main", "MainFrame").ok,  "uppercase needle matches exact case");
    check(fuzzy::matchOne("main", "mainframe").ok,  "lowercase needle is case-insensitive");
    check(fuzzy::matchOne("main", "MainFrame").ok,  "lowercase needle matches uppercase text");

    // ---- (f) UTF-8: code points, never bytes ------------------------------------------------------
    // "żółw" is four code points but seven bytes. A byte-wise matcher would let the first byte of
    // 'ż' (0xC5) match, and would return positions that split the character.
    {
        const fuzzy::Result r = fuzzy::matchOne("\xC5\xBC", "\xC5\xBC\xC3\xB3\xC5\x82w.txt");
        check(r.ok, "multi-byte needle matches its own code point");
        check(r.pos.size() == 1 && r.pos[0] == 0, "position is a code-point index, not a byte offset");
        // 'z' must NOT match 'ż' - they are different code points, and ASCII folding leaves >=0x80 alone
        check(!fuzzy::matchOne("z", "\xC5\xBC\xC3\xB3\xC5\x82w").ok,
              "ASCII 'z' does not match 'z-with-dot'");
    }
    {
        const fuzzy::Item it = fuzzy::build("\xC5\xBC\xC3\xB3\xC5\x82w.txt", false);
        check(it.cp.size() == 8, "8 code points decoded from 11 bytes");
    }

    // ---- (g) bounds: no quadratic blow-up ---------------------------------------------------------
    {
        const std::string longNeedle(40, 'a');
        const std::string longHay(4000, 'a');
        const fuzzy::Result r = fuzzy::matchOne(longNeedle, longHay);
        check(r.ok, "oversized input still produces a match");
        check(r.pos.size() == 40, "oversized input returns one position per needle character");
    }

    // ---- (h) determinism ---------------------------------------------------------------------------
    {
        const int a1 = sc("cfg", "config.h"), a2 = sc("cfg", "config.h");
        check(a1 == a2, "scoring is deterministic across calls");
    }

    // ---- (i) path tail detection -------------------------------------------------------------------
    {
        const fuzzy::Item posix = fuzzy::build("a/b/c.txt", true);
        const fuzzy::Item win   = fuzzy::build("a\\b\\c.txt", true);
        check(posix.tailAt == 4, "tailAt after the last forward slash");
        check(win.tailAt == 4,   "tailAt after the last backslash");
        check(fuzzy::build("a/b/c.txt", false).tailAt == 0, "non-path candidates have no tail");
    }

    // ---- (j) boundary bonuses ----------------------------------------------------------------------
    {
        const fuzzy::Item it = fuzzy::build("open_the-vault.x", false);
        check(it.bonus[0] == fuzzy::BonusBoundary,      "index 0 is a boundary");
        check(it.bonus[5] == fuzzy::BonusBoundary,      "after '_' is a boundary");
        check(it.bonus[9] == fuzzy::BonusBoundary,      "after '-' is a boundary");
        const fuzzy::Item cam = fuzzy::build("openThe", false);
        check(cam.bonus[4] == fuzzy::BonusCamel,        "lower->Upper is a camel hump");
        const fuzzy::Item dig = fuzzy::build("utf8", false);
        check(dig.bonus[3] == fuzzy::BonusDigitEdge,    "letter->digit is a digit edge");
    }

    // ---- (k) the examples the CHANGELOG and the user docs promise ---------------------------------
    // These are advertised behaviour, so they are pinned here: a tuning change that breaks one of them
    // makes the shipped documentation wrong, which is a defect even if every ranking test still passes.
    {
        check(fuzzy::matchOne("fmh", "fuzzy_match.h", true).ok, "documented: 'fmh' finds fuzzy_match.h");
        check(fuzzy::matchOne("parse", "parseFuncList").ok,     "documented: '@parse' finds parseFuncList");
        // The third documented promise - "a file-name match outranks the same letters buried in a
        // directory" - is pinned by group (b) above and deliberately NOT re-asserted here against a
        // path like "f/m/h/other.txt". That candidate is every letter as a directory INITIAL: three
        // boundary bonuses with single-character gaps, which is a genuinely excellent acronym match and
        // is supposed to rank high. Asserting it loses would be pinning a bug, not the promise.
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
