// SPDX-License-Identifier: Apache-2.0
//
// wxNote - the regular-expression backend for Find / Replace / Mark / Count / Find in Files.
// Copyright 2026 The wxNote Authors.
//
// WHY THIS EXISTS. Searching used to go through Scintilla's SCFIND_CXX11REGEX, i.e. std::regex. Three
// things were wrong with that, and all three were invisible to the user:
//
//   1. NO PATTERN COULD CROSS A LINE BREAK. wx's vendored Scintilla gates its whole-buffer regex path
//      behind `#ifdef REGEX_MULTILINE` (Document.cxx:3063) and never defines it, so the live code is
//      the `#else`: a loop over Range(LineStart(line), LineEnd(line)). LineEnd EXCLUDES the EOL bytes,
//      so the matcher never saw a '\n' at all. "delete everything between two markers" was not
//      degraded, it was unreachable.
//   2. NO LOOKBEHIND, no named groups, no atomic groups - std::regex is ECMAScript, which has none.
//   3. NO \U / \L / \E in replacements: Scintilla's SubstituteByPosition knows \a \b \f \n \r \t \v
//      and backslash, and its `default:` branch pushes the backslash back verbatim, so "\U$1" landed
//      in the document as the literal text "\U" followed by the group.
//
// A failed match and an INVALID PATTERN were also indistinguishable - an unsupported construct threw,
// Scintilla turned that into SC_STATUS_WARN_REGEX, and nothing ever called SCI_GETSTATUS.
//
// This header is deliberately free of wx and Scintilla so the whole engine is unit-testable against
// plain strings (tests/regex_test.cpp). The editor side matches over SCI_GETCHARACTERPOINTER and
// drives SCI_SETTARGETRANGE / SCI_REPLACETARGET from the byte offsets returned here.

#pragma once

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cstring>
#include <string>
#include <utility>   // std::pair - Match::g holds the ovector's own (start, end) shape
#include <vector>

namespace wxnre {

inline constexpr size_t kNoPos = static_cast<size_t>(-1);

struct Options
{
    bool matchCase = false;
    bool wholeWord = false;
    bool dotAll    = false;   // let '.' cross line breaks too; off by default, as everywhere else
};

struct Match
{
    bool   ok    = false;
    size_t start = 0;
    size_t end   = 0;                 // half-open [start, end)
    // Group n is [g[n].first, g[n].second); both kNoPos when it did not participate. One vector of
    // pairs rather than two parallel ones: the same-size invariant was maintained by hand at four
    // places, and this is the shape PCRE2's ovector already has.
    std::vector<std::pair<size_t, size_t>> g;

    size_t groups() const { return g.empty() ? 0 : g.size() - 1; }   // excluding group 0
    bool   empty() const { return end == start; }
};

class Regex
{
public:
    Regex() = default;
    ~Regex() { reset(); }
    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;
    Regex(Regex&& o) noexcept : m_re(o.m_re), m_md(o.m_md) { o.m_re = nullptr; o.m_md = nullptr; }

    void reset()
    {
        if (m_md) { pcre2_match_data_free(m_md); m_md = nullptr; }
        if (m_re) { pcre2_code_free(m_re); m_re = nullptr; }
    }

    bool ok() const { return m_re != nullptr; }

    // Compiles `pattern`. On failure returns false and fills `err` with PCRE2's own message plus the
    // offset - the user gets "missing closing parenthesis at 7", not a silent no-match.
    bool compile(const std::string& pattern, const Options& o, std::string* err = nullptr)
    {
        reset();

        // Whole-word wraps rather than adding a flag, because PCRE2 has no "whole word" option. The
        // group is non-capturing so it does not renumber the user's own groups, and \b outside it
        // means an alternation like "a|bc" is bounded as a whole instead of only its last branch.
        const std::string src = o.wholeWord ? ("\\b(?:" + pattern + ")\\b") : pattern;

        uint32_t opts = PCRE2_MULTILINE;          // ^ and $ match at line breaks, as every editor does
        if (!o.matchCase) opts |= PCRE2_CASELESS;
        if (o.dotAll)     opts |= PCRE2_DOTALL;
        // UTF mode with UCP so \w, \b and the classes follow Unicode. MATCH_INVALID_UTF (PCRE2 >= 10.34)
        // makes an ill-formed byte a non-match instead of a hard error: a document being edited is not
        // guaranteed to be valid UTF-8 at every instant, and search must not fail outright on one.
        opts |= PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;

        int    ec = 0;
        PCRE2_SIZE eo = 0;
        m_re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(src.c_str()), src.size(), opts, &ec, &eo, nullptr);
        if (!m_re)
        {
            if (err)
            {
                PCRE2_UCHAR buf[256];
                pcre2_get_error_message(ec, buf, sizeof(buf));
                *err = reinterpret_cast<const char*>(buf);
                // Report the offset in the USER's pattern, not in our wrapped copy. Subtracting the
                // "\b(?:" prefix is not enough on its own: PCRE2 reports some errors (a missing
                // closing parenthesis, for one) at the END of the pattern, and the wrapper adds ")\b"
                // there too - so the shifted offset would point past what the user actually typed.
                // Clamping to the pattern length makes those land on its last character.
                const size_t shift = o.wholeWord ? 5 : 0;
                size_t off = (static_cast<size_t>(eo) > shift) ? static_cast<size_t>(eo) - shift : 0;
                if (off > pattern.size()) off = pattern.size();
                *err += " at offset " + std::to_string(off);
            }
            return false;
        }
        // The match-data block's size depends only on the compiled pattern, so that is its natural
        // lifetime. Creating and freeing one per call put a malloc/free pair on every match attempt -
        // including failed ones - inside Replace All and the incremental bar's per-keystroke loop.
        m_md = pcre2_match_data_create_from_pattern(m_re, nullptr);
        if (!m_md) { reset(); return false; }
        return true;
    }

