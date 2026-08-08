// SPDX-License-Identifier: Apache-2.0
//
// wxNote - self-test for the snippet grammar (src/snippets.h).
// Copyright 2026 The wxNote Authors.
//
// Pure STL, no wx, no editor - the grammar is the part that silently corrupts a body when it drifts
// (a dropped placeholder, a mirror that expands empty, an offset that no longer points at the field),
// and all of that is observable from the parse result alone.
//
//   cmake --build build --target snippets_test && build/bin/snippets_test

#include "snippets.h"

#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& what)
{
    if (ok) { ++g_pass; std::printf("  ok    %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL  %s\n", what.c_str()); }
}

static void eq(const std::string& got, const std::string& want, const std::string& what)
{
    // The got/want detail is appended ONLY on failure - unconditionally would print it on every
    // passing line too, burying the one line that matters in a wall of noise.
    check(got == want, what + (got == want ? "" : "  [got \"" + got + "\" want \"" + want + "\"]"));
}

// The text a field actually covers - this is what catches an offset that drifted off its placeholder.
static std::string at(const SnippetParse& p, size_t idx)
{
    if (idx >= p.fields.size()) return "<no such field>";
    const SnippetField& f = p.fields[idx];
    if (f.start > p.text.size() || f.start + f.len > p.text.size()) return "<out of range>";
    return p.text.substr(f.start, f.len);
}

static std::string order(const SnippetParse& p)
{
    std::string s;
    for (int v : p.visitOrder()) { if (!s.empty()) s += ","; s += std::to_string(v); }
    return s;
}

