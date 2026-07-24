// SPDX-License-Identifier: Apache-2.0
#pragma once
// =====================================================================
// diff_myers.h - a self-contained, dependency-free implementation of Eugene Myers' O(ND) difference
// algorithm ("An O(ND) Difference Algorithm and Its Variations", 1986), plus the line/character layers
// wxNote's File-Compare feature needs to drive Scintilla.
//
// WHY HAND-ROLLED (not a library): the core is ~1 screen of code, it is a well-understood published
// algorithm, and owning it keeps the permissive-future core free of any third-party licence / NOTICE /
// scanner-flag cost (the recurring tax this project already pays elsewhere). See the decision note in
// docs/MISSING_FUNCTIONALITY.md.
//
// THREE LAYERS, each independently testable:
//   1. shortestEditScript()  - the raw Myers SES over two integer token sequences. The primitive; everything
//                              else interns its input to ints and calls this. Returns Equal/Delete/Insert
//                              ops that, applied to A in order, reproduce B (the `verifyScript` invariant).
//   2. diffLines()           - splits two UTF-8 texts into lines (EOL-agnostic: CRLF/CR/LF compare equal),
//                              interns lines to ints, runs the SES, then coalesces adjacent Delete+Insert
//                              runs into Change rows. The output Row list is a 1:1 side-by-side alignment:
//                              each row carries the A line index, the B line index, or both - exactly what
//                              maps onto Scintilla line markers + SCI_ANNOTATIONSETTEXT blank filler.
//   3. diffChars()           - intra-line diff over UTF-8 CODE POINTS (never splits a multibyte sequence),
//                              returning BYTE spans of the deleted range in A and the inserted range in B,
//                              to drive INDIC_* highlighting of the changed span inside a Changed line.
//
// GUARD: Myers is O((N+M)*D) time / space where D is the edit distance, so two very dissimilar large
// inputs can cost O((N+M)^2). Options::maxCost caps D; on overflow the call degrades deterministically to
// "delete all of A, insert all of B" rather than hanging. 0 = unbounded (the default; callers diffing
// large files pass a bound).
//
// The engine is pure <string>/<vector> - NO wx, NO Scintilla - so it links into the headless test with no
// GUI and the UI layer converts wxString<->std::string (UTF-8) at the boundary.
// =====================================================================
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include <climits>

