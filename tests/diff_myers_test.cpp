// SPDX-License-Identifier: Apache-2.0
//
// diff_myers_test - headless self-test for the Myers O(ND) diff engine (src/diff_myers.h). No GUI, no wx:
// the engine is pure STL, so this links nothing. Three layers are exercised:
//   * shortestEditScript() - trivial edges, the canonical Myers example, and a PROPERTY test that cross-
//     checks Myers' edit distance against an independent O(NM) DP over thousands of fixed-seed random pairs
//     (the strongest correctness proof: two unrelated algorithms must agree on D, and every script must
//     reproduce B from A via verifyScript()).
//   * diffLines()  - EOL-agnostic line splitting + Change coalescing + summary counts.
//   * diffChars()  - UTF-8 code-point intra-line diff returning byte spans (never splits a multibyte char).
//
//   cmake --build build --target diff_myers_test && build/bin/diff_myers_test
//
#include "diff_myers.h"

#include <cstdio>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

using namespace wxn::diff;

static int g_fail = 0;
static int g_pass = 0;
static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    if (ok) ++g_pass; else ++g_fail;
}

// ---- helpers ----------------------------------------------------------------------------------------

// Turn a string into a token vector (one int per byte) for the low-level SES tests.
static std::vector<int> toks(const std::string& s)
{
    std::vector<int> v; v.reserve(s.size());
    for (unsigned char c : s) v.push_back(c);
    return v;
}

// Independent reference: indel-only edit distance via O(NM) DP (== N+M-2*LCS == Myers' D). No shared code
// with the engine, so agreement is a real cross-check.
static int dpEditDistance(const std::vector<int>& a, const std::vector<int>& b)
{
    const int N = (int)a.size(), M = (int)b.size();
    std::vector<std::vector<int>> dp(N + 1, std::vector<int>(M + 1, 0));
    for (int i = 0; i <= N; ++i) dp[i][0] = i;
    for (int j = 0; j <= M; ++j) dp[0][j] = j;
    for (int i = 1; i <= N; ++i)
        for (int j = 1; j <= M; ++j)
            dp[i][j] = (a[i-1] == b[j-1]) ? dp[i-1][j-1]
                                          : 1 + std::min(dp[i-1][j], dp[i][j-1]);
    return dp[N][M];
}

// ---- Layer 1: shortestEditScript --------------------------------------------------------------------

static void testSesEdges()
{
    std::printf("[ses: trivial edges]\n");
    {
        Result r = shortestEditScript({}, {});
        check(r.edits.empty() && r.distance == 0, "both empty -> empty script, D=0");
    }
    {
        Result r = shortestEditScript({}, toks("abc"));
        check(r.distance == 3 && r.edits.size() == 3 && r.edits[0].op == Op::Insert, "A empty -> 3 inserts");
        check(verifyScript({}, toks("abc"), r.edits), "A empty -> script reproduces B");
    }
    {
        Result r = shortestEditScript(toks("abc"), {});
        check(r.distance == 3 && r.edits[0].op == Op::Delete, "B empty -> 3 deletes");
        check(verifyScript(toks("abc"), {}, r.edits), "B empty -> script reproduces B");
    }
    {
        Result r = shortestEditScript(toks("identical"), toks("identical"));
        bool allEq = r.distance == 0;
        for (auto& e : r.edits) if (e.op != Op::Equal) allEq = false;
        check(allEq && r.edits.size() == 9, "identical -> all Equal, D=0");
    }
}

