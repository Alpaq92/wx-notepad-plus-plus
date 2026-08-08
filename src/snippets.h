// SPDX-License-Identifier: Apache-2.0
//
// wxNote - snippet bodies: parsing, expansion and tab stops.
// Copyright 2026 The wxNote Authors.
//
// A snippet body is plain text carrying tab stops:
//
//     for (int ${1:i} = 0; $1 < ${2:n}; ++$1)$0
//
// $1 / ${1:default} are stops the caret visits in ascending order; $0 is where the caret finally
// lands. An index used more than once is a MIRROR: every occurrence is edited at the same time, which
// is why the loop variable above only has to be typed once.
//
// Deliberately a standalone header over plain std::string - no wx, no Scintilla - so the grammar is
// unit-testable without an editor (tests/snippets_test.cpp links nothing). The editor side in
// main.cpp consumes SnippetParse and never re-derives the rules.
//
// NOT supported, and rejected rather than half-parsed: nested stops inside a placeholder
// (${1:${2:x}}) and shell/variable interpolation. Anything unrecognised parses as literal text rather
// than silently dropping characters.

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>   // std::move on parsed fields, and std::pair for the placeholder table
#include <vector>

// A ${1/find/replace/flags} transform: parsed here, applied by the editor - snippetApplyTransforms
// in main.cpp, which is where the reasoning about WHY lives.
struct SnippetTransform
{
    bool        active = false;
    std::string pattern, replacement;
    // The flag LETTERS are decoded here, not passed on as a string. Handing the editor a raw "flags"
    // to re-scan meant the alphabet was known in two files and enforced in one: adding a letter to the
    // whitelist below would have parsed cleanly and then been silently ignored downstream - exactly
    // the failure the whitelist exists to prevent.
    bool global = false;      // g
    bool ignoreCase = false;  // i
};

struct SnippetField
{
    int    stop   = 0;       // 0 = the final caret position
    size_t start  = 0;       // byte offset into SnippetParse::text
    size_t len    = 0;       // byte length of the placeholder (0 for a bare $1)
    bool   mirror = false;
    SnippetTransform xform;  // when active, this occurrence shows a TRANSFORM of the stop's text
};

struct SnippetParse
{
    std::string               text;     // the body with every $-construct replaced by its placeholder
    std::vector<SnippetField> fields;   // in document order
    bool                      hasZero = false;   // an explicit $0 was present

    // Distinct stop numbers, in visit order: ascending, with 0 last. Empty when the body has no stops.
    std::vector<int> visitOrder() const
    {
        std::vector<int> out;
        for (const SnippetField& f : fields)
        {
            if (f.mirror) continue;
            bool seen = false;
            for (int v : out) if (v == f.stop) { seen = true; break; }
            if (!seen) out.push_back(f.stop);
        }
        // Ascending, with 0 last: $0 is the exit stop, so it is visited after every numbered one.
        std::sort(out.begin(), out.end(),
                  [](int a, int b) { return a != 0 && (b == 0 || a < b); });
        return out;
    }
};

