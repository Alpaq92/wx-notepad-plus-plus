// SPDX-License-Identifier: Apache-2.0
//
// wxNote - self-test for the PCRE2 search backend (src/regex_engine.h).
// Copyright 2026 The wxNote Authors.
//
// Heavy on the three things the old std::regex path got wrong, because those are the regressions that
// would be invisible if they came back: matching ACROSS a line break, lookbehind/named groups, and
// \U/\L/\E in replacements. Then the iteration hazards - zero-width matches, UTF-8 boundaries - which
// are how a Replace All turns into an infinite loop or a mangled document.
//
//   cmake --build build --target regex_test && build/bin/regex_test

#include "regex_engine.h"

#include <cstdio>
#include <string>

using namespace wxnre;

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& what)
{
    if (ok) { ++g_pass; std::printf("  ok    %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL  %s\n", what.c_str()); }
}

static void eq(const std::string& got, const std::string& want, const std::string& what)
{
    check(got == want, what + (got == want ? "" : "  [got \"" + got + "\" want \"" + want + "\"]"));
}

// Compile + first match; returns the matched text, or "<none>" / "<badpattern>".
static std::string firstMatch(const std::string& pat, const std::string& hay, Options o = Options{})
{
    Regex re;
    if (!re.compile(pat, o)) return "<badpattern>";
    Match m;
    if (!re.search(hay, 0, hay.size(), m)) return "<none>";
    return hay.substr(m.start, m.end - m.start);
}

// Replace every match, the way Replace All drives the engine.
static std::string replaceAll(const std::string& pat, const std::string& repl,
                              const std::string& hay, Options o = Options{})
{
    Regex re;
    if (!re.compile(pat, o)) return "<badpattern>";
    std::string out;
    size_t at = 0;
    int guard = 0;
    Match m;
    while (at <= hay.size() && re.search(hay, at, hay.size(), m))
    {
        if (++guard > 10000) return "<runaway>";
        out += hay.substr(at, m.start - at);
        out += Regex::expand(repl, hay, m);
        const size_t next = Regex::advanceOver(hay.data(), hay.size(), m.start, m.end);
        if (m.empty() && m.start < hay.size())
            out += hay.substr(m.start, next - m.start);   // keep the character we stepped over
        at = next;
    }
    if (at <= hay.size()) out += hay.substr(at > hay.size() ? hay.size() : at);
    return out;
}

