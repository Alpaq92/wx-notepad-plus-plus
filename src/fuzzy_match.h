// SPDX-License-Identifier: Apache-2.0
//
// wxNote - acronym-aware fuzzy matcher for the command palette and quick-open.
// Copyright 2026 The wxNote Authors.
//
// Pure <string>/<vector>/<algorithm>: no wx, no Scintilla, so it is unit-testable on its own (see
// tests/fuzzy_test.cpp) exactly like diff_myers.h and hash_algos.h. The UI converts wxString <-> UTF-8
// at the boundary.
//
// Matching runs over DECODED CODE POINTS, not bytes - the same choice diffChars() already made. Over
// bytes, a needle byte could match half of a multi-byte character, and the returned highlight
// positions could split one, so "z" would appear to match "ż" and the bolding would corrupt the glyph.
//
// The ranking is fzf-shaped: every matched character scores, and characters at a WORD BOUNDARY or a
// camelCase hump score much more. Acronym-awareness ("otv" -> "OpenTheVault") is a consequence of that
// bonus table, not a special case - which is why it also handles snake_case, kebab-case and digits
// without extra rules.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzy {

// ---- scoring constants ---------------------------------------------------------------------------
// Tuned so an acronym match outranks a scattered one of the same length. Worked example, needle "otv":
// against "OpenTheVault" every character lands on a word start (48 + 24 + 12 + 12 = 96 before gaps);
// against "outvoted" only the first does (48 + 24 + one consecutive pair), so the acronym wins by a
// clear margin. That margin is what makes typing initials feel like it knows the name.
//
// kCamel == kBoundary deliberately: a camelCase hump and a character after _ - . / are both the start
// of a word, and ranking one above the other only decides arbitrary ties between naming conventions.
// kConsecutive is the largest single bonus because an unbroken run is the strongest evidence the user
// typed a real substring - without that, "func" scored the same against "funclist" as against
// "f_u_n_c", since three separator boundaries happened to equal three consecutive characters.
inline constexpr int kMatch        = 16;   // per matched code point
inline constexpr int kBoundary     = 12;   // start, or after / \ _ - . or space
inline constexpr int kCamel        = 12;   // lower -> Upper transition - as strong as a boundary
inline constexpr int kDigitEdge    =  6;   // letter -> digit transition
inline constexpr int kConsecutive  = 14;   // per character continuing an unbroken run
inline constexpr int kFirstCharMul =  2;   // the needle's first character's positional bonus, doubled
inline constexpr int kExactCase    =  1;   // matched without case folding
inline constexpr int kInTail       =  4;   // inside the haystack's significant span - see Item::tailAt
inline constexpr int kGapStart     = -3;   // opening a gap between matched characters
inline constexpr int kGapExtend    = -1;   // each additional skipped character

// Guard rails for the quadratic pass. Beyond these the greedy result is used as-is: a 40-character
// needle over a 4000-character line must not turn one keystroke into 160k cells.
inline constexpr size_t kMaxNeedleForDp = 32;
inline constexpr size_t kMaxWindowForDp = 512;

enum Bonus : uint8_t { BonusNone = 0, BonusDigitEdge, BonusCamel, BonusBoundary };

struct Item
{
    std::vector<char32_t> cp;     // the haystack, decoded
    std::vector<char32_t> fold;   // ASCII-lowercased copy (>= 0x80 passes through unchanged)
    std::vector<uint8_t>  bonus;  // per-position Bonus class, precomputed once at build time

    // [tailAt, tailEnd) - the SIGNIFICANT SPAN of the haystack: the part a hit should be rewarded for
    // (kInTail) and measured from (the leading-gap penalty). It is a RANGE, not just a start, because
    // the significant part is not always at the end:
    //
    //   * a raw path ("src/main.cpp")     -> [after the last separator, end)  = the filename
    //   * a FilterRow ("main.cpp src/")   -> [0, length of the primary)       = the filename, FIRST
    //
    // That second shape is why a bare start index is not enough. prepareFilterRow concatenates the
    // filename BEFORE the directory (so the drawing code can slice the primary off the front), which
    // puts the last '/' inside the deepest DIRECTORY name - so a "everything after the last separator"
    // rule would have handed the filename bonus to directories and inverted the whole ranking.
    int                   tailAt  = 0;
    int                   tailEnd = 0;   // set by build() to cp.size(); narrow it via setSignificant()
};

// Number of code points a UTF-8 string decodes to, without materialising the vector.
inline size_t countCps(std::string_view utf8)
{
    size_t n = 0;
    for (unsigned char c : utf8) if ((c & 0xC0) != 0x80) ++n;   // count lead bytes only
    return n;
}