namespace snippet_detail {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Given the index of the '$' of a "${...}", returns the index just past its MATCHING '}', counting
// nesting. Returns npos when it is never closed.
//
// This exists so a construct we refuse to parse can be skipped WHOLE. Rewinding only the outer "${"
// and carrying on would leave the inner one to parse by itself: "${1:${2:x}}" became "${1:x}" - the
// literal text "${2:" silently deleted and a phantom stop 2 conjured out of the leftovers.
inline size_t spanEnd(const std::string& s, size_t dollar)
{
    size_t i = dollar + 2;    // past "${"
    int depth = 1;
    while (i < s.size())
    {
        if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') { ++depth; i += 2; continue; }
        if (s[i] == '}') { if (--depth == 0) return i + 1; ++i; continue; }
        ++i;
    }
    return std::string::npos;
}

// Reads "/pattern/replacement/flags}" starting at the '/', filling `t`.
//
// Contract, on EVERY path: `i` ends up just past the construct, or npos when it never closes. The
// bool says only whether the content was ACCEPTED. That split matters because a refused transform
// still has to be skipped whole, and the caller cannot find its end on its own: spanEnd() counts
// "${" and "}", so the '}' of a quantifier like (\w){2} would close the span early, the tail
// "/\U$1}" would then be parsed as ordinary body text, and its $1 would become a real tab stop -
// the exact "phantom stop conjured out of the leftovers" spanEnd exists to prevent, reachable from
// ordinary regex syntax. Only this function knows where the delimiters were, so only it can say.
//
// One reader called three times, so the sequence reads as the grammar: each part ends at the first
// UNESCAPED delimiter. "\/" is a literal slash and the backslash is dropped; every OTHER backslash
// pair is kept verbatim, because both halves are regex source and "\d" or "\U" must survive intact.
inline bool readTransform(const std::string& s, size_t& i, SnippetTransform& t)
{
    auto part = [&s](size_t& j, char end) {
        std::string out;
        for (; j < s.size() && s[j] != end; ++j)
        {
            if (s[j] == '\\' && j + 1 < s.size())
            { if (s[j + 1] != '/') out.push_back('\\'); out.push_back(s[++j]); }
            else out.push_back(s[j]);
        }
        return out;
    };

    // The construct's own closing '}' at or after `k`, or npos. Called once a delimiter run has
    // failed, starting from the last delimiter we did see.
    //
    // Brace-BALANCED, because everything it walks over is regex source: a bare '}' closing a "{n}"
    // quantifier is not the end of the construct. Counting it as one is the same phantom-stop trap
    // described above, just reached from a different typo - "${1/(\w){2}$2}" would end at the '}' of
    // {2} and hand "$2}" back to the body parser as a real stop.
    auto closeFrom = [&s](size_t k) -> size_t {
        int depth = 0;
        for (; k < s.size(); ++k)
        {
            if (s[k] == '\\' && k + 1 < s.size()) { ++k; continue; }
            if (s[k] == '{') { ++depth; continue; }
            if (s[k] == '}') { if (depth == 0) return k + 1; --depth; }
        }
        return std::string::npos;
    };

    const size_t d1 = i;                                                // the leading '/'
    size_t j = i + 1;
    const std::string pattern = part(j, '/');
    if (j >= s.size()) { i = closeFrom(d1); return false; }             // no second delimiter
    const size_t d2 = j;
    ++j;
    const std::string repl = part(j, '/');
    if (j >= s.size()) { i = closeFrom(d2); return false; }             // no third delimiter
    ++j;
    const std::string flags = part(j, '}');
    if (j >= s.size()) { i = std::string::npos; return false; }         // never closed at all

    // Only the flags this grammar can actually convey. Anything else belongs to a different engine,
    // so the whole construct stays literal rather than being applied with a flag quietly dropped.
    for (char f : flags)
        if (f != 'g' && f != 'i') { i = j + 1; return false; }

    t.active      = true;
    t.pattern     = pattern;
    t.replacement = repl;
    t.global      = flags.find('g') != std::string::npos;
    t.ignoreCase  = flags.find('i') != std::string::npos;
    i = j + 1;                                                          // past '}'
    return true;
}

// Reads the digits at `i`, returning false if there are none. Advances `i` past them.
inline bool readIndex(const std::string& s, size_t& i, int& out)
{
    if (i >= s.size() || !isDigit(s[i])) return false;
    long v = 0;
    size_t n = 0;
    while (i < s.size() && isDigit(s[i]) && n < 4)   // 4 digits is far past any real snippet
    {
        v = v * 10 + (s[i] - '0');
        ++i; ++n;
    }
    out = static_cast<int>(v);
    return true;
}

}   // namespace snippet_detail