int main()
{
    std::printf("== snippets_test ==\n\n");

    // ---- (a) plain text is untouched -------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("hello world");
        eq(p.text, "hello world", "plain: text passes through");
        check(p.fields.empty(), "plain: no fields");
        check(!p.hasZero, "plain: no $0");
    }

    // ---- (b) bare stops --------------------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("a$1b$2c");
        eq(p.text, "abc", "bare: stops expand to nothing");
        check(p.fields.size() == 2, "bare: two fields");
        check(p.fields[0].stop == 1 && p.fields[0].start == 1 && p.fields[0].len == 0, "bare: $1 at offset 1");
        check(p.fields[1].stop == 2 && p.fields[1].start == 2, "bare: $2 at offset 2");
    }

    // ---- (c) placeholders ------------------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("for (int ${1:i} = 0; i < ${2:n}; ++i)");
        eq(p.text, "for (int i = 0; i < n; ++i)", "placeholder: default text is inserted");
        eq(at(p, 0), "i", "placeholder: field 0 covers exactly 'i'");
        eq(at(p, 1), "n", "placeholder: field 1 covers exactly 'n'");
    }

    // ---- (d) mirrors -----------------------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("${1:x} = $1 + $1;");
        eq(p.text, "x = x + x;", "mirror: a bare $1 repeats the placeholder text");
        check(p.fields.size() == 3, "mirror: three occurrences recorded");
        check(!p.fields[0].mirror, "mirror: the first occurrence is primary");
        check(p.fields[1].mirror && p.fields[2].mirror, "mirror: later occurrences are mirrors");
        eq(at(p, 1), "x", "mirror: the second field covers its own copy");
        eq(at(p, 2), "x", "mirror: the third field covers its own copy");
        eq(order(p), "1", "mirror: mirrors do not add extra stops to the visit order");
    }
    {
        // The placeholder can be given on a LATER occurrence than the first.
        const SnippetParse p = wxnParseSnippet("$1 and ${1:late}");
        eq(p.text, " and ", "mirror: first-wins - an empty first occurrence defines the stop as empty");
        check(p.fields.size() == 2 && p.fields[1].mirror, "mirror: the later one is still a mirror");
    }

    // ---- (e) visit order -------------------------------------------------------------------------
    eq(order(wxnParseSnippet("$3$1$2")),      "1,2,3",   "order: ascending regardless of position");
    eq(order(wxnParseSnippet("$0$2$1")),      "1,2,0",   "order: $0 is visited last");
    eq(order(wxnParseSnippet("$10$2")),       "2,10",    "order: multi-digit stops sort numerically, not as text");
    eq(order(wxnParseSnippet("no stops")),    "",        "order: empty when the body has none");

    // ---- (f) $0 ----------------------------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("if ($1) {\n\t$0\n}");
        check(p.hasZero, "zero: $0 detected");
        check(!wxnParseSnippet("if ($1)").hasZero, "zero: absent when not written");
    }

    // ---- (g) escapes -----------------------------------------------------------------------------
    eq(wxnParseSnippet("cost: \\$5").text,  "cost: $5",  "escape: \\$ is a literal dollar");
    check(wxnParseSnippet("cost: \\$5").fields.empty(), "escape: an escaped $ makes no field");
    eq(wxnParseSnippet("a\\\\b").text,      "a\\b",      "escape: \\\\ is a literal backslash");
    eq(wxnParseSnippet("100% \\d").text,    "100% \\d",  "escape: an unknown escape stays literal (regex bodies survive)");

    // ---- (h) malformed input degrades to literal text, never to deletion --------------------------
    eq(wxnParseSnippet("cost $ dollars").text, "cost $ dollars", "malformed: a lone $ is literal");
    eq(wxnParseSnippet("${nope}").text,        "${nope}",        "malformed: ${ with no digits is literal");
    eq(wxnParseSnippet("${1:unterminated").text, "${1:unterminated", "malformed: an unclosed ${ is literal");
    eq(wxnParseSnippet("${1").text,            "${1",            "malformed: ${digits with no close is literal");
    check(wxnParseSnippet("${nope}").fields.empty(), "malformed: literal text produces no fields");
    // Unsupported constructs must NOT be half-parsed - the whole thing stays literal.
    eq(wxnParseSnippet("${1:${2:x}}").text, "${1:${2:x}}", "malformed: a nested stop is rejected whole, not truncated");
    // A transform with an unknown flag is a construct meant for some other engine, so the whole span
    // stays literal rather than being applied with the flag quietly dropped.
    eq(wxnParseSnippet("${1/a/b/x}").text,  "${1/a/b/x}",  "transform: an unknown flag keeps the span literal");
    eq(wxnParseSnippet("${1/a/b}").text,    "${1/a/b}",    "transform: too few slashes keeps it literal");
    eq(wxnParseSnippet("${1/a/b/").text,    "${1/a/b/",    "transform: an unclosed transform keeps it literal");
    // A rejected transform must be skipped WHOLE. spanEnd counts only ${ and }, so the '}' of a
    // quantifier used to end the span early: the tail "/\U$1/gm}" was then parsed as body text and
    // its $1 became a real tab stop - a phantom stop conjured from the leftovers, on ordinary regex.
    {
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/(\\w){2}/\\U$1/gm}");
        eq(p.text, "v ${1/(\\w){2}/\\U$1/gm}", "transform: a rejected flag skips a span containing {n} whole");
        check(p.fields.size() == 1, "transform: ...and conjures no phantom stop from the tail");
        eq(order(p), "1", "transform: ...leaving only the real stop");
    }
    // Same trap, reached the other way: a MISSING trailing delimiter (a plausible typo) leaves only
    // two slashes, so there is no flag run to reject - the shape simply fails. readTransform still
    // has to report the end from the last delimiter it saw, because spanEnd would stop at the '}' of
    // the {2} quantifier and hand the tail "/\U$1}" back to the body parser.
    {
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/(\\w){2}/\\U$1}");
        eq(p.text, "v ${1/(\\w){2}/\\U$1}", "transform: a missing delimiter skips the span whole too");
        check(p.fields.size() == 1, "transform: ...and still conjures no phantom stop");
    }
    // A refused transform must not swallow what comes AFTER it either - the end is taken from the
    // last delimiter seen, not from the end of the body.
    {
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/a/b} $2");
        eq(p.text, "v ${1/a/b} ", "transform: too few slashes stays literal without eating the tail");
        eq(order(p), "1,2", "transform: ...so a real stop after it still parses");
    }
    // Pass 1 has to see a definition ANYWHERE, including the last two bytes of the body: an earlier
    // bound stopped short and refused this perfectly valid transform as undefined.
    {
        const SnippetParse p = wxnParseSnippet("${1/a/b/} $1");
        check(p.fields.size() == 2, "transform: a definition at the very tail still defines the stop");
        check(p.fields[0].xform.active, "transform: ...so the forward-referencing transform is accepted");
    }
    // The same trap once more, now with the SECOND delimiter missing, so there is no later delimiter
    // to measure from. The end scan is brace-balanced for exactly this: the '}' of {2} belongs to the
    // regex, and treating it as the close handed "$2}" back to the body parser as a real stop.
    {
        const SnippetParse p = wxnParseSnippet("${1:x} ${1/(\\w){2}$2}");
        eq(p.text, "x ${1/(\\w){2}$2}", "transform: a missing second delimiter stays literal past the {n}");
        check(p.fields.size() == 1, "transform: ...and conjures no stop 2 from the tail");
        eq(order(p), "1", "transform: ...leaving only the real stop");
    }
    // A definition that never closes is not a definition. Pass 2 emits "${1:unterminated" literally
    // and makes no field, so a transform accepted against it would have no source to derive from -
    // and having been accepted, it contributes no text either. Both halves would silently vanish.
    {
        const SnippetParse p = wxnParseSnippet("${1/a/b/} ${1:unterminated");
        eq(p.text, "${1/a/b/} ${1:unterminated",
           "transform: an unterminated definition does not license a transform");
        check(p.fields.empty(), "transform: ...and neither half produces a field");
    }
    // A transform whose stop is never defined has nothing to derive from: it would expand to nothing,
    // never become a stop, and never be written - silently deleting itself. A one-digit typo did it.
    eq(wxnParseSnippet("${1/a/b/}").text, "${1/a/b/}", "transform: an undefined stop keeps it literal");
    check(wxnParseSnippet("${1/a/b/}").fields.empty(), "transform: ...and produces no field");
    {
        const SnippetParse p = wxnParseSnippet("${1:x} ${2/a/b/}");
        eq(p.text, "x ${2/a/b/}", "transform: a typo'd stop number stays visible rather than vanishing");
    }
    // ${0/../../} is a mirror, so it can never BE the exit stop - it must not suppress the real one.
    {
        const SnippetParse p = wxnParseSnippet("${0:z} ${0/z/Z/}");
        check(p.hasZero, "transform: a real $0 alongside a transform still counts");
    }
    check(!wxnParseSnippet("${1:x} ${0/a/b/}").hasZero,
          "transform: a transform on stop 0 does NOT count as the exit stop");

    // ---- (i) an escaped brace inside a placeholder ------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("${1:a\\}b}");
        eq(p.text, "a}b", "escape: \\} inside a placeholder does not end it");
        eq(at(p, 0), "a}b", "escape: the field still covers the whole placeholder");
    }

    // ---- (j) empty placeholder -------------------------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("x${1:}y");
        eq(p.text, "xy", "empty placeholder: expands to nothing");
        check(p.fields.size() == 1 && p.fields[0].len == 0 && p.fields[0].start == 1,
              "empty placeholder: still a navigable field");
    }

    // ---- (k) every field offset lands inside the text ---------------------------------------------
    {
        const char* bodies[] = {
            "${1:a}${2:b}${3:c}", "$1$2$3$0", "pre ${1:x} mid $1 post $0",
            "${1:multi word}\n\t${2:second}\n$0", "\\$${1:real}",
        };
        bool allInside = true;
        for (const char* b : bodies)
        {
            const SnippetParse p = wxnParseSnippet(b);
            for (const SnippetField& f : p.fields)
                if (f.start + f.len > p.text.size()) allInside = false;
        }
        check(allInside, "offsets: every field lies within the expanded text");
    }

    // ---- (l) re-indentation ----------------------------------------------------------------------
    {
        SnippetParse p = wxnParseSnippet("if ($1) {\n${2:body}\n}");
        const std::string before = p.text.substr(p.fields[1].start, p.fields[1].len);
        wxnReindentSnippet(p, "    ", "\n");
        eq(p.text, "if () {\n    body\n    }", "reindent: continuation lines gain the caller's indent");
        eq(p.text.substr(p.fields[1].start, p.fields[1].len), before,
           "reindent: field offsets follow the text they mark");
    }
    {
        SnippetParse p = wxnParseSnippet("a\n\nb");
        wxnReindentSnippet(p, "  ", "\n");
        eq(p.text, "a\n\n  b", "reindent: a blank line does not collect trailing whitespace");
    }
    {
        SnippetParse p = wxnParseSnippet("x${1:v}\ny");
        wxnReindentSnippet(p, "", "\r\n");
        eq(p.text, "xv\r\ny", "reindent: CRLF is honoured");
        eq(p.text.substr(p.fields[0].start, p.fields[0].len), "v", "reindent: offsets survive a 2-byte EOL");
    }

    // ---- (m) UTF-8 placeholders are byte-accurate ------------------------------------------------
    {
        const SnippetParse p = wxnParseSnippet("${1:\xC5\xBA\xC3\xB3\xC5\x82w}");   // "źółw"
        eq(at(p, 0), "\xC5\xBA\xC3\xB3\xC5\x82w", "utf8: the field covers whole multi-byte characters");
    }

    // ---- (n2) transforms: ${1/find/replace/flags} -------------------------------------------------
    // Parsed here, applied by the editor (it needs a regex engine, which this header deliberately
    // has no access to). What the grammar owes is the three parts, intact.
    {
        const SnippetParse p = wxnParseSnippet("${1:name} -> ${1/a/b/g}");
        eq(p.text, "name -> ", "transform: expands empty, since its content is computed later");
        check(p.fields.size() == 2, "transform: two fields");
        check(!p.fields[0].xform.active, "transform: the plain stop carries no transform");
        check(p.fields[1].xform.active,  "transform: the second one does");
        check(p.fields[1].mirror, "transform: it is a mirror - never a navigation target");
        eq(p.fields[1].xform.pattern,     "a", "transform: pattern captured");
        eq(p.fields[1].xform.replacement, "b", "transform: replacement captured");
        check(p.fields[1].xform.global && !p.fields[1].xform.ignoreCase,
              "transform: the flag letters are decoded here, not passed on as a string");
        eq(order(p), "1", "transform: it adds no extra stop to the visit order");
    }
    {
        // Each body defines its stop: a transform whose stop is never defined is refused and stays
        // literal (see below), so a transform-only body would produce no fields at all.
        // Regex text must survive verbatim - both halves are handed to the engine as-is.
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/\\s+/_/g}");
        eq(p.fields[1].xform.pattern, "\\s+", "transform: a backslash escape is preserved in the pattern");
    }
    {
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/(\\w)/\\U$1/}");
        eq(p.fields[1].xform.replacement, "\\U$1", "transform: case operators survive into the replacement");
        check(!p.fields[1].xform.global && !p.fields[1].xform.ignoreCase,
              "transform: empty flags decode to both off");
    }
    {
        // An escaped slash is a literal one and must not end the part.
        const SnippetParse p = wxnParseSnippet("${1:v} ${1/a\\/b/c/}");
        eq(p.fields[1].xform.pattern, "a/b", "transform: \\/ is a literal slash inside the pattern");
    }
    {
        // A transform on a stop that also has a placeholder elsewhere still mirrors that stop.
        const SnippetParse p = wxnParseSnippet("${1:Hello} ${1/l/L/g} $1");
        check(p.fields.size() == 3, "transform: three occurrences of stop 1");
        check(!p.fields[0].mirror && p.fields[1].mirror && p.fields[2].mirror,
              "transform: only the first occurrence is primary");
        eq(p.text, "Hello  Hello", "transform: the plain mirror repeats the text, the transform starts empty");
    }

    // ---- (n) the store format --------------------------------------------------------------------
    {
        const std::string store =
            "# a comment outside a body\n"
            "\n"
            "[cpp:for]\n"
            "for (int ${1:i} = 0; $1 < ${2:n}; ++$1)\n"
            "{\n"
            "\t$0\n"
            "}\n"
            "[python:def]\n"
            "def ${1:name}(${2:args}):\n"
            "    # not a store comment - this is body text\n"
            "    $0\n"
            "[*:todo]\n"
            "TODO(${1:who}): $0\n";
        const std::vector<SnippetDef> all = wxnParseSnippetStore(store);
        check(all.size() == 3, "store: three entries parsed");
        eq(all[0].lang + "/" + all[0].trigger, "cpp/for", "store: language and trigger split");
        check(all[0].body.find('\n') != std::string::npos, "store: a body keeps its real newlines");
        check(all[0].body.find('\t') != std::string::npos, "store: a body keeps its real tabs");
        eq(all[0].body.substr(all[0].body.size() - 1), "}", "store: the trailing newline before the next header is dropped");
        check(all[1].body.find("# not a store comment") != std::string::npos,
              "store: '#' INSIDE a body is text, not a comment");
        eq(all[2].lang, "*", "store: '*' is a valid language");

        // resolution
        const std::vector<SnippetDef> forCpp = wxnSnippetsFor(all, "cpp");
        check(forCpp.size() == 2, "resolve: cpp gets its own plus the wildcard");
        const std::vector<SnippetDef> forPy = wxnSnippetsFor(all, "python");
        check(forPy.size() == 2, "resolve: python likewise");
        const std::vector<SnippetDef> forNone = wxnSnippetsFor(all, "");
        check(forNone.size() == 1 && forNone[0].trigger == "todo",
              "resolve: an unknown language still gets the wildcard entries");
    }
    {
        // A later definition of the same trigger wins - this is how a user file overrides a built-in.
        const std::vector<SnippetDef> all = wxnParseSnippetStore(
            "[cpp:for]\nBUILTIN\n[cpp:for]\nUSER\n");
        const std::vector<SnippetDef> r = wxnSnippetsFor(all, "cpp");
        check(r.size() == 1, "override: the same trigger does not appear twice");
        eq(r[0].body, "USER", "override: the later definition wins");
    }
    {
        // A language-specific entry must beat a wildcard with the same trigger.
        const std::vector<SnippetDef> all = wxnParseSnippetStore("[*:x]\nWILD\n[cpp:x]\nCPP\n");
        eq(wxnSnippetsFor(all, "cpp")[0].body, "CPP", "override: language beats wildcard");
        eq(wxnSnippetsFor(all, "python")[0].body, "WILD", "override: wildcard still applies elsewhere");
    }
    {
        // Malformed stores must not produce half-entries.
        check(wxnParseSnippetStore("[cpp:empty]\n").empty(), "store: a header with no body is dropped");
        check(wxnParseSnippetStore("no headers at all\n").empty(), "store: body text with no header is ignored");
        check(wxnParseSnippetStore("[nocolon]\nx\n").empty(), "store: a header without ':' is not a header");
        check(wxnParseSnippetStore("[cpp:]\nx\n").empty(), "store: an empty trigger is rejected");
        check(wxnParseSnippetStore("").empty(), "store: empty input");
    }
    {
        // CRLF stores are common on Windows and must parse identically.
        const std::vector<SnippetDef> lf   = wxnParseSnippetStore("[cpp:a]\nline1\nline2\n");
        const std::vector<SnippetDef> crlf = wxnParseSnippetStore("[cpp:a]\r\nline1\r\nline2\r\n");
        check(lf.size() == 1 && crlf.size() == 1 && lf[0].body == crlf[0].body,
              "store: a CRLF store parses identically to an LF one");
    }

    // ---- (o) the shipped built-ins actually parse -------------------------------------------------
    // It is one long concatenated string literal; a missing "\n" silently glues two entries together
    // and the store then contains a snippet nobody wrote.
    {
        const std::vector<SnippetDef> built = wxnParseSnippetStore(wxnBuiltinSnippets());
        check(built.size() >= 15, "builtins: the shipped set parses to a plausible number of entries");

        bool badLang = false, badTrigger = false, badBody = false, unparsable = false, strayHeader = false;
        for (const SnippetDef& d : built)
        {
            if (d.lang.empty() || d.lang.find(' ') != std::string::npos) badLang = true;
            if (d.trigger.empty() || d.trigger.find(' ') != std::string::npos) badTrigger = true;
            if (d.body.empty()) badBody = true;
            // A '[' at the start of a body line means a header did not terminate the previous body.
            if (d.body.size() > 1 && d.body[0] == '[') strayHeader = true;
            if (d.body.find("\n[") != std::string::npos) strayHeader = true;
            const SnippetParse p = wxnParseSnippet(d.body);
            for (const SnippetField& f : p.fields)
                if (f.start + f.len > p.text.size()) unparsable = true;
        }
        check(!badLang,     "builtins: every entry has a single-token language");
        check(!badTrigger,  "builtins: every entry has a single-token trigger");
        check(!badBody,     "builtins: no entry has an empty body");
        check(!strayHeader, "builtins: no body swallowed a following header (a missing newline)");
        check(!unparsable,  "builtins: every body parses with in-range field offsets");

        // Spot-check that resolution reaches a real language and the wildcard.
        const std::vector<SnippetDef> cpp = wxnSnippetsFor(built, "cpp");
        bool sawFor = false, sawTodo = false;
        for (const SnippetDef& d : cpp) { if (d.trigger == "for") sawFor = true; if (d.trigger == "todo") sawTodo = true; }
        check(sawFor,  "builtins: cpp resolves its own 'for'");
        check(sawTodo, "builtins: cpp also gets the wildcard 'todo'");

        // The cpp 'cls' body mirrors $1 into the constructor - the mirror machinery must see two.
        for (const SnippetDef& d : cpp)
            if (d.trigger == "cls")
            {
                const SnippetParse p = wxnParseSnippet(d.body);
                int ones = 0;
                for (const SnippetField& f : p.fields) if (f.stop == 1) ++ones;
                check(ones == 2, "builtins: 'cls' declares the class name once and mirrors it");
            }
    }

    std::printf("\n%s  (%d passed, %d failed)\n", g_fail ? "FAILED" : "PASSED", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