static void testSesKnown()
{
    std::printf("[ses: known cases]\n");
    // Myers' canonical example: A=ABCABBA, B=CBABAC has edit distance 5.
    {
        auto a = toks("ABCABBA"), b = toks("CBABAC");
        Result r = shortestEditScript(a, b);
        check(r.distance == 5, "ABCABBA/CBABAC -> D=5 (Myers canonical)");
        check(verifyScript(a, b, r.edits), "ABCABBA/CBABAC -> valid script");
        check(r.distance == dpEditDistance(a, b), "ABCABBA/CBABAC -> matches DP");
    }
    // Disjoint sequences: full replacement.
    {
        auto a = toks("ABC"), b = toks("XYZ");
        Result r = shortestEditScript(a, b);
        check(r.distance == 6 && verifyScript(a, b, r.edits), "ABC/XYZ -> D=6, valid");
    }
    // Pure prefix add / suffix remove / repeats / single insert.
    {
        auto a = toks("ABC"), b = toks("ABCDE");
        Result r = shortestEditScript(a, b);
        check(r.distance == 2 && verifyScript(a, b, r.edits), "ABC/ABCDE -> D=2 (append)");
    }
    {
        auto a = toks("ABCDE"), b = toks("CDE");
        Result r = shortestEditScript(a, b);
        check(r.distance == 2 && verifyScript(a, b, r.edits), "ABCDE/CDE -> D=2 (drop prefix)");
    }
    {
        auto a = toks("AAAA"), b = toks("AA");
        Result r = shortestEditScript(a, b);
        check(r.distance == 2 && verifyScript(a, b, r.edits), "AAAA/AA -> D=2 (repeats)");
    }
    {
        auto a = toks("AC"), b = toks("ABC");
        Result r = shortestEditScript(a, b);
        check(r.distance == 1 && verifyScript(a, b, r.edits), "AC/ABC -> D=1 (insert middle)");
    }
}

static void testSesGuard()
{
    std::printf("[ses: cost guard]\n");
    Options opt; opt.maxCost = 2;
    auto a = toks("ABCDEFGH"), b = toks("XYZWVUTS");   // needs D=16, far past the cap
    Result r = shortestEditScript(a, b, opt);
    check(r.hitGuard, "guard fires when D would exceed maxCost");
    check(verifyScript(a, b, r.edits), "degraded script still reproduces B");
    // And it must NOT fire when the real distance is within budget.
    Options ok; ok.maxCost = 100;
    Result r2 = shortestEditScript(a, b, ok);
    check(!r2.hitGuard && r2.distance == dpEditDistance(a, b), "guard silent when budget suffices");
}

static void testSesProperty()
{
    std::printf("[ses: property cross-check vs DP over random pairs]\n");
    std::mt19937 rng(0xC0FFEEu);                 // fixed seed: deterministic, no Date/rand dependence
    std::uniform_int_distribution<int> lenD(0, 12);
    std::uniform_int_distribution<int> chD(0, 3);  // tiny alphabet forces frequent matches -> real snakes
    int cases = 0, bad = 0;
    for (int t = 0; t < 5000; ++t) {
        std::vector<int> a(lenD(rng)), b(lenD(rng));
        for (auto& x : a) x = chD(rng);
        for (auto& x : b) x = chD(rng);
        Result r = shortestEditScript(a, b);
        ++cases;
        if (!verifyScript(a, b, r.edits)) ++bad;
        if (r.distance != dpEditDistance(a, b)) ++bad;
    }
    // Second pass: larger alphabet + longer sequences to hit the large-D / k=+/-D-boundary regime the tiny
    // alphabet under-samples.
    std::uniform_int_distribution<int> lenD2(0, 40);
    std::uniform_int_distribution<int> chD2(0, 30);
    for (int t = 0; t < 3000; ++t) {
        std::vector<int> a(lenD2(rng)), b(lenD2(rng));
        for (auto& x : a) x = chD2(rng);
        for (auto& x : b) x = chD2(rng);
        Result r = shortestEditScript(a, b);
        ++cases;
        if (!verifyScript(a, b, r.edits)) ++bad;
        if (r.distance != dpEditDistance(a, b)) ++bad;
    }
    check(bad == 0, "8000 random pairs (2 alphabets): every script valid AND D == DP distance");
    std::printf("        (%d cases, %d failures)\n", cases, bad);
}