// Expands a body into text plus the stops found in it. Never fails: anything that does not parse as a
// stop is emitted literally, so a malformed body produces the text the author typed rather than a
// silent deletion.
inline SnippetParse wxnParseSnippet(const std::string& body)
{
    using namespace snippet_detail;
    SnippetParse p;
    p.text.reserve(body.size());

    // PASS 1: which stops have a NON-transform occurrence. A transform is a derived view of another
    // field, so one whose stop is never defined has nothing to derive from - it would expand to
    // nothing, never become a stop (transforms are mirrors, and visitOrder drops mirrors) and never be
    // written, silently deleting itself. A one-digit typo in the index was enough. Those are refused
    // in pass 2 and stay literal, which is what the header promises and what the reader needs to see
    // the mistake. Cheap: a scan for "${digits" that is not followed by '/'.
    // Deliberately an OVER-approximation of the real grammar: it is looser about escapes and does not
    // check that a braced construct closes. That is safe in this direction - a false positive only
    // means a transform is accepted whose stop pass 2 then fails to define, which is the behaviour
    // that existed before this check. A false NEGATIVE would wrongly turn a valid transform literal,
    // so the bound must cover the whole body: `k + 2 < size` left the last two bytes unscanned and
    // missed a definition at the very tail, e.g. "${1/a/b/} $1".
    std::vector<int> defined;
    for (size_t k = 0; k < body.size(); ++k)
    {
        if (body[k] == '\\') { ++k; continue; }              // \$ is literal
        if (body[k] != '$') continue;
        size_t q = k + 1;
        const bool braced = q < body.size() && body[q] == '{';
        if (braced) ++q;
        int s = 0;
        if (!readIndex(body, q, s)) continue;
        // A braced construct that never closes defines nothing: pass 2 emits it literally and creates
        // no field, so counting it here would accept a transform whose source then does not exist -
        // and the transform, having been accepted, contributes no text either. Both halves vanish.
        // spanEnd is the same closure test pass 2 uses, so the two cannot drift on this point.
        if (braced && spanEnd(body, k) == std::string::npos) continue;
        // A bare $1, or ${1} / ${1:...} - anything except ${1/.../.../}
        if (!braced || (q < body.size() && body[q] != '/'))
            if (std::find(defined.begin(), defined.end(), s) == defined.end()) defined.push_back(s);
    }
    auto stopIsDefined = [&defined](int s) {
        return std::find(defined.begin(), defined.end(), s) != defined.end();
    };

    // Placeholder text captured for each stop the first time it is seen, so a later bare $1 mirrors
    // the same content rather than expanding empty.
    std::vector<std::pair<int, std::string>> known;
    auto placeholderFor = [&known](int stop) -> const std::string* {
        for (const auto& kv : known) if (kv.first == stop) return &kv.second;
        return nullptr;
    };

    for (size_t i = 0; i < body.size();)
    {
        const char c = body[i];

        if (c == '\\' && i + 1 < body.size() && (body[i + 1] == '$' || body[i + 1] == '\\'))
        {
            p.text.push_back(body[i + 1]);   // \$ and \\ are the only escapes; everything else is literal
            i += 2;
            continue;
        }
        if (c != '$') { p.text.push_back(c); ++i; continue; }

        // --- $ ---
        size_t j = i + 1;
        int stop = 0;
        std::string ph;
        SnippetTransform xform;
        bool parsed = false;
        size_t literalTo = std::string::npos;   // set when a whole "${...}" span must stay literal

        if (j < body.size() && body[j] == '{')
        {
            const size_t save = j;
            ++j;
            if (readIndex(body, j, stop))
            {
                const char nx = j < body.size() ? body[j] : '\0';
                if (nx == '}') { ++j; parsed = true; }
                else if (nx == '/')
                {
                    // ${N/find/replace/flags} - a transform of stop N, always a MIRROR. Refused, and
                    // kept literal, when the shape is wrong, a flag is unknown, or stop N is never
                    // defined anywhere in the body (pass 1) - a transform with no source can only
                    // vanish, and vanishing silently is what this grammar refuses to do.
                    //
                    // Either way `j` is now the construct's end, so every refusal skips it WHOLE;
                    // npos means it never closed, and the guard below leaves just the '$' literal.
                    const bool accepted = readTransform(body, j, xform);
                    if (accepted && stopIsDefined(stop)) parsed = true;
                    else                                 literalTo = j;
                }
                else if (nx == ':')
                {
                    ++j;
                    // Scan to the matching '}'. A nested '${' means a construct this grammar does not
                    // support, so the whole thing is abandoned and emitted literally rather than
                    // producing a placeholder with half of it missing.
                    std::string inner;
                    bool closed = false, nested = false;
                    while (j < body.size())
                    {
                        if (body[j] == '\\' && j + 1 < body.size() &&
                            (body[j + 1] == '}' || body[j + 1] == '$' || body[j + 1] == '\\'))
                        { inner.push_back(body[j + 1]); j += 2; continue; }
                        if (body[j] == '$' && j + 1 < body.size() && body[j + 1] == '{') { nested = true; break; }
                        if (body[j] == '}') { ++j; closed = true; break; }
                        inner.push_back(body[j]); ++j;
                    }
                    if (closed && !nested) { ph = inner; parsed = true; }
                    // A construct this grammar does not support. Emit the ENTIRE span literally -
                    // see spanEnd() for why stopping at the outer '$' is not good enough.
                    else if (nested) literalTo = spanEnd(body, i);
                }
            }
            if (!parsed) j = save;   // rewind; the '$' is literal text
        }
        else if (readIndex(body, j, stop))
        {
            parsed = true;
        }

        if (!parsed)
        {
            if (literalTo != std::string::npos && literalTo > i)
            { p.text.append(body, i, literalTo - i); i = literalTo; }
            else
            { p.text.push_back('$'); ++i; }
            continue;
        }

        // Two kinds of mirror, decided here. A transform is one unconditionally: it never DEFINES the
        // stop's text and is never a navigation target, so it stays out of the placeholder bookkeeping
        // entirely and starts empty (`ph` is provably still empty on that path - the '/' and ':'
        // branches are siblings). Otherwise a stop already seen mirrors the FIRST definition's text,
        // whether this occurrence is a bare $1 or carries a placeholder of its own.
        bool mirror = xform.active;
        if (!xform.active)
        {
            if (const std::string* prev = placeholderFor(stop)) { ph = *prev; mirror = true; }
            else                                                known.emplace_back(stop, ph);
        }

        SnippetField f;
        f.stop   = stop;
        f.start  = p.text.size();
        f.len    = ph.size();
        f.mirror = mirror;
        f.xform  = std::move(xform);   // loop-local and dead after this
        p.fields.push_back(std::move(f));
        // Only a REAL $0 counts. A ${0/../../} is a mirror, so it never enters visitOrder and cannot
        // serve as the exit stop - letting it set this suppressed the synthetic $0 that snippetInsert
        // would otherwise add, and the final Tab then fell off the end with the previous stop still
        // multi-selected, so the next keystroke wiped what the user had just typed.
        if (stop == 0 && !f.xform.active) p.hasZero = true;

        p.text += ph;
        i = j;
    }

    return p;
}