// Restrict the significant span to [from, to). Used for haystacks whose meaningful part is not the
// trailing path component - see Item's comment.
inline void setSignificant(Item& it, size_t from, size_t to)
{
    it.tailAt  = static_cast<int>(std::min(from, it.cp.size()));
    it.tailEnd = static_cast<int>(std::min(to,   it.cp.size()));
    if (it.tailEnd < it.tailAt) it.tailEnd = it.tailAt;
}

struct Result
{
    bool             ok = false;
    int              score = 0;
    std::vector<int> pos;         // matched CODE POINT indices, ascending - safe to bold directly
};

// ---- UTF-8 -----------------------------------------------------------------------------------------
// Deliberately lenient: a malformed byte becomes U+FFFD rather than throwing or truncating. Haystacks
// are filenames and translated labels, and one bad byte must not make a file unfindable.
inline std::vector<char32_t> decodeUtf8(std::string_view s)
{
    std::vector<char32_t> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int len = 1; char32_t cp = c;
        if      ((c & 0x80u) == 0x00u) { len = 1; cp = c; }
        else if ((c & 0xE0u) == 0xC0u) { len = 2; cp = c & 0x1Fu; }
        else if ((c & 0xF0u) == 0xE0u) { len = 3; cp = c & 0x0Fu; }
        else if ((c & 0xF8u) == 0xF0u) { len = 4; cp = c & 0x07u; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + static_cast<size_t>(len) > s.size()) { out.push_back(0xFFFD); break; }
        bool bad = false;
        for (int k = 1; k < len; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0u) != 0x80u) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (bad) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += static_cast<size_t>(len);
    }
    return out;
}

// ASCII-only folding, on purpose. Correct Unicode case folding needs a table this header has no
// business carrying; non-ASCII characters compare exactly, which is right for the languages the UI
// ships in - a Polish user typing "ż" means "ż".
inline char32_t foldAscii(char32_t c) { return (c >= U'A' && c <= U'Z') ? c + 32 : c; }

inline bool isAsciiUpper(char32_t c) { return c >= U'A' && c <= U'Z'; }
inline bool isAsciiLower(char32_t c) { return c >= U'a' && c <= U'z'; }
inline bool isAsciiDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
inline bool isSepChar(char32_t c)
{
    return c == U'/' || c == U'\\' || c == U'_' || c == U'-' || c == U'.' || c == U' ';
}

// ---- candidate preparation ------------------------------------------------------------------------
// Called ONCE per candidate when the list is harvested, never per keystroke - the whole point of the
// split. With ~600 menu commands or 50k files, doing this work in the filter loop is the difference
// between instant and visibly laggy.
inline Item build(std::string_view utf8, bool isPath)
{
    Item it;
    it.cp = decodeUtf8(utf8);
    it.fold.resize(it.cp.size());
    it.bonus.resize(it.cp.size());
    for (size_t i = 0; i < it.cp.size(); ++i)
    {
        it.fold[i] = foldAscii(it.cp[i]);
        const char32_t cur = it.cp[i];
        uint8_t b = BonusNone;
        if (i == 0)                                                b = BonusBoundary;
        else
        {
            const char32_t prev = it.cp[i - 1];
            if      (isSepChar(prev))                              b = BonusBoundary;
            else if (isAsciiLower(prev) && isAsciiUpper(cur))      b = BonusCamel;
            else if (!isAsciiDigit(prev) && isAsciiDigit(cur))     b = BonusDigitEdge;
        }
        it.bonus[i] = b;
    }
    it.tailAt  = 0;
    it.tailEnd = static_cast<int>(it.cp.size());
    if (isPath)
        for (size_t i = 0; i < it.cp.size(); ++i)
            if (it.cp[i] == U'/' || it.cp[i] == U'\\')
                it.tailAt = static_cast<int>(i) + 1;
    return it;
}

// "Smart case": a needle containing an uppercase letter is treated as case-sensitive, so "Main"
// stops matching "mainframe" while "main" still matches both. Users reach for this without being
// told it exists, which is why it is on by default rather than a setting.
inline bool smartCaseWanted(const std::vector<char32_t>& needleRaw)
{
    for (char32_t c : needleRaw) if (isAsciiUpper(c)) return true;
    return false;
}