// ---- Layer 2: diffLines -----------------------------------------------------------------------------

static void testSplitLines()
{
    std::printf("[lines: splitLines]\n");
    check(splitLines("").empty(),                              "\"\" -> 0 lines");
    check(splitLines("a") == std::vector<std::string>({"a"}),  "\"a\" -> [a]");
    check(splitLines("a\n") == std::vector<std::string>({"a"}),"\"a\\n\" -> [a] (trailing EOL, no empty tail)");
    check(splitLines("a\nb") == std::vector<std::string>({"a","b"}),          "LF split");
    check(splitLines("a\r\nb") == std::vector<std::string>({"a","b"}),        "CRLF split");
    check(splitLines("a\rb") == std::vector<std::string>({"a","b"}),          "lone CR split");
    check(splitLines("\n") == std::vector<std::string>({""}),                 "\"\\n\" -> one empty line");
    check(splitLines("a\n\nb") == std::vector<std::string>({"a","","b"}),     "blank line preserved");
    check(splitLines("a\r\n\r\nb") == std::vector<std::string>({"a","","b"}), "CRLF blank line preserved");
    check(splitLines("a\r") == std::vector<std::string>({"a"}),               "trailing lone CR -> [a]");
    check(splitLines("\r") == std::vector<std::string>({""}),                 "\"\\r\" -> one empty line");
    check(splitLines("a\r\nb\r\n") == std::vector<std::string>({"a","b"}),    "trailing CRLF, no empty tail");
}

static void testDiffLines()
{
    std::printf("[lines: diffLines]\n");
    {
        LineDiff d = diffLines("one\ntwo\nthree", "one\ntwo\nthree");
        bool allEq = true; for (auto& r : d.rows) if (r.kind != Row::Equal) allEq = false;
        check(allEq && d.added == 0 && d.removed == 0 && d.changed == 0, "identical -> all Equal, 0/0/0");
    }
    {
        // EOL style must not register as a change.
        LineDiff d = diffLines("a\r\nb\r\nc", "a\nb\nc");
        check(d.added == 0 && d.removed == 0 && d.changed == 0, "CRLF vs LF, same content -> no changes");
    }
    {
        // Middle line changed -> one Change row pairing A[1] with B[1].
        LineDiff d = diffLines("one\ntwo\nthree", "one\nTWO\nthree");
        check(d.changed == 1 && d.added == 0 && d.removed == 0, "one changed line -> changed=1");
        bool found = false;
        for (auto& r : d.rows) if (r.kind == Row::Change && r.aLine == 1 && r.bLine == 1) found = true;
        check(found, "Change row pairs A line 1 with B line 1");
    }
    {
        // Pure append.
        LineDiff d = diffLines("a\nb", "a\nb\nc");
        check(d.added == 1 && d.removed == 0 && d.changed == 0, "append -> added=1");
        bool found = false;
        for (auto& r : d.rows) if (r.kind == Row::Insert && r.bLine == 2 && r.aLine == -1) found = true;
        check(found, "Insert row carries B line 2, aLine -1");
    }
    {
        // Pure delete.
        LineDiff d = diffLines("a\nb\nc", "a\nc");
        check(d.removed == 1 && d.added == 0 && d.changed == 0, "delete middle -> removed=1");
    }
    {
        // Row indices must be internally consistent: reconstruct A and B from the alignment.
        LineDiff d = diffLines("alpha\nbeta\ngamma\ndelta", "alpha\nBETA\ngamma\nDELTA\nepsilon");
        std::vector<int> aSeen, bSeen;
        for (auto& r : d.rows) {
            if (r.aLine >= 0) aSeen.push_back(r.aLine);
            if (r.bLine >= 0) bSeen.push_back(r.bLine);
        }
        bool aOk = aSeen == std::vector<int>({0,1,2,3});
        bool bOk = bSeen == std::vector<int>({0,1,2,3,4});
        check(aOk && bOk, "alignment covers every A line once (in order) and every B line once");
        check(d.changed == 2 && d.added == 1 && d.removed == 0, "multi-block: changed=2 (paired), added=1 (leftover), removed=0");
    }
}