// ---- the snippet store -------------------------------------------------------------------------
//
// One entry per snippet, in a line-oriented format chosen so a body can contain literal newlines and
// tabs without escaping - the thing a "\n"-in-one-line format gets wrong, and the reason snippets are
// miserable to hand-edit in most editors:
//
//     [cpp:for]              <- language key from comment_tokens.h, then the trigger word
//     for (int ${1:i} = 0; $1 < ${2:n}; ++$1)
//     {
//         $0
//     }
//     [cpp:if]
//     ...
//
// A header line is "[lang:trigger]" with no leading space. Everything up to the next header is the
// body verbatim, minus the single trailing newline before the next header. Lines starting with '#'
// at column 0 are comments ONLY outside a body - inside one, '#' is just text, because '#' is a
// comment marker in half the languages a snippet might be written for.
struct SnippetDef
{
    std::string lang;      // "cpp", "python", ... or "*" for every language
    std::string trigger;   // the word typed before Tab
    std::string body;
};

inline std::vector<SnippetDef> wxnParseSnippetStore(const std::string& text)
{
    std::vector<SnippetDef> out;
    std::string line;
    bool inBody = false;

    auto flushTrailingNewline = [&out] {
        if (!out.empty() && !out.back().body.empty() && out.back().body.back() == '\n')
            out.back().body.pop_back();
    };

    for (size_t i = 0; i <= text.size(); ++i)
    {
        const bool eof = (i == text.size());
        // A store almost always ends WITH a newline, which has already terminated the last line.
        // Running the end-of-input pass anyway appended a second, empty body line, and the single
        // trailing newline the flush removes then only got the store back to where it should already
        // have been - so every final entry kept one.
        if (eof && line.empty()) break;
        const char c = eof ? '\n' : text[i];
        if (c == '\r') continue;                       // tolerate CRLF stores
        if (c != '\n') { line.push_back(c); continue; }

        // A header must be "[...:...]" occupying the whole line.
        const bool header = line.size() >= 4 && line.front() == '[' && line.back() == ']' &&
                            line.find(':') != std::string::npos;
        if (header)
        {
            flushTrailingNewline();
            const std::string inner = line.substr(1, line.size() - 2);
            const size_t colon = inner.find(':');
            SnippetDef d;
            d.lang    = inner.substr(0, colon);
            d.trigger = inner.substr(colon + 1);
            if (!d.trigger.empty()) out.push_back(d);
            inBody = !d.trigger.empty();
        }
        else if (inBody)
        {
            out.back().body += line;
            out.back().body += '\n';
        }
        // outside a body, a non-header line (blank or '#' comment) is ignored
        line.clear();
    }
    flushTrailingNewline();

    // Drop entries whose body is entirely empty - a header with nothing under it is a typo, and
    // expanding it would silently delete the trigger word the user typed.
    std::vector<SnippetDef> kept;
    for (SnippetDef& d : out) if (!d.body.empty()) kept.push_back(std::move(d));
    return kept;
}