// ---- phase 1: feasibility ---------------------------------------------------------------------------
// Greedy forward subsequence. Rejects the overwhelming majority of candidates at one comparison per
// code point, and hands the survivors a tight [lo,hi) window so the quadratic pass never sees the
// whole haystack.
// Takes the haystack explicitly so the two feasibility passes are ONE function: the case-insensitive
// one scans (it.fold, needleFold), the smart-case one scans (it.cp, needleRaw). They were duplicated,
// which meant any fix to the window arithmetic had to be made twice.
inline bool firstPass(const std::vector<char32_t>& hay, const std::vector<char32_t>& needle,
                      int& lo, int& hi)
{
    if (needle.empty()) { lo = 0; hi = 0; return true; }
    size_t n = 0;
    int first = -1, last = -1;
    for (size_t i = 0; i < hay.size() && n < needle.size(); ++i)
        if (hay[i] == needle[n]) { if (first < 0) first = static_cast<int>(i); last = static_cast<int>(i); ++n; }
    if (n < needle.size()) return false;
    lo = first;
    hi = last + 1;
    return true;
}

namespace detail {

inline int positional(const Item& it, size_t j, bool firstNeedleChar)
{
    int s = 0;
    switch (it.bonus[j])
    {
        case BonusBoundary:  s = kBoundary;  break;
        case BonusCamel:     s = kCamel;     break;
        case BonusDigitEdge: s = kDigitEdge; break;
        default: break;
    }
    if (firstNeedleChar) s *= kFirstCharMul;
    // Half-open: a hit must be INSIDE the significant span, not merely at or after its start. Testing
    // only >= tailAt is what let directory characters collect the filename bonus.
    if (static_cast<int>(j) >= it.tailAt && static_cast<int>(j) < it.tailEnd) s += kInTail;
    return s;
}

// Greedy left-to-right positions, then a right-to-left tightening pass. Used when the input is too
// large for the DP: matching backwards from the end pulls the characters together, which recovers
// most of what the DP would have found for a fraction of the cost.
inline void greedyPositions(const std::vector<char32_t>& needleFold, const Item& it,
                            int lo, int hi, std::vector<int>& out)
{
    out.clear();
    size_t n = 0;
    for (int i = lo; i < hi && n < needleFold.size(); ++i)
        if (it.fold[static_cast<size_t>(i)] == needleFold[n]) { out.push_back(i); ++n; }
    if (out.size() != needleFold.size()) { out.clear(); return; }
    for (size_t k = out.size(); k-- > 0;)
    {
        const int upper = (k + 1 < out.size()) ? out[k + 1] - 1 : hi - 1;
        for (int i = upper; i > out[k]; --i)
            if (it.fold[static_cast<size_t>(i)] == needleFold[k]) { out[k] = i; break; }
    }
}

// No smartCase parameter: every term below is computed the same way either way. The case-sensitivity
// decision has already been made upstream, in which characters were allowed to match at all.
inline int scoreOf(const std::vector<int>& pos, const std::vector<char32_t>& needleRaw, const Item& it)
{
    int total = 0, run = 0;
    for (size_t k = 0; k < pos.size(); ++k)
    {
        const size_t j = static_cast<size_t>(pos[k]);
        total += kMatch;
        total += positional(it, j, k == 0);
        if (k > 0 && pos[k] == pos[k - 1] + 1) { ++run; total += kConsecutive * run; }
        else                                   { run = 0; }
        if (k < needleRaw.size() && it.cp[j] == needleRaw[k]) total += kExactCase;
        if (k > 0)
        {
            const int gap = pos[k] - pos[k - 1] - 1;
            if (gap > 0) total += kGapStart + kGapExtend * (gap - 1);
        }
        else if (pos[0] > it.tailAt)
        {
            const int lead = pos[0] - it.tailAt;
            total += kGapStart + kGapExtend * (lead > 0 ? lead - 1 : 0);
        }
    }
    return total;
}

}  // namespace detail