    // The primitives take a raw buffer, not a std::string, so the editor can match straight over
    // SCI_GETCHARACTERPOINTER. Copying the document into a std::string per call would be pure waste -
    // and inside a Replace All loop it would be a copy per match.

    // First match at or after `from`, entirely within [from, to). Returns false when there is none.
    bool search(const char* hay, size_t size, size_t from, size_t to, Match& out) const
    {
        return searchImpl(hay, size, from, to, out);
    }
    bool search(const std::string& hay, size_t from, size_t to, Match& out) const
    {
        return searchImpl(hay.data(), hay.size(), from, to, out);
    }

    // LAST match within [from, to) - what a backwards Find needs.
    //
    // PCRE2 has no reverse scan, so this has to walk forward. Doing that from `from` would mean one
    // match attempt for EVERY match in the whole preceding document: a backward Find from the end of
    // a file with 20 000 hits ran 20 000 matches, and repeatedly pressing Shift+F3 back through a file
    // was quadratic in total matches.
    //
    // Instead, scan a window just below `to` and only widen it when empty. The last match inside a
    // window is necessarily the last overall - anything earlier starts earlier and cannot win - so
    // the usual case costs one window. Correctness is unaffected by the narrower start: searchImpl
    // always passes the WHOLE buffer as the subject and only moves the start offset, so lookbehind
    // and \b still see the text before the window.
    bool searchBackward(const char* hay, size_t size, size_t from, size_t to, Match& out) const
    {
        for (size_t window = 64 * 1024; ; window *= 4)
        {
            const size_t lo = (to - from > window) ? to - window : from;
            Match cur, best;
            size_t at = lo;
            while (at <= to && searchImpl(hay, size, at, to, cur))
            {
                best = cur;
                at = cur.empty() ? nextChar(hay, size, cur.start) : cur.end;   // never pin on zero-width
            }
            if (best.ok) { out = best; return true; }
            if (lo == from) return false;      // the window already covered everything
        }
    }
    bool searchBackward(const std::string& hay, size_t from, size_t to, Match& out) const
    {
        return searchBackward(hay.data(), hay.size(), from, to, out);
    }

    // Advances past a match when iterating (Replace All, Mark, Count). A zero-width match must move on
    // by one CHARACTER, never one byte - stepping into the middle of a UTF-8 sequence would make the
    // next match start on a continuation byte.
    // Where an iteration must resume after a match (Replace All, Mark, Count). A zero-width match has
    // to move on by one CHARACTER, never one byte: stepping into the middle of a UTF-8 sequence would
    // start the next match on a continuation byte.
    //
    // Takes POSITIONS rather than a Match, because positions are what every caller actually holds -
    // the editor reads them back from Scintilla's target and never has a wxnre::Match in hand. The
    // earlier Match-shaped signature made three call sites fabricate an empty one to satisfy it.
    static size_t advanceOver(const char* hay, size_t size, size_t start, size_t end)
    {
        return (end > start) ? end : nextChar(hay, size, end);
    }

    // Next character boundary at or after `p` - skips UTF-8 continuation bytes (10xxxxxx).
    static size_t nextChar(const char* s, size_t size, size_t p)
    {
        if (p >= size) return p + 1;
        size_t q = p + 1;
        while (q < size && (static_cast<unsigned char>(s[q]) & 0xC0) == 0x80) ++q;
        return q;
    }