// The snippets that ship with the editor, in the store format above so there is exactly one parser.
// Deliberately a short, boring set - the loops and guards people actually retype - rather than an
// attempt at a library. Users extend it through <user-data>/snippets.txt, which is read after these
// and so overrides any trigger it repeats.
inline const char* wxnBuiltinSnippets()
{
    return
        "[*:todo]\n"
        "TODO(${1:who}): ${2:what}$0\n"

        "[cpp:for]\n"
        "for (int ${1:i} = 0; $1 < ${2:n}; ++$1)\n"
        "{\n"
        "\t$0\n"
        "}\n"
        "[cpp:forr]\n"
        "for (const auto& ${1:it} : ${2:container})\n"
        "{\n"
        "\t$0\n"
        "}\n"
        "[cpp:if]\n"
        "if (${1:cond})\n"
        "{\n"
        "\t$0\n"
        "}\n"
        "[cpp:sw]\n"
        "switch (${1:value})\n"
        "{\n"
        "\tcase ${2:x}: $0 break;\n"
        "\tdefault: break;\n"
        "}\n"
        "[cpp:cls]\n"
        "class ${1:Name}\n"
        "{\n"
        "public:\n"
        "\t$1();\n"
        "\t$0\n"
        "};\n"

        "[python:def]\n"
        "def ${1:name}(${2:args}):\n"
        "\t$0\n"
        "[python:cls]\n"
        "class ${1:Name}:\n"
        "\tdef __init__(self${2:}):\n"
        "\t\t$0\n"
        "[python:for]\n"
        "for ${1:item} in ${2:iterable}:\n"
        "\t$0\n"
        "[python:main]\n"
        "if __name__ == \"__main__\":\n"
        "\t$0\n"

        "[js:fn]\n"
        "function ${1:name}(${2:args}) {\n"
        "\t$0\n"
        "}\n"
        "[js:for]\n"
        "for (const ${1:item} of ${2:items}) {\n"
        "\t$0\n"
        "}\n"
        "[js:log]\n"
        "console.log($0);\n"

        "[sh:for]\n"
        "for ${1:x} in ${2:list}; do\n"
        "\t$0\n"
        "done\n"
        "[sh:if]\n"
        "if [ ${1:cond} ]; then\n"
        "\t$0\n"
        "fi\n"

        "[html:a]\n"
        "<a href=\"${1:#}\">$0</a>\n"
        "[html:div]\n"
        "<div class=\"${1:name}\">\n"
        "\t$0\n"
        "</div>\n";
}

// The snippets applicable to `lang`, most specific first: exact-language entries before the "*" ones,
// and a later definition of the same trigger REPLACES an earlier one, so a user file can override a
// built-in simply by repeating its trigger.
inline std::vector<SnippetDef> wxnSnippetsFor(const std::vector<SnippetDef>& all, const std::string& lang)
{
    std::vector<SnippetDef> out;
    auto put = [&out](const SnippetDef& d) {
        for (SnippetDef& e : out) if (e.trigger == d.trigger) { e = d; return; }
        out.push_back(d);
    };
    for (const SnippetDef& d : all) if (d.lang == "*") put(d);
    for (const SnippetDef& d : all) if (!lang.empty() && d.lang == lang) put(d);
    return out;
}

// Re-indents a multi-line body to sit under `indent`, which is the leading whitespace of the line the
// snippet is being inserted on. The FIRST line is left alone - the caret is already there - and blank
// lines stay blank rather than collecting trailing whitespace. Field offsets are shifted to match.
inline void wxnReindentSnippet(SnippetParse& p, const std::string& indent, const std::string& eol)
{
    if (indent.empty() && eol == "\n") return;

    std::string out;
    out.reserve(p.text.size() + 16);
    // Shifts p.fields directly. This used to work on a copy that was assigned straight back, which
    // protected nothing - the copy was mutated in place all the same.
    auto shiftFrom = [&p](size_t at, size_t by) {
        for (SnippetField& f : p.fields) if (f.start >= at) f.start += by;
    };

    for (size_t i = 0; i < p.text.size(); ++i)
    {
        const char c = p.text[i];
        if (c != '\n') { out.push_back(c); continue; }

        out += eol;
        if (eol.size() > 1) shiftFrom(i + 1, eol.size() - 1);

        // Indent the next line unless it is empty (a blank line must not gain trailing spaces).
        const bool nextIsBlank = (i + 1 >= p.text.size()) || (p.text[i + 1] == '\n');
        if (!indent.empty() && !nextIsBlank)
        {
            out += indent;
            shiftFrom(i + 1, indent.size());
        }
    }
    p.text = out;
}