namespace wxn { namespace diff {

// ---- Layer 1: shortest edit script over integer tokens ----------------------------------------------

enum class Op { Equal, Delete, Insert };

// One edit. For Equal: a and b are the matched indices (a[a]==b[b]). For Delete: a is the removed A index,
// b is -1. For Insert: b is the added B index, a is -1.
struct Edit {
    Op  op;
    int a;   // index into A, or -1
    int b;   // index into B, or -1
};

struct Options {
    // Cap on the edit distance D. 0 = unbounded. When D would exceed this, the SES degrades to a full
    // delete+insert rather than continuing the O((N+M)*D) search. `hitGuard` (below) reports whether it fired.
    long long maxCost = 0;
};

struct Result {
    std::vector<Edit> edits;
    int  distance = 0;      // D: number of Delete+Insert ops (== Myers edit distance when the guard didn't fire)
    bool hitGuard = false;  // true if Options::maxCost forced the degraded path
};

// The core algorithm. Runs the greedy forward D-path search recording a per-D trace, then backtracks the
// trace to reconstruct the script. Deterministic. Equal inputs -> all-Equal, distance 0.
inline Result shortestEditScript(const std::vector<int>& a, const std::vector<int>& b, const Options& opt = {})
{
    Result r;

    // Defensive overflow guard: the trace and V array are int-indexed (MAX = N+M, size 2*MAX+1). Refuse
    // absurd multi-GB inputs (>~5e8 tokens) by degrading rather than overflowing int; realistic editor
    // inputs are orders of magnitude below this, so this never fires in practice.
    if (a.size() + b.size() > static_cast<size_t>(INT_MAX / 2 - 2)) {
        r.hitGuard = true;
        const int na = static_cast<int>(std::min<size_t>(a.size(), INT_MAX));
        const int nb = static_cast<int>(std::min<size_t>(b.size(), INT_MAX));
        for (int i = 0; i < na; ++i) r.edits.push_back({Op::Delete, i, -1});
        for (int j = 0; j < nb; ++j) r.edits.push_back({Op::Insert, -1, j});
        r.distance = (na > INT_MAX - nb) ? INT_MAX : na + nb;
        return r;
    }

    const int N = static_cast<int>(a.size());
    const int M = static_cast<int>(b.size());

    // Trivial edges (also keep the main loop's MAX >= 2 so the V offset is always valid).
    if (N == 0 && M == 0) return r;
    if (N == 0) { for (int j = 0; j < M; ++j) r.edits.push_back({Op::Insert, -1, j}); r.distance = M; return r; }
    if (M == 0) { for (int i = 0; i < N; ++i) r.edits.push_back({Op::Delete, i, -1}); r.distance = N; return r; }

    const int MAX = N + M;
    const int off = MAX;                       // maps diagonal k in [-MAX, MAX] onto [0, 2*MAX]
    std::vector<int> V(2 * MAX + 1, 0);
    std::vector<std::vector<int>> trace;       // trace[d] = V as it entered iteration d
    trace.reserve(static_cast<size_t>(MAX) + 1);

    int Dfinal = -1;
    for (int D = 0; D <= MAX; ++D) {
        if (opt.maxCost > 0 && D > opt.maxCost) {          // guard: give up the minimal search
            r.edits.clear();
            for (int i = 0; i < N; ++i) r.edits.push_back({Op::Delete, i, -1});
            for (int j = 0; j < M; ++j) r.edits.push_back({Op::Insert, -1, j});
            r.distance = N + M;
            r.hitGuard = true;
            return r;
        }
        trace.push_back(V);
        for (int k = -D; k <= D; k += 2) {
            int x;
            if (k == -D || (k != D && V[k - 1 + off] < V[k + 1 + off]))
                x = V[k + 1 + off];            // move down: insertion (consume b[y])
            else
                x = V[k - 1 + off] + 1;         // move right: deletion (consume a[x])
            int y = x - k;
            while (x < N && y < M && a[x] == b[y]) { ++x; ++y; }   // follow the diagonal snake
            V[k + off] = x;
            if (x >= N && y >= M) { Dfinal = D; break; }
        }
        if (Dfinal >= 0) break;
    }

    // Backtrack the trace from (N,M) to (0,0), emitting ops in reverse, then flip to forward order.
    int x = N, y = M;
    for (int D = Dfinal; D >= 0; --D) {
        const std::vector<int>& Vp = trace[D];
        const int k = x - y;
        int prevK;
        if (k == -D || (k != D && Vp[k - 1 + off] < Vp[k + 1 + off]))
            prevK = k + 1;                     // came from a down move (insertion)
        else
            prevK = k - 1;                     // came from a right move (deletion)
        const int prevX = Vp[prevK + off];
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {        // the diagonal snake: matched pairs
            r.edits.push_back({Op::Equal, x - 1, y - 1});
            --x; --y;
        }
        if (D > 0) {
            if (x == prevX) r.edits.push_back({Op::Insert, -1, prevY});   // down move
            else            r.edits.push_back({Op::Delete, prevX, -1});   // right move
        }
        x = prevX; y = prevY;
    }
    std::reverse(r.edits.begin(), r.edits.end());
    r.distance = Dfinal;
    return r;
}

// Test/UI invariant helper: apply `edits` to A and confirm they reproduce B (and that Equal ops match).
// Returns true iff the script is a valid transformation A -> B.
inline bool verifyScript(const std::vector<int>& a, const std::vector<int>& b, const std::vector<Edit>& edits)
{
    int ia = 0, ib = 0;
    for (const Edit& e : edits) {
        switch (e.op) {
            case Op::Equal:
                if (ia >= (int)a.size() || ib >= (int)b.size() || a[ia] != b[ib]) return false;
                if (e.a != ia || e.b != ib) return false;
                ++ia; ++ib; break;
            case Op::Delete:
                if (ia >= (int)a.size() || e.a != ia || e.b != -1) return false;
                ++ia; break;
            case Op::Insert:
                if (ib >= (int)b.size() || e.b != ib || e.a != -1) return false;
                ++ib; break;
        }
    }
    return ia == (int)a.size() && ib == (int)b.size();
}

// ---- Layer 2: line diff -----------------------------------------------------------------------------

// Split UTF-8 text into logical lines. EOL-agnostic: "\r\n", "\r" and "\n" all terminate a line and are
// stripped, so a file differing only in EOL style shows no changes. A terminator at end-of-text does NOT
// produce a trailing empty line ("a\n" -> ["a"]); a bare "\n" is one empty line ([""]); "" -> no lines.
inline std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    bool pending = false;                      // a line is "open" (content seen or a terminator consumed)
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') { out.push_back(cur); cur.clear(); pending = false; }
        else if (c == '\r') {
            out.push_back(cur); cur.clear(); pending = false;
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;   // consume the LF of a CRLF pair
        } else { cur.push_back(c); pending = true; }
    }
    if (pending) out.push_back(cur);           // trailing segment with no terminator
    return out;
}