    // Expands a replacement template against a match.
    //
    // Accepts BOTH spellings a Notepad++ user might type: $1 / ${1} and \1. Case folding is
    // \U (upper) and \L (lower) until \E, plus \u / \l for a single following character - the
    // Boost.Regex vocabulary real Notepad++ ships, and precisely what Scintilla's substituter dropped.
    static std::string expand(const std::string& tmpl, const std::string& hay, const Match& m)
    {
        return expand(tmpl, hay.data(), hay.size(), m);
    }
    static std::string expand(const std::string& tmpl, const char* hay, size_t haySize, const Match& m)
    {
        std::string out;
        out.reserve(tmpl.size() + 16);
        int  mode = 0;        // 0 none, 1 upper until \E, 2 lower until \E
        int  once = 0;        // 1 upper next char, 2 lower next char
        auto emit = [&out, &mode, &once](const std::string& s) {
            for (char c : s)
            {
                unsigned char u = static_cast<unsigned char>(c);
                int m2 = once ? once : mode;
                // Case folding is ASCII-only on purpose: doing it for all of Unicode needs a case
                // table this header has no business carrying, and getting it half-right (folding the
                // first byte of a multi-byte sequence) would corrupt the text.
                if (u < 0x80)
                {
                    if (m2 == 1 && u >= 'a' && u <= 'z') c = static_cast<char>(u - 32);
                    else if (m2 == 2 && u >= 'A' && u <= 'Z') c = static_cast<char>(u + 32);
                    if (once) once = 0;
                }
                out.push_back(c);
            }
        };
        auto group = [&](size_t n) -> std::string {
            if (n >= m.g.size()) return std::string();
            const size_t s = m.g[n].first, e = m.g[n].second;
            if (s == kNoPos || e == kNoPos || e > haySize || s > e) return std::string();
            return std::string(hay + s, e - s);
        };

        for (size_t i = 0; i < tmpl.size();)
        {
            const char c = tmpl[i];

            if (c == '$' && i + 1 < tmpl.size())
            {
                size_t j = i + 1;
                if (tmpl[j] == '{')
                {
                    size_t k = j + 1, v = 0; bool any = false;
                    while (k < tmpl.size() && tmpl[k] >= '0' && tmpl[k] <= '9') { v = v * 10 + (tmpl[k] - '0'); ++k; any = true; }
                    if (any && k < tmpl.size() && tmpl[k] == '}') { emit(group(v)); i = k + 1; continue; }
                }
                else if (tmpl[j] >= '0' && tmpl[j] <= '9')
                {
                    size_t v = 0, n = 0;
                    while (j < tmpl.size() && tmpl[j] >= '0' && tmpl[j] <= '9' && n < 2) { v = v * 10 + (tmpl[j] - '0'); ++j; ++n; }
                    emit(group(v)); i = j; continue;
                }
                out.push_back('$'); ++i; continue;
            }

            if (c == '\\' && i + 1 < tmpl.size())
            {
                const char e = tmpl[i + 1];
                if (e >= '0' && e <= '9') { emit(group(static_cast<size_t>(e - '0'))); i += 2; continue; }
                switch (e)
                {
                    case 'U': mode = 1; i += 2; continue;
                    case 'L': mode = 2; i += 2; continue;
                    case 'E': mode = 0; i += 2; continue;
                    case 'u': once = 1; i += 2; continue;
                    case 'l': once = 2; i += 2; continue;
                    case 'n': out.push_back('\n'); i += 2; continue;
                    case 'r': out.push_back('\r'); i += 2; continue;
                    case 't': out.push_back('\t'); i += 2; continue;
                    case '\\': out.push_back('\\'); i += 2; continue;
                    default: break;
                }
                // An unknown escape keeps both characters, so a replacement containing a literal
                // backslash sequence survives instead of losing the backslash.
                out.push_back('\\'); out.push_back(e); i += 2; continue;
            }

            emit(std::string(1, c));
            ++i;
        }
        return out;
    }

private:
    bool searchImpl(const char* hay, size_t size, size_t from, size_t to, Match& out) const
    {
        // Clear in place rather than `out = Match{}`: searchBackward reuses one Match across every
        // iteration, and assigning a fresh one throws away the group vector's capacity each time.
        out.ok = false; out.start = out.end = 0;
        out.g.clear();
        if (!m_re || !m_md || !hay || from > size) return false;
        if (to > size) to = size;
        if (from > to) return false;

        pcre2_match_data* md = m_md;

        // The subject is the WHOLE buffer up to `to`, started at `from`, rather than a substring:
        // that way lookbehind can still see the text before `from`, and ^ / \b know whether `from`
        // really is a line or word boundary. Passing a substring would silently change what matches.
        const int rc = pcre2_match(m_re, reinterpret_cast<PCRE2_SPTR>(hay), to,
                                   from, 0, md, nullptr);
        bool got = false;
        if (rc > 0)
        {
            const PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
            const uint32_t    n  = static_cast<uint32_t>(rc);
            out.ok    = true;
            out.start = ov[0];
            out.end   = ov[1];
            out.g.resize(n);
            for (uint32_t k = 0; k < n; ++k)
                out.g[k] = { (ov[2 * k]     == PCRE2_UNSET) ? kNoPos : ov[2 * k],
                             (ov[2 * k + 1] == PCRE2_UNSET) ? kNoPos : ov[2 * k + 1] };
            got = true;
        }
        return got;
    }

    pcre2_code*       m_re = nullptr;
    pcre2_match_data* m_md = nullptr;   // owned alongside m_re; see compile()
};

}   // namespace wxnre