// ---- Layer 3: diffChars -----------------------------------------------------------------------------

static bool spansEq(const std::vector<Span>& v, std::initializer_list<Span> exp)
{
    if (v.size() != exp.size()) return false;
    size_t i = 0;
    for (auto& s : exp) { if (v[i].start != s.start || v[i].len != s.len) return false; ++i; }
    return true;
}

// Independent check: deleting aDeleted spans from A must yield the same bytes as deleting bInserted from B.
static std::string removeSpans(const std::string& s, const std::vector<Span>& spans)
{
    std::string out; size_t next = 0;
    for (auto& sp : spans) { out.append(s, next, sp.start - next); next = sp.start + sp.len; }
    out.append(s, next, s.size() - next);
    return out;
}

static void testDiffChars()
{
    std::printf("[chars: diffChars]\n");
    {
        CharDiff d = diffChars("abcXYZdef", "abcPQdef");
        check(spansEq(d.aDeleted, {{3,3}}),  "aDeleted = [{3,3}] (XYZ)");
        check(spansEq(d.bInserted, {{3,2}}), "bInserted = [{3,2}] (PQ)");
    }
    {
        // UTF-8: 'é' is 2 bytes at offset 3; replaced by 1-byte 'e'. Must not split the multibyte char.
        CharDiff d = diffChars("caf\xC3\xA9", "cafe");
        check(spansEq(d.aDeleted, {{3,2}}),  "aDeleted = [{3,2}] (the 2-byte e-acute)");
        check(spansEq(d.bInserted, {{3,1}}), "bInserted = [{3,1}] (ascii e)");
    }
    {
        CharDiff d = diffChars("same", "same");
        check(d.aDeleted.empty() && d.bInserted.empty(), "identical line -> no spans");
    }
    {
        // Common-core invariant across a few pairs.
        const char* pairs[][2] = {
            {"hello world", "hello there"},
            {"the quick brown fox", "the slow brown cat"},
            {"\xE4\xBD\xA0\xE5\xA5\xBD", "\xE4\xBD\xA0\xE5\x97\xA8"},   // 你好 vs 你嗨 (CJK, 3 bytes each)
        };
        bool ok = true;
        for (auto& p : pairs) {
            CharDiff d = diffChars(p[0], p[1]);
            if (removeSpans(p[0], d.aDeleted) != removeSpans(p[1], d.bInserted)) ok = false;
        }
        check(ok, "common core matches after removing changed spans (incl. CJK)");
    }
    {
        CharDiff d = diffChars("aXbYc", "abc");   // two deletions split by a common 'b'
        check(spansEq(d.aDeleted, {{1,1},{3,1}}) && d.bInserted.empty(), "two non-adjacent deletions -> two spans (no merge across common)");
    }
    {
        CharDiff d = diffChars("", "abc");
        check(d.aDeleted.empty() && spansEq(d.bInserted, {{0,3}}), "empty A -> whole B as one inserted span");
    }
    {
        // BUG-1 regression: malformed / over-range / overlong UTF-8 must NOT alias to one token; the
        // common-core invariant must hold AND a real change must be detected (the aliasing bug reported none).
        const std::string a1("\x80", 1),           b1("\xF4\x90\x82\x80", 4);   // stray byte vs over-range 4-byte
        const std::string a2("\x00", 1),           b2("\xC0\x80", 2);           // NUL vs overlong-NUL
        CharDiff d1 = diffChars(a1, b1), d2 = diffChars(a2, b2);
        bool inv  = removeSpans(a1, d1.aDeleted) == removeSpans(b1, d1.bInserted)
                 && removeSpans(a2, d2.aDeleted) == removeSpans(b2, d2.bInserted);
        bool seen = !(d1.aDeleted.empty() && d1.bInserted.empty())
                 && !(d2.aDeleted.empty() && d2.bInserted.empty());
        check(inv && seen, "malformed/over-range UTF-8: invariant holds AND change is detected (no aliasing)");
    }
}