struct Row {
    enum Kind { Equal, Delete, Insert, Change } kind;
    int aLine;   // 0-based line index in A, or -1
    int bLine;   // 0-based line index in B, or -1
};

struct LineDiff {
    std::vector<Row> rows;
    int added = 0, removed = 0, changed = 0;   // summary counts (Change counts once, not as add+remove)
    bool hitGuard = false;
};

// Intern lines to ints and diff. Adjacent Delete+Insert runs (a replaced block) are paired positionally
// into Change rows; the unpaired remainder stays as Delete or Insert rows.
inline LineDiff diffLines(const std::string& textA, const std::string& textB, const Options& opt = {})
{
    const std::vector<std::string> la = splitLines(textA);
    const std::vector<std::string> lb = splitLines(textB);

    std::unordered_map<std::string, int> intern;
    intern.reserve(la.size() + lb.size());
    auto idOf = [&intern](const std::string& s) {
        auto it = intern.find(s);
        if (it != intern.end()) return it->second;
        const int id = static_cast<int>(intern.size());
        intern.emplace(s, id);
        return id;
    };
    std::vector<int> ta; ta.reserve(la.size());
    std::vector<int> tb; tb.reserve(lb.size());
    for (const auto& s : la) ta.push_back(idOf(s));
    for (const auto& s : lb) tb.push_back(idOf(s));

    const Result ses = shortestEditScript(ta, tb, opt);

    LineDiff out;
    out.hitGuard = ses.hitGuard;
    out.rows.reserve(ses.edits.size());

    // Flush a pending block of deletes (A lines) and inserts (B lines): pair positionally into Change rows,
    // then emit the leftover as pure Delete or Insert rows.
    std::vector<int> pendDel, pendIns;
    auto flush = [&]() {
        // When the cost guard fired the SES is "delete all A, insert all B" over UNRELATED lines; pairing
        // them positionally into Change rows would misrepresent unrelated lines as replacements, so in that
        // case emit pure Delete/Insert rows only (the caller also sees LineDiff.hitGuard).
        const size_t pairs = out.hitGuard ? 0u : std::min(pendDel.size(), pendIns.size());
        for (size_t i = 0; i < pairs; ++i) {
            out.rows.push_back({Row::Change, pendDel[i], pendIns[i]});
            ++out.changed;
        }
        for (size_t i = pairs; i < pendDel.size(); ++i) { out.rows.push_back({Row::Delete, pendDel[i], -1}); ++out.removed; }
        for (size_t i = pairs; i < pendIns.size(); ++i) { out.rows.push_back({Row::Insert, -1, pendIns[i]}); ++out.added; }
        pendDel.clear(); pendIns.clear();
    };

    for (const Edit& e : ses.edits) {
        if (e.op == Op::Delete)      pendDel.push_back(e.a);
        else if (e.op == Op::Insert) pendIns.push_back(e.b);
        else { flush(); out.rows.push_back({Row::Equal, e.a, e.b}); }
    }
    flush();
    return out;
}