int main()
{
    std::printf("== regex_test ==\n\n");

    // ---- (a) THE headline fix: patterns that cross a line break ---------------------------------
    // Every one of these was impossible before: the matcher was handed one line at a time, with the
    // EOL bytes excluded from the range.
    eq(firstMatch("a\\nb", "xa\nby"),                "a\nb",          "multiline: \\n matches a real line break");
    eq(firstMatch("BEGIN[\\s\\S]*?END", "x BEGIN\n1\n2\n END y"),
       "BEGIN\n1\n2\n END",                                            "multiline: a lazy any-char span crosses lines");
    eq(firstMatch("foo\\r\\nbar", "foo\r\nbar"),     "foo\r\nbar",    "multiline: CRLF documents work too");
    {
        // "join wrapped lines" - the archetypal forum recipe.
        eq(replaceAll("\\n\\s+", " ", "one\n   two\n   three"), "one two three",
           "multiline: collapsing a newline plus indent (the join-lines recipe)");
    }
    {
        // "delete everything between two markers", inclusive.
        eq(replaceAll("<!--[\\s\\S]*?-->", "", "a<!--\nx\ny\n-->b"), "ab",
           "multiline: deleting a block comment that spans lines");
    }
    // '.' still stops at a line break unless asked otherwise - matching every other editor.
    eq(firstMatch("a.b", "a\nb"), "<none>", "multiline: '.' does not cross a line break by default");
    {
        Options o; o.dotAll = true;
        eq(firstMatch("a.b", "a\nb", o), "a\nb", "multiline: dotAll lets '.' cross one");
    }

    // ---- (b) ^ and $ are per-line ----------------------------------------------------------------
    eq(firstMatch("^two$", "one\ntwo\nthree"), "two", "anchors: ^ and $ match at line boundaries");
    eq(replaceAll("^", "> ", "a\nb"),          "> a\n> b",  "anchors: ^ matches once per line");

    // ---- (c) constructs std::regex simply did not have --------------------------------------------
    eq(firstMatch("(?<=\\$)\\d+", "cost $42 now"),        "42",   "lookbehind: positive");
    eq(firstMatch("(?<!\\$)\\b\\d+", "$42 and 7"),        "7",    "lookbehind: negative");
    eq(firstMatch("\\d+(?= dollars)", "5 euros 9 dollars"), "9",  "lookahead: positive");
    eq(firstMatch("(?<year>\\d{4})-\\d\\d", "on 2026-08-03"), "2026-08", "named groups: compile and match");
    eq(firstMatch("(?i)HELLO", "say hello"),              "hello", "inline flags: (?i) works");
    eq(firstMatch("a(?>bc|b)c", "abcc"),                  "abcc",  "atomic groups compile");

    // ---- (d) named + numbered group capture -------------------------------------------------------
    {
        Regex re;
        Options o; o.matchCase = true;
        check(re.compile("(?<y>\\d{4})-(?<m>\\d\\d)", o), "groups: named pattern compiles");
        Match m;
        check(re.search("2026-08", 0, 7, m), "groups: it matches");
        check(m.groups() == 2, "groups: two capture groups reported");
        eq(Regex::expand("$2/$1", "2026-08", m), "08/2026", "groups: $1/$2 expand by number");
    }

    // ---- (e) replacement templates - the \U/\L/\E that used to land as literal text ---------------
    eq(replaceAll("(\\w+)", "\\U$1", "abc def"),      "ABC DEF",   "replace: \\U upper-cases the group");
    eq(replaceAll("(\\w+)", "\\L$1", "ABC DEF"),      "abc def",   "replace: \\L lower-cases it");
    eq(replaceAll("(a+)(b+)", "\\U$1\\E$2", "aabb"),  "AAbb",      "replace: \\E ends the case span");
    eq(replaceAll("(\\w)(\\w*)", "\\u$1$2", "bob"),   "Bob",       "replace: \\u affects one character only");
    eq(replaceAll("(\\w)(\\w*)", "\\l$1$2", "BOB"),   "bOB",       "replace: \\l likewise");
    eq(replaceAll("x", "\\1", "x"),                   "",          "replace: a group that does not exist expands empty");
    eq(replaceAll("(a)", "\\1\\1", "a"),              "aa",        "replace: backslash-N is accepted as well as $N");
    eq(replaceAll("(a)", "${1}z", "a"),               "az",        "replace: ${1} disambiguates from following digits");
    eq(replaceAll("a", "$0!", "a"),                   "a!",        "replace: $0 is the whole match");
    eq(replaceAll("a", "x\\ty", "a"),                 "x\ty",      "replace: \\t is a tab");
    eq(replaceAll("a", "x\\ny", "a"),                 "x\ny",      "replace: \\n is a newline");
    eq(replaceAll("a", "50\\%", "a"),                 "50\\%",     "replace: an unknown escape keeps both characters");
    eq(replaceAll("a", "c:\\\\tmp", "a"),             "c:\\tmp",   "replace: \\\\ is a literal backslash");
    eq(replaceAll("a", "costs $", "a"),               "costs $",   "replace: a trailing $ is literal");

    // ---- (f) zero-width matches must not loop forever ---------------------------------------------
    eq(replaceAll("x*", "-", "abc"),   "-a-b-c-",  "zero-width: an all-optional pattern terminates");
    eq(replaceAll("\\b", "|", "ab cd"), "|ab| |cd|", "zero-width: word boundaries terminate");
    eq(replaceAll("(?=b)", "!", "abc"), "a!bc",     "zero-width: a bare lookahead terminates");
    {
        // The engine's own advance() is what guarantees this.
        Regex re;
        check(re.compile("", Options{}), "zero-width: an empty pattern compiles");
        Match m;
        check(re.search("ab", 0, 2, m) && m.empty(), "zero-width: the empty pattern matches empty");
        check(Regex::advanceOver("ab", 2, m.start, m.end) > m.start,
              "zero-width: advanceOver() always moves forward");
    }

    // ---- (g) UTF-8 correctness --------------------------------------------------------------------
    {
        // NOTE the split literals: a hex escape in C swallows every following hex digit, so
        // "\xC5\xBAb" is one out-of-range escape rather than "ź" + 'b'. MSVC rejects it outright;
        // a compiler that merely warned would have silently changed what these tests search for.
        const std::string pl = "z\xC5\xBA" "d\xC5\xBA" "b\xC5\x82o";     // "zźdźbło"
        eq(firstMatch("\xC5\xBA", pl), "\xC5\xBA",                       "utf8: a multi-byte literal matches");
        eq(firstMatch("^.", pl),        "z",                             "utf8: '.' is one CHARACTER, not one byte");
        // Stepping over a zero-width match must land on a character boundary, never mid-sequence.
        const std::string out = replaceAll("(?=\\w)", "|", "a\xC5\xBA" "b");
        eq(out, "|a|\xC5\xBA" "|b",                                     "utf8: zero-width iteration lands on character boundaries");
    }
    {
        // \w must be Unicode-aware (PCRE2_UCP), or "ź" would not count as a word character.
        eq(firstMatch("\\w+", "\xC5\xBA\xC3\xB3\xC5\x82w"), "\xC5\xBA\xC3\xB3\xC5\x82w",
           "utf8: \\w covers non-ASCII letters (UCP)");
    }
    {
        // An ill-formed byte must be a non-match, not a hard failure that breaks search entirely.
        const std::string bad = std::string("ok\xFF\xFE" "bad");
        Regex re;
        check(re.compile("ok", Options{}), "invalid utf8: pattern compiles");
        Match m;
        check(re.search(bad, 0, bad.size(), m) && m.start == 0,
              "invalid utf8: a buffer with bad bytes still searches (MATCH_INVALID_UTF)");
    }

    // ---- (h) case sensitivity and whole word ------------------------------------------------------
    {
        Options ci; ci.matchCase = false;
        Options cs; cs.matchCase = true;
        eq(firstMatch("hello", "say HELLO", ci), "HELLO",  "case: insensitive by default option");
        eq(firstMatch("hello", "say HELLO", cs), "<none>", "case: sensitive when asked");
    }
    {
        Options w; w.wholeWord = true; w.matchCase = true;
        eq(firstMatch("cat", "concatenate", w), "<none>", "wholeword: does not match inside a word");
        eq(firstMatch("cat", "a cat here", w),  "cat",    "wholeword: matches a standalone word");
        // The wrapper must bound the WHOLE alternation, not just its last branch.
        eq(firstMatch("a|bc", "xbcx", w),       "<none>", "wholeword: an alternation is bounded as a whole");
        eq(firstMatch("a|bc", "x bc x", w),     "bc",     "wholeword: and still matches when standalone");
        // A user's own groups must keep their numbers despite the wrapper's (?:...).
        Regex re;
        check(re.compile("(c)(a)t", w), "wholeword: a grouped pattern compiles wrapped");
        Match m;
        check(re.search("a cat", 0, 5, m) && m.groups() == 2, "wholeword: the wrapper does not renumber groups");
        eq(Regex::expand("$1$2", "a cat", m), "ca", "wholeword: groups still expand correctly");
    }

    // ---- (i) invalid patterns report a REASON ----------------------------------------------------
    {
        Regex re;
        std::string err;
        check(!re.compile("(unclosed", Options{}, &err), "error: an invalid pattern fails to compile");
        check(!err.empty(), "error: a message is produced");
        check(err.find("offset") != std::string::npos, "error: the message carries an offset");
        check(!re.ok(), "error: the object reports itself unusable");

        // The offset must describe the pattern the USER typed. Whole-word wraps it in "\b(?:...)\b",
        // so a raw PCRE2 offset would be 5 too large - and for errors reported at the END of a
        // pattern it would point past the text entirely. The invariant: wrapping changes nothing.
        Options w; w.wholeWord = true;
        for (const char* bad : { "(bad", "a{2,1}", "[z-a]", "*x", "(?<=a+)b" })
        {
            std::string plainErr, wrapErr;
            Regex r1, r2;
            const bool p1 = r1.compile(bad, Options{}, &plainErr);
            const bool p2 = r2.compile(bad, w, &wrapErr);
            check(p1 == p2, std::string("error: \"") + bad + "\" is rejected the same way wrapped or not");
            if (!p1 && !p2)
                check(plainErr == wrapErr,
                      std::string("error: \"") + bad + "\" reports an identical message and offset either way");
        }
    }

    // ---- (j) ranged search: [from, to) is respected, with context still visible -------------------
    {
        Regex re;
        Options cs; cs.matchCase = true;
        check(re.compile("b", cs), "range: compiles");
        Match m;
        check(re.search("abcb", 2, 4, m) && m.start == 3, "range: a match before `from` is skipped");
        check(!re.search("abcb", 0, 1, m),                "range: a match past `to` is not found");
    }
    {
        // Lookbehind must still see text BEFORE `from` - this is why the whole buffer is passed.
        Regex re;
        Options cs; cs.matchCase = true;
        check(re.compile("(?<=a)b", cs), "range: lookbehind pattern compiles");
        Match m;
        check(re.search("ab", 1, 2, m) && m.start == 1,
              "range: lookbehind sees text before the start offset");
    }
    {
        // ...and \b must know whether `from` is really a word boundary.
        Regex re;
        Options cs; cs.matchCase = true;
        check(re.compile("\\bcd", cs), "range: boundary pattern compiles");
        Match m;
        check(!re.search("abcd", 2, 4, m),
              "range: \\b is false mid-word even when the range starts there");
    }

    // ---- (k) backward search ---------------------------------------------------------------------
    {
        Regex re;
        Options cs; cs.matchCase = true;
        check(re.compile("a", cs), "backward: compiles");
        Match m;
        check(re.searchBackward("a_a_a", 0, 5, m) && m.start == 4, "backward: finds the LAST match");
        check(re.searchBackward("a_a_a", 0, 3, m) && m.start == 2, "backward: bounded by `to`");
        check(!re.searchBackward("xyz", 0, 3, m),                  "backward: reports no match");
        // A zero-width pattern must terminate here too.
        Regex re2;
        check(re2.compile("x*", cs), "backward: zero-width pattern compiles");
        Match m2;
        check(re2.searchBackward("abc", 0, 3, m2), "backward: zero-width search terminates");
    }

    // ---- (l) practical recipes, end to end --------------------------------------------------------
    eq(replaceAll("(\\w+)@(\\w+)", "$2 at $1", "me@here"),        "here at me",   "recipe: swap around a delimiter");
    eq(replaceAll("\\s+$", "", "trailing   "),                     "trailing",     "recipe: strip trailing whitespace");
    eq(replaceAll("^(\\w)", "\\u$1", "hello\nworld"),              "Hello\nWorld", "recipe: capitalise each line");
    eq(replaceAll("(?m)^\\s*\\n", "", "a\n\n   \nb"),              "a\nb",         "recipe: drop blank lines");
    eq(replaceAll("(\\d+)", "<$1>", "a1b22c"),                     "a<1>b<22>c",   "recipe: wrap every number");

    std::printf("\n%s  (%d passed, %d failed)\n", g_fail ? "FAILED" : "PASSED", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