// ---- Layer 3b: tokenizer directly + guard propagation + byte fuzz -----------------------------------

// Assert a string tokenizes to exactly these per-token byte lengths, with contiguous offsets covering it.
static bool tokLens(const std::string& s, std::initializer_list<int> lens)
{
    auto t = detail::tokenizeUtf8(s);
    if (t.size() != lens.size()) return false;
    int off = 0; size_t i = 0;
    for (int L : lens) { if (t[i].len != L || t[i].off != off) return false; off += L; ++i; }
    return off == (int)s.size();   // every byte consumed exactly once
}

static void testTokenizer()
{
    std::printf("[chars: tokenizeUtf8 malformed]\n");
    check(tokLens("caf\xC3", {1,1,1,1}),            "truncated lead byte -> 1-byte sentinel");
    check(tokLens("\xC3\x41", {1,1}),               "bad continuation ('A') -> lead sentinel + 'A'");
    check(tokLens("\x80x", {1,1}),                  "lone continuation at start -> sentinel + 'x'");
    check(tokLens("\xF4\x90\x82\x80", {1,1,1,1}),   "over-range 4-byte (>U+10FFFF) -> four sentinels");
    check(tokLens("\xC0\x80", {1,1}),               "overlong NUL -> two sentinels");
    check(tokLens("\xED\xA0\x80", {1,1,1}),         "surrogate -> three sentinels");
    check(tokLens(std::string("\x00", 1), {1}),     "embedded NUL -> one valid cp-0 token");
    check(tokLens("caf\xC3\xA9", {1,1,1,2}),        "valid 2-byte e-acute -> len 2");
    check(tokLens("\xE4\xBD\xA0", {3}),             "valid 3-byte CJK -> len 3");
    auto t1 = detail::tokenizeUtf8("\x80");
    auto t2 = detail::tokenizeUtf8("\xF4\x90\x82\x80");
    check(!t1.empty() && !t2.empty() && t1[0].cp != t2[0].cp, "over-range seq does not alias a stray byte");
}

static void testGuardPropagation()
{
    std::printf("[guard: propagation + inclusive boundary]\n");
    Options ok; ok.maxCost = 2;      // diffChars("ABCD","AXCD") has char-distance 2 (inclusive cap completes)
    Options tight; tight.maxCost = 1;
    check(!diffChars("ABCD","AXCD", ok).hitGuard,    "diffChars maxCost == D completes (no guard, inclusive)");
    check( diffChars("ABCD","AXCD", tight).hitGuard, "diffChars maxCost < D degrades (guard set, propagated)");
    LineDiff d = diffLines("a\nb\nc", "x\ny\nz", tight);   // all lines distinct -> D=6, tight budget
    check(d.hitGuard, "diffLines guard fires on dissimilar input with tight budget");
    int changeRows = 0; for (auto& r : d.rows) if (r.kind == Row::Change) ++changeRows;
    check(d.changed == 0 && changeRows == 0, "guard-degraded diffLines emits NO fabricated Change rows");
}