// ---- phase 2: scoring -------------------------------------------------------------------------------
// Maximises the score over every way the needle can be embedded in the window, rather than taking the
// first embedding greedy order happens to find. Two rows of DP: best[j] is the best score for the
// needle prefix ending with its last character matched at j.
inline Result score(const std::vector<char32_t>& needleFold,
                    const std::vector<char32_t>& needleRaw,
                    bool smartCase, const Item& it)
{
    Result r;
    if (needleFold.empty()) { r.ok = true; r.score = 0; return r; }

    // Smart case narrows the haystack view: compare raw against raw where the needle is uppercase.
    const std::vector<char32_t>& hay = it.fold;
    int lo = 0, hi = 0;
    // Smart case compares raw against raw; otherwise folded against folded. Same scan either way.
    if (!firstPass(smartCase ? it.cp : it.fold, smartCase ? needleRaw : needleFold, lo, hi))
        return r;

    const size_t m = needleFold.size();
    const size_t w = static_cast<size_t>(hi - lo);

    auto matches = [&](size_t k, size_t j) {
        return smartCase ? (it.cp[j] == needleRaw[k]) : (hay[j] == needleFold[k]);
    };

    if (m > kMaxNeedleForDp || w > kMaxWindowForDp)
    {
        // Too big to optimise; the greedy+tighten answer is good enough and bounded.
        std::vector<int> pos;
        if (smartCase)
        {
            pos.clear(); size_t n = 0;
            for (int i = lo; i < hi && n < needleRaw.size(); ++i)
                if (it.cp[static_cast<size_t>(i)] == needleRaw[n]) { pos.push_back(i); ++n; }
            if (pos.size() != needleRaw.size()) return r;
        }
        else
            detail::greedyPositions(needleFold, it, lo, hi, pos);
        if (pos.empty()) return r;
        r.ok = true; r.pos = std::move(pos);
        r.score = detail::scoreOf(r.pos, needleRaw, it);
        return r;
    }

    // best[j]  - score of the best match of needle[0..k] whose last character sits at window slot j
    // from[j]  - the previous slot, for reconstruction
    constexpr int kNeg = -1000000;
    std::vector<int> prev(w, kNeg), cur(w, kNeg);
    // FLAT, indexed from[k * w + j]. As a vector-of-vectors this was up to kMaxNeedleForDp + 1 = 33
    // separate heap allocations for every candidate that reaches the DP - i.e. per matching row, per
    // keystroke. One allocation, and the backtrack walk below stays in contiguous memory.
    std::vector<int> from(m * w, -1);

    for (size_t k = 0; k < m; ++k)
    {
        std::fill(cur.begin(), cur.end(), kNeg);
        int bestPrev = kNeg, bestPrevAt = -1;
        for (size_t j = 0; j < w; ++j)
        {
            const size_t hj = static_cast<size_t>(lo) + j;
            // NOTE the shape of this loop: the running-maximum update at the bottom must run for
            // EVERY slot, so nothing here may `continue`. It did once, on the "needle[k] matches
            // before needle[k-1] has any predecessor" path - which is exactly the slot a REPEATED
            // adjacent character lands on. bestPrev was then never seeded and the whole match failed:
            // "aa" did not match "aa", and "ll" did not match "hello".
            if (matches(k, hj))
            {
                int base = kNeg;
                int src = -1;
                if (k == 0)
                {
                    base = 0;
                    // leading gap from the filename start, so "main" prefers src/main.cpp over
                    // src/domain.cpp even though both contain the needle
                    const int lead = static_cast<int>(hj) - it.tailAt;
                    if (lead > 0) base += kGapStart + kGapExtend * (lead - 1);
                }
                else if (bestPrevAt >= 0)
                {
                    // consecutive continuation, or the best earlier match plus a gap penalty
                    const int contigAt = static_cast<int>(j) - 1;
                    const int contig = (contigAt >= 0 && prev[static_cast<size_t>(contigAt)] > kNeg)
                                     ? prev[static_cast<size_t>(contigAt)] + kConsecutive : kNeg;
                    const int gap = bestPrev + kGapStart
                                  + kGapExtend * std::max(0, static_cast<int>(j) - bestPrevAt - 2);
                    if (contig >= gap) { base = contig; src = contigAt; }
                    else               { base = gap;    src = bestPrevAt; }
                }
                if (base > kNeg)   // unreachable slot stays kNeg, but the update below still runs
                {
                    int s = base + kMatch + detail::positional(it, hj, k == 0);
                    if (it.cp[hj] == needleRaw[k]) s += kExactCase;
                    cur[j] = s;
                    from[k * w + j] = src;
                }
            }
            if (prev[j] > bestPrev) { bestPrev = prev[j]; bestPrevAt = static_cast<int>(j); }
        }
        prev.swap(cur);
    }

    int best = kNeg, bestAt = -1;
    for (size_t j = 0; j < w; ++j) if (prev[j] > best) { best = prev[j]; bestAt = static_cast<int>(j); }
    if (bestAt < 0) return r;

    r.ok = true;
    r.score = best;
    r.pos.resize(m);
    int j = bestAt;
    for (size_t k = m; k-- > 0;) { r.pos[k] = lo + j; j = from[k * w + static_cast<size_t>(j)]; if (j < 0 && k > 0) break; }
    return r;
}

// ---- convenience ------------------------------------------------------------------------------------
// One-shot match for callers that do not keep prepared Items (tests, and any list small enough that
// preparation is not worth caching).
inline Result matchOne(std::string_view needleUtf8, std::string_view hayUtf8, bool isPath = false)
{
    const std::vector<char32_t> raw = decodeUtf8(needleUtf8);
    std::vector<char32_t> fold(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) fold[i] = foldAscii(raw[i]);
    const Item it = build(hayUtf8, isPath);
    return score(fold, raw, smartCaseWanted(raw), it);
}

}  // namespace fuzzy