// ---- Layer 3: intra-line character diff -------------------------------------------------------------

struct Span { int start; int len; };   // byte offset + byte length within the source line

struct CharDiff {
    std::vector<Span> aDeleted;    // byte spans removed from line A
    std::vector<Span> bInserted;   // byte spans added in line B
    bool hitGuard = false;         // true if Options::maxCost forced the degraded (whole-line) result
};

namespace detail {
    struct Tok { uint32_t cp; int off; int len; };   // one UTF-8 code point: value + byte offset + byte length

    // Tokenize UTF-8 into code points. ONLY well-formed sequences decode to their code point; overlong
    // encodings, UTF-16 surrogates, and out-of-range (> U+10FFFF) sequences are REJECTED and each such lead
    // byte becomes a 1-byte sentinel token (value 0x110000+byte). Because every valid code point is <=
    // 0x10FFFF and every sentinel is >= 0x110000, the two ranges never overlap, so distinct byte sequences
    // never alias to the same token (the bug a naive decoder has: an unchecked 4-byte decode can land in the
    // sentinel band and be reported as "equal" to a stray byte). Every byte is consumed exactly once.
    inline std::vector<Tok> tokenizeUtf8(const std::string& s)
    {
        std::vector<Tok> t;
        const int n = static_cast<int>(s.size());
        auto cont = [&](int idx) { return idx < n && (static_cast<unsigned char>(s[idx]) & 0xC0) == 0x80; };
        int i = 0;
        while (i < n) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            int len = 1; uint32_t cp = 0; bool ok = false;
            if (c < 0x80) {
                len = 1; cp = c; ok = true;
            } else if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
                len = 2;
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3Fu);
                ok = cp >= 0x80u;                                          // reject overlong
            } else if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
                len = 3;
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i+1]) & 0x3Fu) << 6)
                                         |  (static_cast<unsigned char>(s[i+2]) & 0x3Fu);
                ok = cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu);    // reject overlong + surrogates
            } else if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
                len = 4;
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i+1]) & 0x3Fu) << 12)
                                         | ((static_cast<unsigned char>(s[i+2]) & 0x3Fu) << 6)
                                         |  (static_cast<unsigned char>(s[i+3]) & 0x3Fu);
                ok = cp >= 0x10000u && cp <= 0x10FFFFu;                    // reject overlong + out-of-range
            }
            if (!ok) { len = 1; cp = 0x110000u + c; }                     // any invalid lead/sequence -> sentinel
            t.push_back({cp, i, len});
            i += len;
        }
        return t;
    }

    // Append a token's byte range to `spans`, merging with the previous span if contiguous.
    inline void addSpan(std::vector<Span>& spans, int off, int len)
    {
        if (!spans.empty() && spans.back().start + spans.back().len == off) spans.back().len += len;
        else spans.push_back({off, len});
    }
}