static void testFuzzChars()
{
    std::printf("[chars: fuzz removeSpans invariant with random raw bytes]\n");
    std::mt19937 rng(0xBADC0DEu);
    // Byte menu skewed toward the tricky ones: NUL, continuation, 2/3/4-byte leads incl. over-range/invalid.
    const unsigned char menu[] = {0x00,0x41,0x42,0x80,0xA9,0xC3,0xC0,0xE4,0xED,0xF0,0xF4,0xFF};
    std::uniform_int_distribution<int> lenD(0, 8);
    std::uniform_int_distribution<int> byteD(0, (int)sizeof(menu) - 1);
    int bad = 0;
    for (int t = 0; t < 4000; ++t) {
        std::string a, b;
        int la = lenD(rng), lb = lenD(rng);
        for (int i = 0; i < la; ++i) a.push_back((char)menu[byteD(rng)]);
        for (int i = 0; i < lb; ++i) b.push_back((char)menu[byteD(rng)]);
        CharDiff d = diffChars(a, b);
        if (removeSpans(a, d.aDeleted) != removeSpans(b, d.bInserted)) ++bad;
    }
    check(bad == 0, "4000 random raw-byte pairs: removeSpans common-core invariant holds");
}

// ---- Layer 4: side-by-side compare plan -------------------------------------------------------------

// Full structural check of a ComparePlan against the two texts' line counts. This is the workhorse the
// property test leans on: it proves the two columns are aligned, cover every real line exactly once in
// order, never put a filler opposite a filler, and mark rows consistently.
static bool checkPlan(const ComparePlan& p, int NA, int NB)
{
    if (p.left.size() != p.right.size()) return false;
    std::vector<int> la, lb;
    for (size_t i = 0; i < p.left.size(); ++i) {
        const VisualRow& L = p.left[i];
        const VisualRow& R = p.right[i];
        if (L.filler && R.filler) return false;                       // never a filler opposite a filler
        if (L.filler) {
            if (L.line != -1 || L.marker != Marker::None) return false;
            if (R.filler || R.marker != Marker::Added) return false;  // A-filler <=> B is an Added real line
        } else {
            la.push_back(L.line);
        }
        if (R.filler) {
            if (R.line != -1 || R.marker != Marker::None) return false;
            if (L.filler || L.marker != Marker::Removed) return false; // B-filler <=> A is a Removed real line
        } else {
            lb.push_back(R.line);
        }
        if (!L.filler && !R.filler)                                    // both real => either both Changed or neither
            if ((L.marker == Marker::Changed) != (R.marker == Marker::Changed)) return false;
    }
    if ((int)la.size() != NA || (int)lb.size() != NB) return false;    // every real line present exactly once
    for (int i = 0; i < NA; ++i) if (la[i] != i) return false;         // ...and in order
    for (int i = 0; i < NB; ++i) if (lb[i] != i) return false;
    return true;
}

static bool runsEq(const std::vector<FillerRun>& v, std::initializer_list<FillerRun> exp)
{
    if (v.size() != exp.size()) return false;
    size_t i = 0; for (auto& e : exp) { if (v[i].afterLine != e.afterLine || v[i].count != e.count) return false; ++i; }
    return true;
}

static int nlines(const std::string& s) { return (int)splitLines(s).size(); }