// Character-level diff of two lines, returning the changed byte spans on each side (for INDIC_* painting).
inline CharDiff diffChars(const std::string& lineA, const std::string& lineB, const Options& opt = {})
{
    const std::vector<detail::Tok> ta = detail::tokenizeUtf8(lineA);
    const std::vector<detail::Tok> tb = detail::tokenizeUtf8(lineB);
    std::vector<int> ka; ka.reserve(ta.size());
    std::vector<int> kb; kb.reserve(tb.size());
    for (const auto& t : ta) ka.push_back(static_cast<int>(t.cp));
    for (const auto& t : tb) kb.push_back(static_cast<int>(t.cp));

    const Result ses = shortestEditScript(ka, kb, opt);

    CharDiff out;
    out.hitGuard = ses.hitGuard;
    for (const Edit& e : ses.edits) {
        if (e.op == Op::Delete)      detail::addSpan(out.aDeleted, ta[e.a].off, ta[e.a].len);
        else if (e.op == Op::Insert) detail::addSpan(out.bInserted, tb[e.b].off, tb[e.b].len);
    }
    return out;
}

// ---- Layer 4: side-by-side render plan --------------------------------------------------------------
// Turns a LineDiff into two equal-length, row-aligned columns for a side-by-side Compare view. Each side
// is a list of VisualRows: a real line (carrying an optional marker) or a blank "filler" that lines up
// with a real line on the other side (rendered in Scintilla as SCI_ANNOTATIONSETTEXT blank lines so the
// document is never mutated). Invariant: left.size() == right.size(), and at every visual row exactly one
// of {left,right} is a filler unless both are real (Equal/Change).

enum class Marker { None, Added, Removed, Changed };

struct VisualRow {
    int    line;    // real line index on this side, or -1 for a filler
    Marker marker;  // None for equal rows and fillers
    bool   filler;  // true => blank filler here (the other side has the real line)
};

struct ComparePlan {
    std::vector<VisualRow> left;    // side A ("before")
    std::vector<VisualRow> right;   // side B ("after")
    int added = 0, removed = 0, changed = 0;
    bool hitGuard = false;
};

inline ComparePlan buildComparePlan(const LineDiff& d)
{
    ComparePlan p;
    p.added = d.added; p.removed = d.removed; p.changed = d.changed; p.hitGuard = d.hitGuard;
    p.left.reserve(d.rows.size());
    p.right.reserve(d.rows.size());
    for (const Row& r : d.rows) {
        switch (r.kind) {
            case Row::Equal:
                p.left.push_back ({r.aLine, Marker::None,    false});
                p.right.push_back({r.bLine, Marker::None,    false});
                break;
            case Row::Change:
                p.left.push_back ({r.aLine, Marker::Changed, false});
                p.right.push_back({r.bLine, Marker::Changed, false});
                break;
            case Row::Delete:                                   // A-only line: B gets a filler
                p.left.push_back ({r.aLine, Marker::Removed, false});
                p.right.push_back({-1,      Marker::None,    true});
                break;
            case Row::Insert:                                   // B-only line: A gets a filler
                p.left.push_back ({-1,      Marker::None,    true});
                p.right.push_back({r.bLine, Marker::Added,   false});
                break;
        }
    }
    return p;
}

// A run of blank filler lines to place on one side, as a Scintilla annotation. `afterLine` is the real
// line index the annotation attaches below (-1 => the run leads the file, before any real line — a rare
// edge the caller renders best-effort since annotations can only sit *below* a line).
struct FillerRun { int afterLine; int count; };

inline std::vector<FillerRun> fillerRuns(const std::vector<VisualRow>& side)
{
    std::vector<FillerRun> runs;
    int lastReal = -1, pending = 0;
    for (const VisualRow& v : side) {
        if (v.filler) ++pending;
        else {
            if (pending) { runs.push_back({lastReal, pending}); pending = 0; }
            lastReal = v.line;
        }
    }
    if (pending) runs.push_back({lastReal, pending});
    return runs;
}

// Convenience: text A vs text B straight to a side-by-side plan.
inline ComparePlan compareTexts(const std::string& a, const std::string& b, const Options& opt = {})
{
    return buildComparePlan(diffLines(a, b, opt));
}

}} // namespace wxn::diff