static void testComparePlan()
{
    std::printf("[compare: side-by-side plan]\n");
    {
        ComparePlan p = compareTexts("a\nb\nc", "a\nb\nc");
        bool anyFiller = false, anyMark = false;
        for (auto& v : p.left)  { if (v.filler) anyFiller = true; if (v.marker != Marker::None) anyMark = true; }
        for (auto& v : p.right) { if (v.filler) anyFiller = true; if (v.marker != Marker::None) anyMark = true; }
        check(checkPlan(p, 3, 3) && !anyFiller && !anyMark, "identical -> 3 aligned rows, no fillers, no markers");
        check(fillerRuns(p.left).empty() && fillerRuns(p.right).empty(), "identical -> no filler runs");
    }
    {
        ComparePlan p = compareTexts("a\nb", "a\nb\nc");     // append c
        check(checkPlan(p, 2, 3), "append -> plan valid");
        check(runsEq(fillerRuns(p.left), {{1,1}}), "append -> left filler run {after line 1, count 1}");
        check(fillerRuns(p.right).empty(), "append -> right has no fillers");
        check(p.added == 1 && p.removed == 0 && p.changed == 0, "append -> added=1");
    }
    {
        ComparePlan p = compareTexts("a\nb\nc", "a\nc");     // delete b
        check(checkPlan(p, 3, 2), "delete -> plan valid");
        check(runsEq(fillerRuns(p.right), {{0,1}}), "delete -> right filler run {after line 0, count 1}");
        check(fillerRuns(p.left).empty(), "delete -> left has no fillers");
    }
    {
        ComparePlan p = compareTexts("one\ntwo\nthree", "one\nTWO\nthree");   // change middle
        check(checkPlan(p, 3, 3) && p.changed == 1, "change -> plan valid, changed=1");
        bool ch = p.left[1].marker == Marker::Changed && p.right[1].marker == Marker::Changed && !p.left[1].filler && !p.right[1].filler;
        check(ch, "change -> both sides Changed at the aligned row, no fillers");
    }
    {
        ComparePlan p = compareTexts("b", "a\nb");           // leading insert (annotation-above edge)
        check(checkPlan(p, 1, 2), "leading insert -> plan valid");
        check(p.left[0].filler && p.right[0].marker == Marker::Added, "leading insert -> row 0 is filler|Added");
        check(runsEq(fillerRuns(p.left), {{-1,1}}), "leading insert -> left filler run leads (afterLine -1)");
    }
    {
        // A replaced block with unequal counts: 2 lines -> 3 lines. Coalescing pairs 2 as Change, 1 Insert.
        ComparePlan p = compareTexts("x\nA\nB\ny", "x\nA1\nB1\nC1\ny");
        check(checkPlan(p, 4, 5) && p.changed == 2 && p.added == 1 && p.removed == 0,
              "unequal replace block -> changed=2, added=1, valid alignment");
    }
}

static void testComparePlanProperty()
{
    std::printf("[compare: plan invariants over random line-pairs]\n");
    std::mt19937 rng(0x5C09E5u);
    std::uniform_int_distribution<int> nD(0, 8);      // number of lines
    std::uniform_int_distribution<int> chD(0, 3);     // line "content" from a tiny alphabet -> frequent matches
    int bad = 0;
    for (int t = 0; t < 4000; ++t) {
        std::string A, B;
        int na = nD(rng), nb = nD(rng);
        for (int i = 0; i < na; ++i) { A += (char)('a' + chD(rng)); if (i + 1 < na) A += '\n'; }
        for (int i = 0; i < nb; ++i) { B += (char)('a' + chD(rng)); if (i + 1 < nb) B += '\n'; }
        LineDiff d = diffLines(A, B);
        ComparePlan p = buildComparePlan(d);
        if (!checkPlan(p, nlines(A), nlines(B))) { ++bad; continue; }
        // marker tallies must match the diff summary
        int rem = 0, add = 0, chgL = 0, chgR = 0, fillL = 0, fillR = 0;
        for (auto& v : p.left)  { if (v.marker == Marker::Removed) ++rem; if (v.marker == Marker::Changed) ++chgL; if (v.filler) ++fillL; }
        for (auto& v : p.right) { if (v.marker == Marker::Added) ++add; if (v.marker == Marker::Changed) ++chgR; if (v.filler) ++fillR; }
        if (rem != d.removed || add != d.added || chgL != d.changed || chgR != d.changed) ++bad;
        if (fillL != d.added || fillR != d.removed) ++bad;   // a left filler <=> an Added row; right filler <=> Removed
    }
    check(bad == 0, "4000 random pairs: plan aligned, real lines covered once, markers/fillers match the diff");
}

// ---- main -------------------------------------------------------------------------------------------

int main()
{
    std::printf("diff_myers_test\n");
    testSesEdges();
    testSesKnown();
    testSesGuard();
    testSesProperty();
    testSplitLines();
    testDiffLines();
    testDiffChars();
    testTokenizer();
    testGuardPropagation();
    testFuzzChars();
    testComparePlan();
    testComparePlanProperty();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
