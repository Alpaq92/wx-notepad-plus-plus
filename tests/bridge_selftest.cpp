// SPDX-License-Identifier: Apache-2.0
//
// bridge_selftest - end-to-end behavioural self-test of the npp-bridge Phase-1 surface.
// Copyright 2026 The wxNote Authors.
//
// Boots the REAL application object (WxnApp + WxnShellFrame from src/main.cpp - compiled INTO this
// translation unit with wxIMPLEMENT_APP neutralized, so the genuine host pipeline runs: nib
// capability surface, plugin loader, save pipeline, wx command dispatcher), which loads the real
// npp_bridge plugin from <exe>/nib, which in turn loads the PROBE N++ plugin
// (packages/npp-bridge/example/example_plugin.cpp, built to <exe>/plugins/WxnProbe on Windows). The
// probe logs every notification (code + idFrom) plus its toolbar/allocator/dark-mode registration
// results to a JSON-lines file; this harness then drives open / edit / save / undo-to-savepoint /
// save-all / close through DIRECT host calls (the same file-scope nib seams the bridge itself uses -
// keymap_selftest style, never wxUIActionSimulator/OS input injection) and asserts the Phase-1
// acceptance points against the probe's log:
//
//   (a) NPPN_FILEBEFORESAVE precedes NPPN_FILESAVED, both carrying the SAME buffer id - including
//       for a BACKGROUND buffer written by Save All (the wrong-id regression the v2 events fix);
//   (b) NO false NPPN_FILESAVED after undo-to-savepoint (the savepoint-derived false positive);
//   (c) NPPM_ALLOCATE* grants land inside the host pools, disjoint from every host-reserved
//       marker/indicator/command number (asserted against the REAL constants in main.cpp);
//   (d) invoking an allocated command id round-trips host -> wx dispatcher -> bridge -> the probe's
//       messageProc (ids > 32767, so this also exercises the 16-bit WM_COMMAND wrap-safe path);
//   (e) NPPM_ISDARKMODEENABLED == the host's real dark state.
//
// Everything user-visible is sandboxed: a custom wxAppTraits redirects GetUserDataDir() into a
// scratch dir and a wxFileConfig replaces the registry config BEFORE WxnApp::OnInit runs, so the
// test can never read or clobber the real installation's session/recovery/preferences (nor hand off
// to a running wxnote via the reuse-instance IPC, which is config-gated off in the sandbox).
//
//   cmake --build build --target bridge_selftest && build/bin/bridge_selftest

#include <wx/app.h>
#include <wx/init.h>
#include <wx/apptrait.h>
#include <wx/stdpaths.h>
#include <wx/fileconf.h>
#include <wx/modalhook.h>   // headlessly auto-answer the confirmClose save prompt (Phase-4 shutdown-veto test)

// Pull the whole application into this TU with its app-entry macro neutralized: every wx header
// main.cpp includes is guard-deduplicated against the includes above, so this redefinition is the
// one that reaches line "wxIMPLEMENT_APP(WxnApp);". The selftest then subclasses WxnApp and supplies
// its own main() -> wxEntry() below - the documented embedding path.
#undef wxIMPLEMENT_APP
#define wxIMPLEMENT_APP(appname) /* neutralized: bridge_selftest provides its own app + main() */
#include "main.cpp"

// The N++ ABI numbers the log assertions speak (NPPN_*/SCN codes). Apache-2.0 clean-room header;
// main.cpp already included Scintilla.h from the same include set, so this adds only the NPPM/NPPN
// vocabulary.
#include "Notepad_plus_msgs.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/dir.h>     // the backup tests enumerate RecoveryBackups/*.bak
#include <wx/utils.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <climits>
#include <map>          // the probe-button image check's colour histogram
#include <algorithm>    // std::max (same)

static int g_pass = 0;
static int g_failCount = 0;
static void check(bool ok, const char* what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
    std::fflush(stdout);   // flush per line so a hang's last-reached check is visible even when stdout is redirected (block-buffered)
    if (ok) ++g_pass; else ++g_failCount;
}

// ---- the search + snippet seams -------------------------------------------------------------------
// These run against a REAL frame with a real editor, because everything they cover lives between the
// pure engines (already covered by regex_test / snippets_test) and Scintilla: target-range handling,
// backward search, replacement expansion, and the snippet session's absolute offsets as text is typed
// into it. That layer is where both of the last two defects were.
//
// Defined here and declared in main.cpp as a friend of the frame, so the private seams stay private.
template <class FB>
void wxnDriveEditorSelfTests(WxnShellFrameT<FB>* f)
{
    auto text  = [f]() { return f->getDocUtf8(); };
    auto load  = [f](const char* s) { f->setDocUtf8(s); f->sci(SCI_SETSEL, 0, 0); };
    auto opts  = [](const char* find, bool regex, bool fwd = true) {
        FindOpts o; o.find = wxString::FromUTF8(find); o.regex = regex; o.matchCase = true;
        o.forward = fwd; o.wrap = false; return o;
    };

    // ---- (1) a regex that CROSSES A LINE BREAK, through the real Find path ------------------------
    // The headline capability. Before PCRE2 this could not match at all, at any surface.
    {
        load("alpha\nbeta\ngamma");
        FindOpts o = opts("alpha\\nbeta", true);
        check(f->doFindNext(o), "wiring: a pattern containing \\n matches through doFindNext");
        check(f->sci(SCI_GETSELECTIONSTART) == 0 && f->sci(SCI_GETSELECTIONEND) == 10,
              "wiring: ...and selects exactly the two lines it spanned");
    }

    // ---- (2) Replace All across lines, with a group reference -------------------------------------
    {
        load("a\n  b\n  c");
        FindOpts o = opts("\\n\\s+", true);
        o.repl = " ";
        const int n = f->doReplaceAll(o);
        check(n == 2, "wiring: Replace All applies a multi-line pattern to every match");
        check(text() == "a b c", "wiring: ...producing the joined text (the join-lines recipe)");
    }
    {
        load("me@here");
        FindOpts o = opts("(\\w+)@(\\w+)", true);
        o.repl = "$2 at $1";
        f->doReplaceAll(o);
        check(text() == "here at me", "wiring: $1/$2 expand through the real replace path");
    }

    // ---- (3) \U reaches the document as CASE, not as literal text ---------------------------------
    // Scintilla's own substituter has no case operators and emitted "\U" verbatim; this is the proof
    // that replacement expansion is ours now.
    {
        load("hello world");
        FindOpts o = opts("(\\w+)", true);
        o.repl = "\\U$1";
        f->doReplaceAll(o);
        check(text() == "HELLO WORLD", "wiring: \\U upper-cases through Replace All");
        check(text().find("\\U") == std::string::npos, "wiring: ...and no literal backslash-U is left behind");
    }

    // ---- (4) backward search finds the LAST match before the caret --------------------------------
    {
        load("x_x_x");
        f->sci(SCI_SETSEL, 5, 5);
        FindOpts o = opts("x", true, /*forward=*/false);
        check(f->doFindNext(o), "wiring: a backward regex search finds a match");
        check(f->sci(SCI_GETSELECTIONSTART) == 4, "wiring: ...the LAST one before the caret, not the first");
    }

    // ---- (5) zero-width patterns terminate, and every surface agrees on the count -----------------
    // doCount, doMarkAll and the incremental bar each iterate matches separately. They disagreed:
    // \b was counted by one and skipped by the others, so one document gave three different answers.
    {
        load("ab cd");
        FindOpts o = opts("\\b", true);
        const int counted = f->doCount(o);          // terminating at all is half the assertion
        check(counted == 4, "wiring: Count reports every zero-width match (4 word boundaries)");
        check(f->doMarkAll(o) == counted, "wiring: Mark All agrees with Count on a zero-width pattern");
    }
    {
        load("abc");
        FindOpts o = opts("x*", true);
        o.repl = "-";
        const int n = f->doReplaceAll(o);           // a hang here is the failure
        check(n > 0 && text() == "-a-b-c-", "wiring: Replace All on an all-optional pattern terminates correctly");
    }

    // ---- (6) a zero-width step must not split a multi-byte character ------------------------------
    {
        load("a\xC5\xBA" "b");                       // "aźb"
        FindOpts o = opts("(?=\\w)", true);
        o.repl = "|";
        f->doReplaceAll(o);
        check(text() == "|a|\xC5\xBA" "|b", "wiring: zero-width iteration lands on character boundaries");
    }

    // ---- (7) an invalid pattern is reported, not silently treated as no-match ---------------------
    {
        load("anything");
        check(!f->doFindNext(opts("(unclosed", true)), "wiring: an invalid pattern reports no match");
        // ...and the SAME pattern as literal text still searches, i.e. the failure did not poison state.
        load("a (unclosed b");
        check(f->doFindNext(opts("(unclosed", false)), "wiring: the same text still matches literally");
    }

    // ---- (8) literal mode is untouched by any of this ---------------------------------------------
    {
        load("a.b axb");
        FindOpts o = opts("a.b", false);
        check(f->doFindNext(o) && f->sci(SCI_GETSELECTIONSTART) == 0,
              "wiring: literal mode still treats '.' as a full stop, not a wildcard");
        check(f->doCount(o) == 1, "wiring: ...and counts only the literal occurrence");
    }

    // ---- (9) the SNIPPET session: insert, fields, mirrors, navigation -----------------------------
    {
        load("");
        check(f->snippetInsert("for (int ${1:i} = 0; $1 < ${2:n}; ++$1)"),
              "snippet: inserting a body with stops starts a session");
        check(text() == "for (int i = 0; i < n; ++i)", "snippet: the body expands with its placeholders");
        check(f->m_snip.active, "snippet: the session is live");
        // MIRRORS. What this code is responsible for is selecting EVERY occurrence of the current stop
        // and turning additional-selection typing on; Scintilla then applies one keystroke to all of
        // them. So that is what is asserted - the exact ranges, and the flag. Simulating the typing
        // itself is not possible from here: SCI_REPLACESEL edits only the MAIN selection, so a test
        // written that way measures the wrong message rather than the feature.
        check(f->sci(SCI_GETSELECTIONS) == 3, "snippet: every mirror of the first stop is selected at once");
        check(f->sci(SCI_GETADDITIONALSELECTIONTYPING) == 1,
              "snippet: additional-selection typing is on, so one keystroke reaches every mirror");
        {
            // "for (int i = 0; i < n; ++i)" - the three 'i' of stop 1 sit at 9, 16 and 25.
            bool ranges = true;
            const int want[3][2] = { { 9, 10 }, { 16, 17 }, { 25, 26 } };
            for (int k = 0; k < 3; ++k)
            {
                const int a = (int)f->sci(SCI_GETSELECTIONNSTART, k);
                const int b = (int)f->sci(SCI_GETSELECTIONNEND, k);
                if (a != want[k][0] || b != want[k][1]) ranges = false;
            }
            check(ranges, "snippet: ...and each selection covers exactly its own occurrence");
        }

        // Offset tracking: edit at the first occurrence, and the LATER stop must move with the text.
        f->sci(SCI_SETSELECTION, 10, 9);            // collapse to just the first 'i'
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("index"));
        f->snippetStep(+1);
        check(f->sci(SCI_GETSELECTIONS) == 1, "snippet: Tab moves to the single-occurrence second stop");
        check(f->sci(SCI_GETSELECTIONSTART) == 24 && f->sci(SCI_GETSELECTIONEND) == 25,
              "snippet: the second stop tracked the 4 characters inserted before it");

        f->snippetStep(+1);   // past the last stop -> session ends at the exit position
        check(!f->m_snip.active, "snippet: stepping past the final stop ends the session");
    }
    {
        // Shift+Tab goes back, and Esc abandons the session leaving the text in place.
        load("");
        f->snippetInsert("${1:one} ${2:two}");
        f->snippetStep(+1);
        f->snippetStep(-1);
        check(f->sci(SCI_GETSELECTIONSTART) == 0 && f->sci(SCI_GETSELECTIONEND) == 3,
              "snippet: Shift+Tab returns to the previous stop");
        f->snippetCancel();
        check(!f->m_snip.active && text() == "one two", "snippet: Esc leaves the session and keeps the text");
    }
    {
        // A body with no stops still inserts; there is simply nothing to navigate.
        load("");
        check(!f->snippetInsert("plain text"), "snippet: a body with no stops starts no session");
        check(text() == "plain text", "snippet: ...but the text is still inserted");
    }
    {
        // $0 is where the caret ends up, and it must not be treated as an editable field.
        load("");
        f->snippetInsert("if ($1) { $0 }");
        f->snippetStep(+1);
        check(!f->m_snip.active, "snippet: $0 ends the session rather than becoming a stop");
        check(f->sci(SCI_GETSELECTIONSTART) == f->sci(SCI_GETSELECTIONEND),
              "snippet: ...leaving a caret, not a selection");
    }
    {
        // Indentation of the starting line carries to continuation lines.
        load("    ");
        f->sci(SCI_SETSEL, 4, 4);
        f->snippetInsert("if ($1)\n{\n\t$0\n}");
        const std::string t = text();
        check(t.find("\n    {") != std::string::npos,
              "snippet: continuation lines inherit the starting line's indent");
        f->snippetCancel();
    }
    {
        // The session must not survive a buffer switch - its offsets belong to one document.
        load("");
        f->snippetInsert("${1:x} $1");
        check(f->m_snip.active, "snippet: session live before switching buffers");
        f->addDocument(wxString(), "untitled-snippet-test");
        check(!f->m_snip.active, "snippet: switching to another buffer ends the session");
        f->closeActive();                   // drop the scratch document again
    }

    // ---- snippet transforms: ${1/find/replace/flags} ---------------------------------------------
    {
        load("");
        check(f->snippetInsert("${1:hello world} -> ${1/o/0/g}"),
              "transform: a body with a transform starts a session");
        check(text() == "hello world -> hell0 w0rld",
              "transform: seeded from the placeholder at insert time");

        // Retype the stop; the transform must follow when the stop is left.
        f->sci(SCI_SETSELECTION, 11, 0);
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("foo boo"));
        f->snippetStep(+1);
        check(text() == "foo boo -> f00 b00", "transform: recomputed from the stop's new text on Tab");
        f->snippetCancel();
    }
    {
        // Without 'g' only the first match is replaced; 'i' makes it case-insensitive.
        load("");
        f->snippetInsert("${1:aaa} ${1/a/X/}");
        check(text() == "aaa Xaa", "transform: no 'g' replaces only the first match");
        f->snippetCancel();

        load("");
        f->snippetInsert("${1:ABC} ${1/b/x/i}");
        check(text() == "ABC AxC", "transform: 'i' matches case-insensitively");
        f->snippetCancel();
    }
    {
        // Case operators in the replacement, which is what PCRE2 unblocked.
        load("");
        f->snippetInsert("${1:some name} ${1/(\\w+)/\\U$1/g}");
        check(text() == "some name SOME NAME", "transform: \\U works in a transform replacement");
        f->snippetCancel();
    }
    {
        // A transform is never a navigation target, and never selected for typing.
        load("");
        f->snippetInsert("${1:x} ${1/x/y/} $2");
        check(f->sci(SCI_GETSELECTIONS) == 1,
              "transform: the derived field is excluded from the mirror selection");
        f->snippetCancel();
    }
    {
        // A broken transform must leave the text alone rather than emptying the field. Assert the
        // WHOLE string: a prefix check passed even if the derived field was filled with garbage.
        load("");
        f->snippetInsert("${1:keep} ${1/(unclosed/z/}");
        check(text() == "keep ", "transform: an invalid pattern leaves the stop's text intact");
        f->snippetCancel();
    }
    {
        // ADJACENCY. A transform written at a field boundary used to be absorbed by the neighbour:
        // the primary grew over the derived text, so landing selected it and every Tab compounded.
        load("");
        f->snippetInsert("${1:foo}${1/o/0/g}");
        check(text() == "foof00", "adjacency: a transform with no separator expands correctly");
        check(f->sci(SCI_GETSELECTIONSTART) == 0 && f->sci(SCI_GETSELECTIONEND) == 3,
              "adjacency: the primary still covers ONLY its own text, not the derived output");
        f->snippetStep(+1);
        check(text() == "foof00", "adjacency: a second pass does not compound");
        f->snippetCancel();
    }
    {
        // The exit caret must land AFTER a trailing transform, not inside it. The synthetic $0 shares
        // the transform's offset, so it has to be pushed past the derived text rather than grown.
        load("");
        f->snippetInsert("${1:ab} ${1/a/X/}");
        check(text() == "ab Xb", "exit caret: body expands");
        f->snippetStep(+1);   // past the last stop -> lands on $0 and ends the session
        check(!f->m_snip.active, "exit caret: session ends");
        check(f->sci(SCI_GETCURRENTPOS) == 5,
              "exit caret: lands at the END of the snippet, not before the derived text");
    }
    {
        // Two transforms on one stop: the back-to-front loop must not let one overwrite the other.
        load("");
        f->snippetInsert("${1:ab} ${1/a/X/} ${1/b/Y/}");
        check(text() == "ab Xb aY", "two transforms: both are written, neither is lost");
        f->snippetCancel();
    }
    {
        // Esc must finalize before ending the session - otherwise the pre-edit derived text is what
        // stays in the document, and gets saved. No test drove this path before.
        load("");
        f->snippetInsert("${1:hello} ${1/l/L/g}");
        check(text() == "hello heLLo", "esc: seeded");
        f->sci(SCI_SETSELECTION, 5, 0);
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("full"));
        wxKeyEvent esc(wxEVT_KEY_DOWN);
        esc.m_keyCode = WXK_ESCAPE;
        f->onStcKeyDown(esc);                       // the real handler, not snippetCancel
        check(!f->m_snip.active, "esc: the key handler ends the session");
        check(text() == "full fuLL", "esc: ...after bringing the derived field up to date");
    }
    {
        // Saving mid-session must finalize too - Ctrl+S was the commonest way to commit stale text.
        load("");
        f->snippetInsert("${1:hello} ${1/l/L/g}");
        f->sci(SCI_SETSELECTION, 5, 0);
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("bell"));
        const wxString tmp = wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxn_snip_save.txt";
        f->writeFile(tmp);
        wxString onDisk;
        { wxFile r(tmp); r.ReadAll(&onDisk); }
        check(onDisk.StartsWith("bell beLL"), "save: Ctrl+S finalizes before the bytes go out");
        f->snippetCancel();
        wxRemoveFile(tmp);
    }
    {
        // Undo cannot be tracked, so the session must end rather than write through stale offsets.
        load("x");
        f->sci(SCI_SETSELECTION, 1, 1);
        f->snippetInsert("${1:a} ${1/a/A/}");
        check(f->m_snip.active, "undo: session live");
        f->sci(SCI_UNDO);
        check(!f->m_snip.active, "undo: the session ends rather than tracking a rewritten history");
        check(text() == "x", "undo: insert plus seeding is ONE undo step");
    }
    {
        // A whole-document rewrite likewise ends the session; before, every field aliased to the
        // whole buffer and the next Tab replaced the entire file with a transform of itself.
        load("");
        f->snippetInsert("${1:a} ${1/a/A/}");
        check(f->m_snip.active, "doc-rewrite: session live");
        f->setDocUtf8("completely different text");
        check(!f->m_snip.active, "doc-rewrite: SCI_SETTEXT ends the session");
        check(text() == "completely different text", "doc-rewrite: ...and does not corrupt the new text");
    }

    // ---- (10) wrap-around, in-selection bounds, and Replace-and-find-next ------------------------
    {
        load("target a target b");
        FindOpts o = opts("t.rget", true);
        o.wrap = true;
        // "target a target b" - matches start at 0 and 9. From 6 the next one forward is 9; from the
        // end of that one there is nothing left, so the second call must wrap back to 0.
        f->sci(SCI_SETSEL, 6, 6);
        check(f->doFindNext(o) && f->sci(SCI_GETSELECTIONSTART) == 9,
              "wiring: forward search finds the match after the caret");
        check(f->doFindNext(o) && f->sci(SCI_GETSELECTIONSTART) == 0,
              "wiring: ...then wraps to the top rather than reporting failure");
    }
    {
        // "In selection" must bound a regex search exactly as it bounds a literal one.
        load("aaa bbb aaa");
        FindOpts o = opts("a+", true);
        o.inSelection = true;
        f->sci(SCI_SETSEL, 8, 11);                   // only the trailing "aaa"
        check(f->doCount(o) == 1, "wiring: In selection bounds a regex Count to the selected range");
        o.repl = "Z";
        check(f->doReplaceAll(o) == 1 && text() == "aaa bbb Z",
              "wiring: ...and bounds Replace All to it too");
    }
    {
        // Replace (one) replaces the current match then advances to the next.
        load("x1 x2 x3");
        FindOpts o = opts("x(\\d)", true);
        o.repl = "[$1]";
        f->sci(SCI_SETSEL, 0, 0);
        f->doFindNext(o);                            // select the first match
        check(f->doReplaceOne(o), "wiring: Replace replaces the selection and finds the next");
        check(text() == "[1] x2 x3", "wiring: ...expanding the group in the replacement");
        check(f->sci(SCI_GETSELECTIONSTART) == 4, "wiring: ...and leaves the NEXT match selected");
    }

    // ---- (11) the pattern cache must notice an option change, not just a new pattern ---------------
    // regexReady() caches on (pattern, matchCase, wholeWord). Keying on the pattern alone would make
    // a case-sensitivity toggle silently reuse the wrong compiled pattern.
    {
        load("Alpha alpha");
        FindOpts ci = opts("alpha", true); ci.matchCase = false;
        FindOpts cs = opts("alpha", true); cs.matchCase = true;
        check(f->doCount(ci) == 2, "cache: case-insensitive counts both");
        check(f->doCount(cs) == 1, "cache: the SAME pattern re-compiles when matchCase changes");
        check(f->doCount(ci) == 2, "cache: and back again");

        FindOpts ww = opts("alpha", true); ww.wholeWord = true; ww.matchCase = true;
        load("alpha alphabet");
        check(f->doCount(ww) == 1, "cache: whole-word re-compiles rather than reusing the plain pattern");
        FindOpts nw = opts("alpha", true); nw.matchCase = true;
        check(f->doCount(nw) == 2, "cache: ...and clearing whole-word re-compiles again");

        // ". matches newline" is part of the key for the same reason - it changes the compiled pattern.
        load("a\nb");
        FindOpts plain = opts("a.b", true);
        FindOpts dotnl = opts("a.b", true); dotnl.dotAll = true;
        check(f->doCount(plain) == 0, "dotall: '.' does not cross a line break by default");
        check(f->doCount(dotnl) == 1, "dotall: ...and does when the option is on");
        check(f->doCount(plain) == 0, "cache: toggling it back re-compiles rather than reusing");
    }

    // ---- (12) Find in Files, on real files, with a pattern that spans lines -----------------------
    {
        const wxString dir = wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxn_fif_selftest";
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        const wxString fa = dir + wxFILE_SEP_PATH + "a.txt";
        { wxFile w(fa, wxFile::write); w.Write("one\nBEGIN\nmiddle\nEND\ntwo\n"); }

        std::vector<FifHit> hits;
        FindOpts o = opts("BEGIN[\\s\\S]*?END", true);
        f->fifScanFile(fa, o, hits);
        check(hits.size() == 1, "fif: a multi-line pattern matches inside a real file");
        check(!hits.empty() && hits[0].line == 2, "fif: ...reported at the line the match STARTS on");

        hits.clear();
        FindOpts lit = opts("middle", false);
        f->fifScanFile(fa, lit, hits);
        check(hits.size() == 1 && hits[0].line == 3, "fif: literal search still works alongside it");

        // A zero-width pattern must terminate here too - this loop has its own advance.
        hits.clear();
        FindOpts zw = opts("^", true);
        f->fifScanFile(fa, zw, hits, /*cap=*/100);
        check(hits.size() == 5, "fif: a zero-width pattern terminates, one hit per line");

        wxRemoveFile(fa);
        wxFileName::Rmdir(dir);
    }

    // ---- (13) snippets: trigger expansion and the user-file override ------------------------------
    {
        load("");
        f->sci(SCI_SETSEL, 0, 0);
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("todo"));
        check(f->snippetExpandTrigger(), "snippet: a known trigger word expands on Tab");
        check(text().find("TODO(") == 0, "snippet: ...replacing the trigger with the body");
        f->snippetCancel();

        load("");
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("notasnippet"));
        check(!f->snippetExpandTrigger(), "snippet: an unknown word does not expand (Tab still indents)");
        check(text() == "notasnippet", "snippet: ...and the word is left alone");
    }
    {
        // The user file is re-read when it changes, and overrides a built-in of the same name.
        const wxString sp = f->userDataDir() + wxFILE_SEP_PATH + "snippets.txt";
        const bool had = wxFileExists(sp);
        wxString saved;
        if (had) { wxFile r(sp); r.ReadAll(&saved); }

        { wxFile w(sp, wxFile::write); w.Write("[*:todo]\nUSERTODO $0\n"); }
        load("");
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("todo"));
        f->snippetExpandTrigger();
        check(text().find("USERTODO") == 0, "snippet: a user snippet overrides the built-in of the same name");
        f->snippetCancel();

        if (had) { wxFile w(sp, wxFile::write); w.Write(saved); }
        else     wxRemoveFile(sp);
        // ...and the override disappears again once the file does, proving the stat-based reload works
        // rather than the store having been cached once at startup.
        load("");
        f->sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("todo"));
        f->snippetExpandTrigger();
        check(!had ? text().find("TODO(") == 0 : true,
              "snippet: removing the user file restores the built-in on the very next expansion");
        f->snippetCancel();
    }

    // ---- preferences reach BOTH halves of a split, and every DOCUMENT -----------------------------
    // applySettings drove bare sci(), which targets whichever view has focus. The other half of a
    // split kept the previous preferences until it was next focused, so wrap, whitespace, caret and
    // the line-number margin visibly disagreed across the splitter.
    {
        const int  savedCaret = f->m_caretWidth;
        const bool savedWrap  = f->m_wrap;

        f->m_caretWidth = 3;
        f->m_wrap       = true;
        f->applySettings();

        check(f->m_main.stc && f->m_sub.stc, "settings: both views exist from construction");
        check(sciSend(f->m_main.stc, SCI_GETCARETWIDTH) == 3, "settings: caret width reaches the main view");
        check(sciSend(f->m_sub.stc,  SCI_GETCARETWIDTH) == 3,
              "settings: ...and the second view, which is not the focused one");
        check(sciSend(f->m_main.stc, SCI_GETWRAPMODE) == SC_WRAP_WORD,
              "settings: wrap mode reaches the main view");
        check(sciSend(f->m_sub.stc,  SCI_GETWRAPMODE) == SC_WRAP_WORD,
              "settings: wrap mode reaches the second view too");
        // The margin is part of the per-view work (updateLineMarginFor) - assert it, not just the
        // raw style settings. Line numbers default on, so both views must have a sized margin 0.
        check(sciSend(f->m_main.stc, SCI_GETMARGINWIDTHN, 0) > 0 &&
              sciSend(f->m_sub.stc,  SCI_GETMARGINWIDTHN, 0) > 0,
              "settings: the line-number margin is sized on both views");

        // A SECOND, different value - not a revert to savedCaret, which the second view already held
        // and so passed even before the fix.
        f->m_caretWidth = 2;
        f->applySettings();
        check(sciSend(f->m_sub.stc, SCI_GETCARETWIDTH) == 2, "settings: ...and a later change reaches it again");

        f->m_caretWidth = savedCaret;
        f->m_wrap       = savedWrap;
        f->applySettings();
    }
    {
        // Tab width and use-tabs live on the DOCUMENT, not the view: a fresh Scintilla document
        // starts at tabInChars 8 / useTabs true regardless of preferences, and nothing re-applied
        // them, so every newly opened tab silently ignored both settings.
        const int  savedWidth = f->m_tabWidth;
        const bool savedTabs  = f->m_useTabs;

        f->m_tabWidth = 3;
        f->m_useTabs  = false;
        f->applySettings();
        check(f->sci(SCI_GETTABWIDTH) == 3, "settings: tab width applies to the mounted document");
        check(f->sci(SCI_GETUSETABS) == 0, "settings: ...and so does use-tabs");

        f->addDocument(wxString(), "untitled-tabwidth-test");   // activates the new page
        check(f->sci(SCI_GETTABWIDTH) == 3,
              "settings: a NEW document gets the configured tab width, not Scintilla's default 8");
        check(f->sci(SCI_GETUSETABS) == 0, "settings: ...and the configured use-tabs, not its default true");
        f->closeActive();                                        // drop the scratch document (never dirtied)

        f->m_tabWidth = savedWidth;
        f->m_useTabs  = savedTabs;
        f->applySettings();
    }

    // Leave the editor exactly as it was found. Every test above wrote into the buffer, and a DIRTY
    // buffer makes the next close - by a later phase of this suite, or by shutdown - raise a modal
    // "save changes?" prompt with nobody to answer it. That is a hang, not a failure: the process sat
    // at 4 seconds of CPU for ten minutes before this cleanup existed.
    f->setDocUtf8("");
    f->sci(SCI_EMPTYUNDOBUFFER);
    f->sci(SCI_SETSAVEPOINT);
    check(f->sci(SCI_GETMODIFY) == 0, "editor seams: the buffer is left clean for the phases that follow");
}

// ---- the sandbox (set in main() BEFORE wxEntry, read by the traits below) --------------------------
static wxString g_sandboxRoot;       // <temp>/wxnote_bridge_selftest
static wxString g_sandboxUserData;   // <root>/userdata - what the app believes its user-data dir is

// Headlessly answer the confirmClose "wxNote" save prompt (no OS input injection). The whole run has
// AskBeforeClose armed (so the Phase-4 shutdown VETO path can be driven), but every close in Phases 1-3
// wants the old discard-and-proceed behaviour, so the DEFAULT answer is Don't Save (wxID_NO -> discard,
// close proceeds). The shutdown test arms g_closeAnswer to wxID_CANCEL for one vetoed close, forcing
// confirmClose to return false. Any dialog that is NOT the "wxNote" prompt is shown as usual (wxID_NONE).
static int g_closeAnswer = wxID_NO;
class CloseDialogHook : public wxModalDialogHook
{
protected:
    int Enter(wxDialog* dlg) override
    { return (dlg && dlg->GetTitle() == "wxNote") ? g_closeAnswer : wxID_NONE; }
};

// wxStandardPaths whose GetUserDataDir() lands in the sandbox. Everything keyed off userDataDir() -
// session, recovery backups, keymap store, the POSIX plugins root - follows automatically, because
// main.cpp resolves it through wxStandardPaths::Get() on every call.
class SandboxStandardPaths : public wxStandardPaths
{
public:
    SandboxStandardPaths() = default;
    wxString GetUserDataDir() const override
    {
        return g_sandboxUserData.empty()
            ? wxFileName::GetTempDir() + wxFILE_SEP_PATH + "wxnote_bridge_selftest" + wxFILE_SEP_PATH + "userdata"
            : g_sandboxUserData;
    }
};
class SandboxTraits : public wxGUIAppTraits
{
public:
    wxStandardPaths& GetStandardPaths() override { return m_paths; }
private:
    SandboxStandardPaths m_paths;
};

// ---- probe-log access ------------------------------------------------------------------------------
// The probe writes <plugins-config-dir>/wxn_probe.jsonl: exeDir\plugins\Config on Windows (the same
// layout the bridge's loader scans), <user-data>/plugins/Config off-Windows (sandboxed above).
static wxString probeLogPathStr()
{
#ifdef __WXMSW__
    return wxPathOnly(wxStandardPaths::Get().GetExecutablePath())
         + "\\plugins\\Config\\wxn_probe.jsonl";
#else
    return g_sandboxUserData + "/plugins/Config/wxn_probe.jsonl";
#endif
}
// The scratch session file the probe's Phase-5 table writes (next to the log, in the plugins-config dir).
static wxString probeSessionPathStr()
{
#ifdef __WXMSW__
    return wxPathOnly(wxStandardPaths::Get().GetExecutablePath())
         + "\\plugins\\Config\\p5sess.xml";
#else
    return g_sandboxUserData + "/plugins/Config/p5sess.xml";
#endif
}
static std::vector<std::string> readLogLines()
{
    std::vector<std::string> out;
    wxFile f;
    {
        wxLogNull noLog;
        if (!f.Open(probeLogPathStr())) return out;
    }
    wxString all;
    f.ReadAll(&all, wxConvUTF8);
    const std::string s(all.utf8_str());
    size_t start = 0;
    while (start < s.size())
    {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        if (nl > start) out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}
static int findFrom(const std::vector<std::string>& L, size_t from, const std::string& needle)
{
    for (size_t i = from; i < L.size(); ++i)
        if (L[i].find(needle) != std::string::npos) return static_cast<int>(i);
    return -1;
}
static int countFrom(const std::vector<std::string>& L, size_t from, const std::string& needle)
{
    int n = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (L[i].find(needle) != std::string::npos) ++n;
    return n;
}
// The exact line the probe writes for a notification - the trailing '}' pins the id (no prefix match).
static std::string notifNeedle(unsigned code, unsigned long long idFrom)
{
    char b[96];
    std::snprintf(b, sizeof(b), "{\"k\":\"n\",\"c\":%u,\"i\":%llu}", code, idFrom);
    return b;
}
static std::string notifNeedleForPage(unsigned code, intptr_t pageId)
{
    return notifNeedle(code, static_cast<unsigned long long>(static_cast<uintptr_t>(pageId)));
}
// Same notification line, id-agnostic (prefix-only match) - for a notification whose triggering id is
// not known ahead of the search (e.g. a startup-time recovery restore whose fresh page id we never see).
static std::string notifNeedlePrefix(unsigned code)
{
    char b[64];
    std::snprintf(b, sizeof(b), "{\"k\":\"n\",\"c\":%u,\"i\":", code);
    return b;
}
// {"k":"<key>","ok":N,"first":N,"count":N} - the probe's allocator-result lines.
static bool parseAlloc(const std::vector<std::string>& L, const char* key,
                       long long& ok, int& first, int& count)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"%s\",", key);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return false;
    return std::sscanf(L[i].c_str(), "{\"k\":\"%*[^\"]\",\"ok\":%lld,\"first\":%d,\"count\":%d",
                       &ok, &first, &count) == 3;
}
static bool parseKV(const std::vector<std::string>& L, const char* prefix, long long& v)
{
    const int i = findFrom(L, 0, prefix);
    if (i < 0) return false;
    const size_t p = L[i].find(prefix);
    return std::sscanf(L[i].c_str() + p + std::strlen(prefix), "%lld", &v) == 1;
}
// The Phase-2 scripted table logs one {"k":"p2","m":<msg>,"r":<ret>,"s":"<str>"} line per message (the
// table runs exactly once - g_p2done guards it - so there is a single line per message in the whole log).
// p2ret returns the message's return value (LLONG_MIN if the line is missing/malformed); p2str returns
// the string it wrote back ("\x01" if missing).
static long long p2ret(const std::vector<std::string>& L, unsigned msg)
{
    char pre[48];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p2\",\"m\":%u,", msg);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"r\":");
    long long r = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &r) != 1) return LLONG_MIN;
    return r;
}
static std::string p2str(const std::vector<std::string>& L, unsigned msg)
{
    char pre[48];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p2\",\"m\":%u,", msg);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return "\x01";
    const size_t p = L[i].find("\"s\":\"");
    const size_t end = L[i].rfind("\"}");
    if (p == std::string::npos || end == std::string::npos || end < p + 5) return "\x01";
    return L[i].substr(p + 5, end - (p + 5));
}
// The Phase-3 table logs one {"k":"p3","t":"<tag>","v":<val>} line per probe. p3val returns the value
// for a tag (LLONG_MIN if the line is missing/malformed), searching from a mark so the parser sees only
// this run's lines.
static long long p3val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p3\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The Phase-4 table logs {"k":"p4","t":"<tag>","v":<val>} lines (same shape as p3).
static long long p4val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p4\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The Phase-5 table logs {"k":"p5","t":"<tag>","v":<val>} lines (same shape as p3/p4).
static long long p5val(const std::vector<std::string>& L, const char* tag)
{
    char pre[64];
    std::snprintf(pre, sizeof(pre), "{\"k\":\"p5\",\"t\":\"%s\",", tag);
    const int i = findFrom(L, 0, pre);
    if (i < 0) return LLONG_MIN;
    const size_t p = L[i].find("\"v\":");
    long long v = 0;
    if (p == std::string::npos || std::sscanf(L[i].c_str() + p + 4, "%lld", &v) != 1) return LLONG_MIN;
    return v;
}
// The probe logs one {"k":"gm","mt":<modificationType>} line per NPPN_GLOBALMODIFIED. countGm counts
// them (from a mark); gmOrFrom ORs their modificationType bitsets (the opted-in flags that came through).
static int countGm(const std::vector<std::string>& L, size_t from)
{
    int n = 0; unsigned mt = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (std::sscanf(L[i].c_str(), "{\"k\":\"gm\",\"mt\":%u", &mt) == 1) ++n;
    return n;
}
static unsigned gmOrFrom(const std::vector<std::string>& L, size_t from)
{
    unsigned acc = 0, mt = 0;
    for (size_t i = from; i < L.size(); ++i)
        if (std::sscanf(L[i].c_str(), "{\"k\":\"gm\",\"mt\":%u", &mt) == 1) acc |= mt;
    return acc;
}

static void pump(int ms = 60)
{
    wxYield();
    if (ms > 0) wxMilliSleep(ms);
    wxYield();
}

static bool writeWholeFile(const wxString& path, const char* content)
{
    wxLogNull noLog;
    wxFile f(path, wxFile::write);
    return f.IsOpened() && f.Write(content, std::strlen(content)) == std::strlen(content);
}

// Delete a file an assertion depends on having found. Empty path = the lookup that produced it found
// nothing, which is a test failure to REPORT, never an OS call to attempt (wxRemoveFile("") logs a
// system error, and before logging was disabled that meant a modal dialog and a wedged run).
static bool removeFound(const wxString& path) { return !path.empty() && wxRemoveFile(path); }

// Fixture helpers shared by the autocomplete and crash-backup blocks (each grew its own copies first;
// the hand-counted APPENDTEXT byte lengths were exactly the constants that drift when a fixture changes).
static void openDoc(const wxString& p) { g_nibDocOpen(std::string(p.utf8_str()).c_str()); pump(); }
static void appendText(const char* t)
{ nibSciCall(nullptr, -1, SCI_APPENDTEXT, (int)std::strlen(t), reinterpret_cast<intptr_t>(t)); pump(); }
// 63 x's + \n filler: pure ASCII (byte counts survive load untouched - no EOL rewriting), no 'wor'
// anywhere, and no line long enough to trip the byLine large-file threshold.
static std::string filler(size_t n)
{
    std::string s; s.reserve(n + 64);
    while (s.size() < n) { s.append(63, 'x'); s += '\n'; }
    s.resize(n); return s;
}

// The host's toolbar, wherever it is parented. Must recurse: on macOS the bar hangs off m_toolBarHost
// (a wxPanel) rather than the frame, deliberately, so wxToolBar::Create leaves m_macToolbar null and wx
// lays it out itself instead of promoting it to a native NSToolbar - see the __WXMAC__ branch of the
// toolbar builder in src/main.cpp. A one-level child scan therefore finds nothing on macOS.
static wxToolBar* findToolBarIn(wxWindow* w)
{
    if (!w) return nullptr;
    for (wxWindow* c : w->GetChildren())
    {
        if (auto* t = wxDynamicCast(c, wxToolBar)) return t;
        if (auto* deeper = findToolBarIn(c)) return deeper;
    }
    return nullptr;
}
static wxString readWholeFile(const wxString& path)
{
    wxLogNull noLog;
    wxString out;
    wxFile f(path, wxFile::read);
    if (f.IsOpened()) f.ReadAll(&out, wxConvUTF8);
    return out;
}

// ====================================================================================================
class BridgeSelfTestApp : public WxnApp
{
public:
    bool OnInit() override
    {
        // Sandbox the config BEFORE the base OnInit's first wxConfigBase::Get() touch (readUiLang):
        // with an explicit object Set(), wx never auto-creates the registry/user config, so
        // ReuseInstance/IntegratedBar/theme/session state all resolve from this scratch file - and
        // the reuse-instance IPC handoff to a genuinely running wxnote can never trigger.
        wxFileName::Mkdir(g_sandboxUserData, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        wxConfigBase::Set(new wxFileConfig("wxNoteBridgeSelftest", wxEmptyString,
                                           g_sandboxUserData + wxFILE_SEP_PATH + "selftest.ini",
                                           wxEmptyString, wxCONFIG_USE_LOCAL_FILE));
        // Arm "Ask before closing unsaved changes" so the Phase-4 shutdown-VETO path (confirmClose ->
        // Cancel) can be driven headlessly; the CloseDialogHook auto-answers the resulting prompt (Don't
        // Save by default), so no close in Phases 1-3 hangs. Written before OnInit reads it (loadSettings).
        wxConfigBase::Get()->Write("AskBeforeClose", true);
        // Phase 6: seed one dirty-recovery backup BEFORE WxnApp::OnInit() runs restoreSession() ->
        // restoreRecoveryBackups() (main.cpp), so the SNAPSHOTDIRTYFILELOADED assertion in runAll()
        // observes a REAL startup recovery restore - same manifest-entry + ".bak" file shape
        // backupUnsavedChanges() writes on a real crash, just written directly instead of driving one.
        // Path left empty (untitled-style recovery), so restoreRecoveryBackups() opens it as a fresh tab.
        {
            const wxString recDir = g_sandboxUserData + wxFILE_SEP_PATH + "RecoveryBackups";
            wxFileName::Mkdir(recDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            wxFile bak(recDir + wxFILE_SEP_PATH + "seed1.bak", wxFile::write);
            if (bak.IsOpened())
            { const char content[] = "phase-6 seeded dirty recovery content\n"; bak.Write(content, sizeof(content) - 1); }
            wxConfigBase::Get()->Write("Recovery/seed1/Title", wxString("P6 Recovered"));
        }
        wxConfigBase::Get()->Flush();
        m_closeHook.Register();
        if (!WxnApp::OnInit()) return false;   // the REAL boot: frame + nib surface + loadNibPlugins()
        CallAfter([this] { runAll(); });       // run once the loop is live (CallAfter/event paths behave as in the app)
        return true;
    }
    wxAppTraits* CreateTraits() override { return new SandboxTraits; }

private:
    CloseDialogHook m_closeHook;   // registered in OnInit; auto-unregisters on destruction

    void runAll()
    {
        std::printf("bridge_selftest\n");

        // ---- boot phase: the probe reached us through host -> npp_bridge -> probe ------------------
        pump(120);   // let any boot-time stragglers land before the log snapshot
        std::vector<std::string> L = readLogLines();
        check(!L.empty(), "probe log exists (host loaded npp_bridge, bridge loaded the probe)");
        long long pid = -1;
        parseKV(L, "{\"k\":\"start\",\"pid\":", pid);
        check(pid == static_cast<long long>(wxGetProcessId()),
              "log pid-stamp matches this process (fresh log, not a stale run's)");

        const int iReady = findFrom(L, 0, notifNeedle(NPPN_READY, 0));
        const int iTb    = findFrom(L, 0, notifNeedle(NPPN_TBMODIFICATION, 0));
        check(iReady >= 0, "NPPN_READY delivered to the probe");
        check(iTb >= 0 && iTb > iReady, "NPPN_TBMODIFICATION delivered, after NPPN_READY");

        // ---- toolbar registration (probe ran it inside its TBMODIFICATION handler) ----------------
        long long tbOk = 0; int tbCmd = -1;
        {
            const int i = findFrom(L, 0, "{\"k\":\"tb\",");
            check(i >= 0 && std::sscanf(L[i].c_str(), "{\"k\":\"tb\",\"ok\":%lld,\"cmd\":%d", &tbOk, &tbCmd) == 2,
                  "probe logged its toolbar registration");
        }
        check(tbOk == TRUE, "the probe's toolbar registration answered TRUE (NPPM_ADDTOOLBARICONBYNAME)");
        check(tbCmd >= NIB_ALLOC_CMD_FIRST && tbCmd <= NIB_ALLOC_CMD_LAST,
              "the probe's FuncItem got a host-granted command id (bridge wrote it into _cmdID)");

        // ---- the registered button must actually carry a visible image on the host toolbar ---------
        // Registration answering TRUE is not the same as a button a user can see: the stored bundle could
        // rasterize blank (a bad asset name resolving to an empty bundle, or alpha lost on the way to the
        // bundle) and this suite would still have been all-green. Rasterize what the toolbar actually
        // holds and demand the glyph's pixels survived.
        {
            // ---- Command Palette: the HARVEST, against the real live menu bar ----------------------
            // The dialog is ordinary wx; what needs proving is that walking the live bar yields clean,
            // dispatchable rows - the part that degrades silently (duplicate ids, separators leaking
            // in, the Language menu's A/B/C buckets becoming fake path segments).
            if (auto* pf = wxDynamicCast(wxTheApp->GetTopWindow(), wxFrame))
            {
                wxMenuBar* mb = pf->GetMenuBar();
                check(mb != nullptr, "palette: live menu bar located");
                if (mb)
                {
                    const std::vector<FilterRow> rows = wxnHarvestCommands(mb, nullptr);
                    check(rows.size() > 200, "palette: harvest yields the real command set (>200 rows)");

                    bool blankPrimary = false, dupe = false, bucketPath = false, badId = false;
                    std::unordered_set<int> ids;
                    for (const FilterRow& r : rows)
                    {
                        if (r.primary.empty()) blankPrimary = true;
                        if (r.userId <= 0)     badId = true;
                        if (!ids.insert(r.userId).second) dupe = true;
                        // A one-character path segment means a Language A/B/C bucket leaked through.
                        //
                        // 0x203A, NOT '›'. In single quotes that is a narrow-char literal holding three
                        // UTF-8 bytes - a multicharacter literal, value implementation-defined (~0xE280BA)
                        // - so it could never equal the real U+203A the harvester emits, and this whole
                        // assertion passed vacuously whatever wxnHarvestMenu did.
                        const wxUniChar kSep(0x203A);
                        wxString seg;
                        for (size_t k = 0; k <= r.secondary.length(); ++k)
                        {
                            const wxUniChar c = (k < r.secondary.length()) ? r.secondary[k] : kSep;
                            if (c == kSep)
                            { if (seg.Strip(wxString::both).length() == 1) bucketPath = true; seg.clear(); }
                            else seg += c;
                        }
                    }
                    check(!blankPrimary, "palette: no row has an empty label");
                    check(!badId,        "palette: every row carries a positive command id");
                    check(!dupe,         "palette: every row has a unique command id");
                    check(!bucketPath,   "palette: single-letter Language buckets are flattened out of the path");

                    bool sawSelf = false;
                    for (const FilterRow& r : rows) if (r.userId == myID_COMMAND_PALETTE) sawSelf = true;
                    check(sawSelf, "palette: its own menu entry is harvested (so it is discoverable)");

                    // End-to-end ranking, over the LIVE harvest.
                    //
                    // The query is the palette row's OWN harvested label, never a hardcoded English
                    // word. This used to type "palette" literally, which passed only by accident: the
                    // Command Palette menu entry was untranslated at the time, so it rendered in
                    // English even in a Polish UI. The moment that catalog bug was fixed the label
                    // became "Paleta poleceń" - which does not contain "palette" at all (no second
                    // 't') - while the encoding entry "OEM 860 : portugalski ... Zachodnioeuropejski"
                    // DOES contain p-a-l-e-t-t-e as a subsequence, and won. The product was fine; the
                    // test was asserting English against labels that are translated at runtime, so it
                    // could only ever hold on an English UI.
                    {
                        const FilterRow* self = nullptr;
                        for (const FilterRow& r : rows) if (r.userId == myID_COMMAND_PALETTE) self = &r;
                        check(self != nullptr, "palette: the palette row is available to query with");
                        if (self)
                        {
                            const std::vector<char32_t> raw = fuzzy::decodeUtf8(self->primary.utf8_string());
                            std::vector<char32_t> fold(raw.size());
                            for (size_t i = 0; i < raw.size(); ++i) fold[i] = fuzzy::foldAscii(raw[i]);
                            const bool smart = fuzzy::smartCaseWanted(raw);
                            int bestScore = -1, bestId = 0;
                            for (const FilterRow& r : rows)
                            {
                                const fuzzy::Result res = fuzzy::score(fold, raw, smart, r.prep);
                                if (res.ok && res.score > bestScore) { bestScore = res.score; bestId = r.userId; }
                            }
                            check(bestId == myID_COMMAND_PALETTE,
                                  "palette: filtering on a command's own label ranks that command first");
                        }
                    }
                }
            }

            // ---- Quick Open: the shared dialog's '@' alternate-set switch ---------------------------
            // The switch is per-keystroke and mid-query, which is exactly the kind of state that breaks
            // quietly: pick from the alternate set, and chosenRow() must still return an ALTERNATE row
            // after the modal has closed, not the file at the same index.
            {
                std::vector<FilterRow> files, syms;
                for (const char* n : { "alpha.cpp", "beta.cpp", "gamma.h" })
                { FilterRow r; r.primary = n; r.userId = 100 + (int)files.size();
                  prepareFilterRow(r, true); files.push_back(r); }
                for (const char* n : { "parseThing", "renderThing" })
                { FilterRow r; r.primary = n; r.userId = 7 + (int)syms.size();
                  prepareFilterRow(r, false); syms.push_back(r); }

                FilterListDialog qd(nullptr, "qo", "", files, /*dark=*/false);
                // The provider must not run until '@' is typed - Quick Open's real one runs the
                // whole-buffer symbol scan, so an eager call would tax every invocation.
                int providerCalls = 0;
                qd.setAltRows('@', [&] { ++providerCalls; return syms; });
                check(providerCalls == 0, "quickopen: the '@' provider is NOT called up front");
                check(qd.rows().size() == 3, "quickopen: the primary set is the file list");
                check(qd.visibleCount() == 3, "quickopen: an empty query shows everything");
                check(!qd.chosenIsAlt(), "quickopen: starts in the file set, not the symbol set");

                qd.setQuery("beta");
                check(qd.visibleCount() == 1 && qd.visibleId(0) == 101,
                      "quickopen: typing narrows the list to the matching file");

                qd.setQuery("zzz");
                check(qd.visibleCount() == 0, "quickopen: a non-matching query yields nothing");

                // The '@' switch: same control, different set, and the prefix itself is not part of the
                // needle - "@render" must match the SYMBOL, and must not be searched among filenames.
                qd.setQuery("@");
                check(providerCalls == 1, "quickopen: typing '@' materializes the symbol set, once");
                check(qd.visibleCount() == 2, "quickopen: '@' alone lists every symbol in the file");
                check(qd.visibleId(0) == 7 || qd.visibleId(0) == 8, "quickopen: '@' lists symbol ids, not file ids");
                qd.setQuery("@render");
                check(qd.visibleCount() == 1 && qd.visibleId(0) == 8,
                      "quickopen: '@' + a needle matches the symbol, with the prefix stripped");

                qd.setQuery("beta");
                check(qd.visibleCount() == 1 && qd.visibleId(0) == 101,
                      "quickopen: deleting the '@' switches back to the file set");
                qd.setQuery("@parse");
                check(providerCalls == 1, "quickopen: re-entering '@' reuses the cached symbol set");
                // NO Destroy() here: qd has automatic storage. wxTopLevelWindowBase::Destroy would queue
                // it on wxPendingDelete, and the idle loop would then `delete` a stack address after the
                // enclosing scope had already run its destructor. RAII is the whole lifetime here, and
                // it is what the two production call sites rely on too.
            }

            // ---- ranking on the row shape prepareFilterRow ACTUALLY builds ---------------------------
            // tests/fuzzy_test.cpp pins "the file name beats the directory" by feeding whole paths
            // ("src/main.cpp") straight to matchOne. prepareFilterRow never produces that shape - it
            // concatenates primary THEN secondary ("main.cpp src"), which moves the last path separator
            // into the deepest directory name. The promise has to be re-checked on the real layout,
            // because it was inverted here while every fuzzy_test assertion still passed.
            {
                auto row = [](const char* name, const char* dir) {
                    FilterRow r; r.primary = name; r.secondary = dir;
                    prepareFilterRow(r, /*pathMode=*/true); return r;
                };
                auto scoreOn = [](const FilterRow& r, const char* needle) {
                    const std::vector<char32_t> raw = fuzzy::decodeUtf8(needle);
                    std::vector<char32_t> fold(raw.size());
                    for (size_t i = 0; i < raw.size(); ++i) fold[i] = fuzzy::foldAscii(raw[i]);
                    const fuzzy::Result res = fuzzy::score(fold, raw, fuzzy::smartCaseWanted(raw), r.prep);
                    return res.ok ? res.score : -1;
                };

                const FilterRow inName = row("util.cpp",   "C:\\ws\\src\\parser");
                const FilterRow inDir  = row("parser.cpp", "C:\\ws\\src\\util");
                check(scoreOn(inName, "util") > scoreOn(inDir, "util"),
                      "ranking: a hit in the FILE NAME outranks the same word in a directory");

                const FilterRow shallow = row("main.cpp", "src");
                const FilterRow deep    = row("other.cpp", "main/a/b");
                check(scoreOn(shallow, "main") > scoreOn(deep, "main"),
                      "ranking: the file name still wins when the directory match is a whole segment");

                // The command palette uses the same layout, primary = command, secondary = menu path.
                FilterRow cmd; cmd.primary = "Save All"; cmd.secondary = "File";
                prepareFilterRow(cmd, /*pathMode=*/false);
                FilterRow path; path.primary = "Print"; path.secondary = "Save All Documents";
                prepareFilterRow(path, /*pathMode=*/false);
                check(scoreOn(cmd, "saveall") > scoreOn(path, "saveall"),
                      "ranking: a command NAME outranks the same words in its menu path");
            }

            // ---- the highlight run-splitter that drawRow paints with ---------------------------------
            // Batching characters into runs is what keeps a row to a couple of DrawText calls instead of
            // one per character. It is pure index arithmetic over two sequences and nothing else would
            // catch an off-by-one here, because a wrong run still paints - just the wrong letters bold.
            {
                auto shape = [](size_t n, const std::vector<int>& hit) {
                    std::string s;
                    for (const HlRun& r : wxnHighlightRuns(n, hit))
                    {
                        for (size_t i = r.begin; i < r.end; ++i) s += r.match ? '#' : '.';
                        s += '|';
                    }
                    return s;
                };
                check(shape(5, {}) == ".....|",        "runs: no hits is ONE plain run");
                check(shape(5, {0,1,2,3,4}) == "#####|", "runs: all hits is ONE match run");
                check(shape(5, {0}) == "#|....|",      "runs: a hit at the start");
                check(shape(5, {4}) == "....|#|",      "runs: a hit at the end");
                check(shape(5, {2}) == "..|#|..|",     "runs: a hit in the middle splits three ways");
                check(shape(6, {1,2,4}) == ".|##|.|#|.|", "runs: adjacent hits merge, separated ones do not");
                check(shape(0, {}) == "",              "runs: empty text yields no runs");
                // Hits past the end of the drawn text: the match positions index the whole
                // "primary secondary" haystack, so anything at or beyond n belongs to the secondary.
                check(shape(3, {0,5,7}) == "#|..|",    "runs: hits beyond the primary are ignored");
                // Every run must be non-empty and they must exactly tile [0,n) in order.
                {
                    const std::vector<HlRun> rr = wxnHighlightRuns(7, {1,2,5});
                    bool tiles = !rr.empty() && rr.front().begin == 0 && rr.back().end == 7;
                    for (size_t i = 0; i < rr.size(); ++i)
                    {
                        if (rr[i].begin >= rr[i].end) tiles = false;
                        if (i && rr[i].begin != rr[i-1].end) tiles = false;
                    }
                    check(tiles, "runs: the runs exactly tile the text, with no gaps or empty runs");
                }
            }

            // The crawler's prune list must not exclude this very repository's source directory - a
            // regression there would make Quick Open silently useless on the project it ships with.
            check(!wxnPruneDir("src") && !wxnPruneDir("tests") && !wxnPruneDir("installer"),
                  "quickopen: the prune list does not swallow this repo's own source directories");
            check(wxnPruneDir("build") && wxnPruneDir(".git"),
                  "quickopen: the prune list does cover the big generated trees");

            // findToolBarIn recurses; the old inline scan looked only at the frame's direct children, which
            // is why this check and the three image checks below failed on macos-arm64 alone (there the bar
            // is a grandchild, parented to a wxPanel on purpose - see findToolBarIn's comment).
            wxToolBar* tb = nullptr;
            if (auto* frame = wxDynamicCast(wxTheApp->GetTopWindow(), wxFrame))
            {
                tb = frame->GetToolBar();                      // classic mode: the frame's own toolbar
                if (!tb) tb = findToolBarIn(frame);            // integrated/macOS: an aui-docked descendant
            }
            check(tb != nullptr, "host toolbar located for the probe-button image check");

            // ---- the search + snippet WIRING, driven through the real frame ---------------------------
            // dynamic_cast, not static_cast: the app builds WxnShellFrame or WxnIntegratedFrame depending
            // on the IntegratedBar preference, and only the former is nameable here. A null result means
            // this run used the borderless chrome, which is a skip rather than a failure.
            if (auto* fr = dynamic_cast<WxnShellFrame*>(wxTheApp->GetTopWindow()))
                wxnDriveEditorSelfTests(fr);
            else
                check(true, "editor seams: skipped (integrated-bar frame in use)");

            // ---- Toggle Comment: the toolbar button and the rule that greys it out -------------------
            // The button is only useful if its icon actually resolved from the active pack; a missing
            // comment.svg would fall back to wxART_QUESTION and still "exist", so check the bitmap too.
            {
                wxToolBarToolBase* ct = tb ? tb->FindById(kCmdEditBlockComment & 0xFFFF) : nullptr;
                check(ct != nullptr, "comment: Toggle Comment is on the toolbar");
                if (ct)
                {
                    const wxBitmap bmp = ct->GetNormalBitmap();
                    check(bmp.IsOk() && bmp.GetWidth() > 0, "comment: its icon resolved to a real bitmap");
                }

                // updateUiState greys the button on exactly this predicate, so pin the predicate. A
                // language with NEITHER a line nor a block form cannot be commented at all.
                auto commentable = [](const char* key) { return !wxnCommentStyleForKey(key).empty(); };
                check(commentable("cpp"),    "comment: enabled for a language with a line form");
                check(commentable("python"), "comment: enabled for a hash language");
                check(commentable("css"),    "comment: enabled for a block-only language (CSS has no //)");
                check(!commentable("json"),  "comment: DISABLED for JSON - no comment form at all");
                check(!commentable("diff"),  "comment: DISABLED for a patch file");
                check(!commentable(""),      "comment: DISABLED when the language is unknown");
            }

            wxToolBarToolBase* tool = tb ? tb->FindById(tbCmd & 0xFFFF) : nullptr;
            check(tool != nullptr, "probe toolbar button exists on the host toolbar (FindById)");
            // The button now carries a HOST asset (NPPM_ADDTOOLBARICONBYNAME), so its hue follows whichever
            // icon pack and theme the host is running - this cannot assert a specific colour. What must hold
            // for every pack is that the image is not blank AND not a featureless block: count opaque pixels,
            // then count the ones that differ from the most common opaque colour - those are the glyph.
            int visible = 0, glyph = 0;
            if (tb && tool)
            {
                const wxBitmap bmp = tool->GetNormalBitmapBundle().GetBitmap(tb->GetToolBitmapSize());
                const wxImage img = bmp.ConvertToImage();
                std::map<unsigned, int> hist;
                for (int y = 0; y < img.GetHeight(); ++y)
                    for (int x = 0; x < img.GetWidth(); ++x)
                    {
                        if (img.HasAlpha() && img.GetAlpha(x, y) < 128) continue;
                        ++visible;
                        ++hist[(static_cast<unsigned>(img.GetRed(x, y)) << 16)
                             | (static_cast<unsigned>(img.GetGreen(x, y)) << 8)
                             |  static_cast<unsigned>(img.GetBlue(x, y))];
                    }
                int modal = 0;
                for (const auto& kv : hist) modal = std::max(modal, kv.second);
                glyph = visible - modal;   // everything that is not the dominant field colour
            }
            check(visible >= 8, "probe button image is not blank (opaque pixels survived to the stored bundle)");
            check(glyph >= 4, "probe button image kept its glyph (pixels distinct from the dominant colour)");
        }

        // ---- (c) allocator grants: inside the pools, disjoint from every host-reserved number ------
        long long okC = 0, okM = 0, okI = 0; int cFirst = -1, mFirst = -1, iFirst = -1, n = 0;
        check(parseAlloc(L, "allocCmd", okC, cFirst, n) && okC == TRUE && n == 4,
              "NPPM_ALLOCATECMDID granted 4 ids (Phase 2/3/4/5 table triggers)");
        check(cFirst >= NIB_ALLOC_CMD_FIRST && cFirst + 3 <= NIB_ALLOC_CMD_LAST,
              "(c) cmd-id grant inside the host pool (64000..64999; clear of kCmd*/menu/NIB_CMD ids)");
        check(parseAlloc(L, "allocMark", okM, mFirst, n) && okM == TRUE && n == 1,
              "NPPM_ALLOCATEMARKER granted 1 marker");
        check(mFirst >= 3 && mFirst <= 20,
              "(c) marker grant inside the host pool (3..20)");
        check(mFirst != MARK_BOOKMARK && !(mFirst >= 21 && mFirst <= 31),
              "(c) marker grant disjoint from bookmark=2, change-history 21..24 and fold chrome 25..31");
        check(parseAlloc(L, "allocInd", okI, iFirst, n) && okI == TRUE && n == 1,
              "NPPM_ALLOCATEINDICATOR granted 1 indicator");
        check((iFirst >= 12 && iFirst <= 20) || (iFirst >= 26 && iFirst <= 31),
              "(c) indicator grant inside the host pools (12..20 / 26..31)");
        check(iFirst > 8 && iFirst != MARK_INDIC && iFirst != SMART_INDIC && iFirst != URL_INDIC
                  && !(iFirst >= MARK_STYLE_BASE && iFirst <= MARK_STYLE_BASE + 4),
              "(c) indicator grant disjoint from lexer 0..8, mark/smart/url 9..11 and mark-styles 21..25");

        // ---- (e) dark-mode probe == host state -----------------------------------------------------
        long long darkV = -1;
        check(parseKV(L, "{\"k\":\"dark\",\"v\":", darkV), "probe logged NPPM_ISDARKMODEENABLED");
        check(darkV == (g_nibUiIsDark ? g_nibUiIsDark() : 0),
              "(e) NPPM_ISDARKMODEENABLED matches the host's real dark state");

        // ---- close the boot-time recovery-restored tab before it can interfere with later doc-count
        // assumptions ---------------------------------------------------------------------------------
        // main() seeded a Recovery/seed1 entry BEFORE wxEntry() (see BridgeSelfTestApp::OnInit), so
        // WxnApp::OnInit()'s restoreSession() -> restoreRecoveryBackups() already restored it as the
        // sole open document (replacing the startup "new 1") before runAll() ever ran. It's dirty and
        // untitled by design - that's exactly what NPPN_SNAPSHOTDIRTYFILELOADED (asserted purely from
        // the log, in Phase 6 below) is about - but left open it is exactly the kind of background
        // dirty/untitled buffer onSaveAll() is supposed to sweep up, so Phase 1's Save-All test just
        // below would activate it and run onSaveAs() on it, popping a REAL blocking Save-As dialog.
        // Close it now (CloseDialogHook auto-answers Don't Save) so the rest of the run has a clean
        // baseline; closing it early doesn't weaken the Phase 6 check, which never re-opens the tab -
        // it just re-reads the boot-time log line.
        check(g_nibDocCount && g_nibDocCount() == 1,
              "sole open document after boot is the recovery-restored tab");
        g_nibInvokeCommand(kCmdFileClose);
        pump();
        check(g_nibDocCount && g_nibDocCount() == 1,
              "recovery tab closed (discarded) - back to a fresh, clean single document");

        // ---- fixture files in the sandbox ----------------------------------------------------------
        const wxString work = g_sandboxRoot + wxFILE_SEP_PATH + "work";
        wxFileName::Mkdir(work, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        const wxString fileA = work + wxFILE_SEP_PATH + "a.txt";
        const wxString fileB = work + wxFILE_SEP_PATH + "b.txt";
        check(writeWholeFile(fileA, "alpha\n") && writeWholeFile(fileB, "bravo\n"),
              "fixture files created in the sandbox");

        // ---- Phase 2: scripted NPPM getter/stub table against a known fixture -----------------------
        // Open a fixture whose single line and caret are known, then trigger the probe's Phase-2 table
        // (it fires on the first allocated cmd id - see the probe's messageProc). The probe calls each
        // Phase-2 message through ::SendMessage and logs {"k":"p2",...}; here we assert the exact
        // returns/strings. This runs BEFORE the open/save/close flow so the fixture is isolated.
        {
            const wxString fileP2 = work + wxFILE_SEP_PATH + "p2.txt";
            // line: "hello wxNote/plugin.cpp bridge" - caret at index 9 sits inside the word "wxNote"
            // (a word boundary at '/') and inside the filename token "wxNote/plugin.cpp" ('/' and '.'
            // are filename chars). Byte offsets: h0 e1 l2 l3 o4 (sp)5 w6 x7 N8 o9 t10 e11 /12 ...
            check(writeWholeFile(fileP2, "hello wxNote/plugin.cpp bridge\n"), "p2 fixture created");
            const int p2BaseDocs = (g_nibDocCount ? g_nibDocCount() : 1);   // restore to this after the table
            g_nibDocOpen(std::string(fileP2.utf8_str()).c_str());
            pump();
            nibSciCall(nullptr, -1, SCI_GOTOPOS, 9, 0);        // caret inside "wxNote"
            pump();
            check(coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0) == 9, "p2 caret placed at index 9");

            const size_t p2mark = readLogLines().size();
            g_nibInvokeCommand(cFirst);                        // fire the probe's Phase-2 table (one-shot)
            pump(120);
            std::vector<std::string> P = readLogLines();
            // discard everything before the trigger so p2ret/p2str see only this run's lines
            P.erase(P.begin(), P.begin() + std::min(p2mark, P.size()));

            // -- the 15 implemented getters --
            check(p2str(P, NPPM_GETCURRENTWORD) == "wxNote" && p2ret(P, NPPM_GETCURRENTWORD) == 6,
                  "NPPM_GETCURRENTWORD == \"wxNote\" (word at caret)");
            check(p2str(P, NPPM_GETCURRENTLINESTR) == "hello wxNote/plugin.cpp bridge"
                      && p2ret(P, NPPM_GETCURRENTLINESTR) == 30,
                  "NPPM_GETCURRENTLINESTR == the caret line, EOL stripped");
            check(p2str(P, NPPM_GETFILENAMEATCURSOR) == "wxNote/plugin.cpp"
                      && p2ret(P, NPPM_GETFILENAMEATCURSOR) == 17,
                  "NPPM_GETFILENAMEATCURSOR == \"wxNote/plugin.cpp\" (filename token at caret)");
            check(p2ret(P, NPPM_GETNPPFULLFILEPATH) > 0 && !p2str(P, NPPM_GETNPPFULLFILEPATH).empty(),
                  "NPPM_GETNPPFULLFILEPATH returns the exe's own full path (non-empty)");
            check(p2ret(P, NPPM_GETNPPSETTINGSDIRPATH) == TRUE
                      && p2str(P, NPPM_GETNPPSETTINGSDIRPATH).find("userdata") != std::string::npos,
                  "NPPM_GETNPPSETTINGSDIRPATH returns the host user-data dir");
            check(p2ret(P, NPPM_GETCURRENTNATIVELANGENCODING) == 65001,
                  "NPPM_GETCURRENTNATIVELANGENCODING == 65001 (UTF-8 code page)");
            check(p2str(P, NPPM_GETLANGUAGENAME) == "C++" && p2ret(P, NPPM_GETLANGUAGENAME) == 3,
                  "NPPM_GETLANGUAGENAME(L_CPP) == \"C++\"");
            check(p2str(P, NPPM_GETLANGUAGEDESC) == "C++ source file" && p2ret(P, NPPM_GETLANGUAGEDESC) == 15,
                  "NPPM_GETLANGUAGEDESC(L_CPP) == \"C++ source file\"");
            check(p2ret(P, NPPM_GETBOOKMARKID) == MARK_BOOKMARK,
                  "NPPM_GETBOOKMARKID == the host's bookmark marker number (2)");
            check(p2ret(P, NPPM_GETAPPDATAPLUGINSALLOWED) == TRUE,
                  "NPPM_GETAPPDATAPLUGINSALLOWED == TRUE (plugins load from the user-writable dir)");
            check(p2ret(P, NPPM_SETSMOOTHFONT) == TRUE, "NPPM_SETSMOOTHFONT answered TRUE");
            check(p2ret(P, NPPM_RELOADBUFFERID) == TRUE,
                  "NPPM_RELOADBUFFERID reloaded the fixture buffer by id");
#ifdef _WIN32
            check(p2ret(P, NPPM_GETWINDOWSVERSION) > 0,
                  "NPPM_GETWINDOWSVERSION returns a real winVer on Windows");
            check(p2ret(P, NPPM_GETCURRENTCMDLINE) > 0 && !p2str(P, NPPM_GETCURRENTCMDLINE).empty(),
                  "NPPM_GETCURRENTCMDLINE returns the process command line on Windows");
#else
            check(p2ret(P, NPPM_GETWINDOWSVERSION) == WV_UNKNOWN,
                  "NPPM_GETWINDOWSVERSION == WV_UNKNOWN off-Windows (honest, no fragile per-OS hack)");
            check(p2ret(P, NPPM_GETCURRENTCMDLINE) == 0,
                  "NPPM_GETCURRENTCMDLINE == empty off-Windows (documented: needs a host hook)");
#endif
            // -- the documented no-op / interim stubs: every message answers (zero silent drops) --
            struct { unsigned msg; long long want; const char* what; } stubs[] = {
                { NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED,      TRUE,  "DESTROYSCINTILLAHANDLE_DEPRECATED -> TRUE (N++ no-ops it)" },
                { NPPM_ENCODESCI,                              1,     "ENCODESCI -> UTF-8 UniMode (buffers are always UTF-8)" },
                { NPPM_DECODESCI,                              1,     "DECODESCI -> UTF-8 UniMode" },
                { NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED,   0,     "GETENABLETHEMETEXTUREFUNC_DEPRECATED -> 0" },
                { NPPM_DOCLISTDISABLEEXTCOLUMN,                TRUE,  "DOCLISTDISABLEEXTCOLUMN -> TRUE (no columns to disable)" },
                { NPPM_DOCLISTDISABLEPATHCOLUMN,               TRUE,  "DOCLISTDISABLEPATHCOLUMN -> TRUE" },
                { NPPM_DISABLEAUTOUPDATE,                      TRUE,  "DISABLEAUTOUPDATE -> TRUE (no updater)" },
                { NPPM_GETEXTERNALLEXERAUTOINDENTMODE,         FALSE, "GETEXTERNALLEXERAUTOINDENTMODE -> FALSE" },
                { NPPM_SETEXTERNALLEXERAUTOINDENTMODE,         FALSE, "SETEXTERNALLEXERAUTOINDENTMODE -> FALSE" },
                { NPPM_MODELESSDIALOG,                         0x5EED, "MODELESSDIALOG -> echoes the passed handle (Annex W portable no-op)" },
                { NPPM_DMMVIEWOTHERTAB,                        0,     "DMMVIEWOTHERTAB -> NULL (Annex W portable no-op)" },
                { NPPM_SETEDITORBORDEREDGE,                    FALSE, "SETEDITORBORDEREDGE -> FALSE (Annex W portable no-op)" },
                { NPPM_DMMGETPLUGINHWNDBYNAME,                 0,     "DMMGETPLUGINHWNDBYNAME -> NULL (Annex W portable no-op)" },
                { NPPM_CREATESCINTILLAHANDLE,                  0,     "CREATESCINTILLAHANDLE -> NULL" },
                { NPPM_HIDETABBAR,                             FALSE, "HIDETABBAR -> FALSE" },
                { NPPM_ISTABBARHIDDEN,                         FALSE, "ISTABBARHIDDEN -> FALSE" },
                { NPPM_DARKMODESUBCLASSANDTHEME,               0,     "DARKMODESUBCLASSANDTHEME -> 0" },
                { NPPM_REMOVESHORTCUTBYCMDID,                  FALSE, "REMOVESHORTCUTBYCMDID -> FALSE (Phase 5 documented no-op)" },
                { NPPM_TRIGGERTABBARCONTEXTMENU,               FALSE, "TRIGGERTABBARCONTEXTMENU -> FALSE (Phase 5 documented no-op)" },
            };
            bool allStubs = true;
            for (const auto& s : stubs)
                if (p2ret(P, s.msg) != s.want) { allStubs = false; check(false, s.what); }
            check(allStubs, "every documented no-op/interim stub returned its exact documented value");
            check(p2ret(P, NPPM_GETSETTINGSONCLOUDPATH) == 0 && p2str(P, NPPM_GETSETTINGSONCLOUDPATH).empty(),
                  "GETSETTINGSONCLOUDPATH -> empty string (no cloud settings)");

            // Clean up: RELOADBUFFERID re-opens the (already-open) fixture through the non-deduping
            // nib.documents open(), so the reload leaves a duplicate tab - exactly as the shipped
            // NPPM_RELOADFILE does (a switch-if-already-open seam is Phase 3 work). Close every buffer
            // the section added, back to the pre-Phase-2 baseline, so the doc-count flow below is
            // unperturbed. All of them are clean (the fixture was only reloaded), so no close prompts.
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > p2BaseDocs && guard < 12; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == p2BaseDocs,
                  "p2 fixture(s) closed - doc model restored to the pre-Phase-2 baseline");
        }

        // ---- open: BEFOREOPEN precedes FILEOPENED with the same id ---------------------------------
        size_t mark = readLogLines().size();
        g_nibDocOpen(std::string(fileA.utf8_str()).c_str());   // DIRECT host call (the nib.documents seam)
        pump();
        const intptr_t idA = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idA != 0, "opened a.txt (active buffer id is non-zero)");
        L = readLogLines();
        {
            const int iBo = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREOPEN, idA));
            const int iOp = findFrom(L, mark, notifNeedleForPage(NPPN_FILEOPENED, idA));
            check(iBo >= 0 && iOp >= 0 && iBo < iOp,
                  "NPPN_FILEBEFOREOPEN precedes NPPN_FILEOPENED, same buffer id");
        }

        // ---- (a) save: BEFORESAVE precedes FILESAVED, same id, exactly once ------------------------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit1\n"));
        pump();
        mark = readLogLines().size();
        g_nibDocSave();                                        // DIRECT host call -> onSave -> writeFile
        pump();
        L = readLogLines();
        {
            const int iBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA));
            const int iSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA));
            check(iBs >= 0 && iSv >= 0 && iBs < iSv,
                  "(a) NPPN_FILEBEFORESAVE precedes NPPN_FILESAVED with the same buffer id");
            check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 1,
                  "(a) exactly ONE NPPN_FILESAVED per real save");
        }

        // ---- (b) undo-to-savepoint: savepoint reached, but NO false FILESAVED ----------------------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 3, reinterpret_cast<intptr_t>("zzz"));
        pump();
        mark = readLogLines().size();
        nibSciCall(nullptr, -1, SCI_UNDO, 0, 0);               // back to the save point
        pump();
        check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) == 0, "undo really returned to the save point");
        L = readLogLines();
        check(countFrom(L, mark, notifNeedle(SCN_SAVEPOINTREACHED, 0)) >= 1,
              "SCN_SAVEPOINTREACHED did fire (the undo-to-savepoint scenario really ran)");
        check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 0,
              "(b) NO false NPPN_FILESAVED after undo-to-savepoint");
        check(countFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA)) == 0,
              "(b) ...and no NPPN_FILEBEFORESAVE either (nothing was written)");

        // ---- (a, background id) Save All: per-buffer SAVING/SAVED pairs, each with its OWN id -------
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 4, reinterpret_cast<intptr_t>("yyy\n"));   // re-dirty A
        pump();
        g_nibDocOpen(std::string(fileB.utf8_str()).c_str());   // B becomes the active buffer; A goes background
        pump();
        const intptr_t idB = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idB != 0 && idB != idA, "opened b.txt as a second, distinct buffer");
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 4, reinterpret_cast<intptr_t>("www\n"));   // dirty B
        pump();
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileSaveall);                   // DIRECT dispatch through the wx command path
        pump();
        L = readLogLines();
        {
            const int aBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idA));
            const int aSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA));
            const int bBs = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORESAVE, idB));
            const int bSv = findFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idB));
            check(bBs >= 0 && bSv >= 0 && bBs < bSv,
                  "(a) Save All: active buffer's BEFORESAVE->FILESAVED pair, its own id");
            check(aBs >= 0 && aSv >= 0 && aBs < aSv,
                  "(a) Save All: BACKGROUND buffer's BEFORESAVE->FILESAVED pair carries ITS id, not the active one's");
            check(countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idA)) == 1
                      && countFrom(L, mark, notifNeedleForPage(NPPN_FILESAVED, idB)) == 1,
                  "(a) Save All: exactly one FILESAVED per written buffer");
        }
        check(readWholeFile(fileA).Contains("yyy") && readWholeFile(fileB).Contains("www"),
              "Save All really wrote both buffers to disk (byte truth, not just events)");

        // ---- close: FILEBEFORECLOSE precedes FILECLOSED, same id -----------------------------------
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileClose);                     // closes the active (clean) buffer B
        pump();
        L = readLogLines();
        {
            const int iBc = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORECLOSE, idB));
            const int iCl = findFrom(L, mark, notifNeedleForPage(NPPN_FILECLOSED, idB));
            check(iBc >= 0 && iCl >= 0 && iBc < iCl,
                  "NPPN_FILEBEFORECLOSE precedes NPPN_FILECLOSED with the same buffer id");
        }

        // ---- close, last-document path: the recycle is still a close for plugins -------------------
        // Closing the final document recycles its page into a fresh untitled buffer (never zero
        // documents) instead of deleting it - but real N++ fires FILEBEFORECLOSE/FILECLOSED there
        // too before re-using the tab, so the host fires DOCUMENT_CLOSED on that path as well.
        g_nibInvokeCommand(kCmdFileClose);                     // drain: closes the now-active doc (normal > 1 path)
        pump();
        const intptr_t idLast = g_nibDocActiveId ? g_nibDocActiveId() : 0;
        check(idLast != 0 && g_nibDocCount && g_nibDocCount() == 1,
              "drained to a single remaining document");
        mark = readLogLines().size();
        g_nibInvokeCommand(kCmdFileClose);                     // totalDocs() == 1 -> the buffer-recycle path
        pump();
        L = readLogLines();
        {
            const int iBc = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORECLOSE, idLast));
            const int iCl = findFrom(L, mark, notifNeedleForPage(NPPN_FILECLOSED, idLast));
            check(iBc >= 0 && iCl >= 0 && iBc < iCl,
                  "last-document close (buffer recycle) still fires FILEBEFORECLOSE -> FILECLOSED, same id");
        }
        check(g_nibDocCount && g_nibDocCount() == 1,
              "the recycled untitled buffer remains (never zero documents)");

        // ---- Phase 3: per-view buffer model (nib.documents v5) -------------------------------------
        // Set up 3 fixture files, migrate one to the SUB view (a real split), then assert both the HOST
        // hooks directly (per-view enumeration, background-buffer encoding/EOL peeks with the active
        // view's caret preserved, background save-by-id byte-truth) and the BRIDGE router (id/pos
        // packing incl. the view bit, GETNBOPENFILES filter, UniMode/EOL round-trips) via the probe log.
        {
            const wxString p3a = work + wxFILE_SEP_PATH + "p3a.txt";
            const wxString p3b = work + wxFILE_SEP_PATH + "p3b.txt";
            const wxString p3c = work + wxFILE_SEP_PATH + "p3c.txt";
            check(writeWholeFile(p3a, "aaa aaa\r\n") && writeWholeFile(p3b, "bbb bbb\r\n") && writeWholeFile(p3c, "ccc ccc\r\n"),
                  "Phase-3 fixtures created (CRLF line endings)");
            g_nibDocOpen(std::string(p3a.utf8_str()).c_str()); pump();
            g_nibDocOpen(std::string(p3b.utf8_str()).c_str()); pump();
            g_nibDocOpen(std::string(p3c.utf8_str()).c_str()); pump();   // p3c is active (newest)
            g_nibInvokeCommand(kCmdViewGotoAnotherView); pump(120);      // migrate p3c to the SUB view
            const int mainN = g_nibDocViewCount(0), subN = g_nibDocViewCount(1);
            check(subN >= 1 && mainN >= 1, "after migration both views hold >= 1 document (real split)");

            // resolve a buffer id by its path (layout-independent)
            auto idForPath = [&](const wxString& path) -> intptr_t {
                char buf[2048];
                for (int view = 0; view < 2; ++view)
                    for (int i = 0, n = g_nibDocViewCount(view); i < n; ++i) {
                        const intptr_t id = g_nibDocIdAt(view, i);
                        const int len = g_nibDocPathFromId(id, buf, sizeof(buf));
                        if (len > 0 && wxString::FromUTF8(buf, len) == path) return id;
                    }
                return 0;
            };

            // -- host hooks: id/index round-trip in both directions, both views ----------------------
            {
                const intptr_t idM0 = g_nibDocIdAt(0, 0);
                int v = -9, i = -9;
                check(idM0 != 0 && g_nibDocPosOf(idM0, 0, &v, &i) && v == 0 && i == 0,
                      "host pos_of(id_at(0,0)) round-trips to (view 0, index 0)");
                const intptr_t idS0 = g_nibDocIdAt(1, 0);
                int vs = -9, is = -9;
                check(idS0 != 0 && g_nibDocPosOf(idS0, 0, &vs, &is) && vs == 1 && is == 0,
                      "host pos_of(id_at(1,0)) round-trips to (view 1, index 0) - the sub view");
                check(g_nibDocIdAt(0, 999) == 0 && g_nibDocPosOf(0xdead, 0, &v, &i) == 0,
                      "host id_at out-of-range -> 0 and pos_of(unknown id) -> not found");
            }

            // -- host hooks: background-buffer encoding/EOL peeks with BOTH views' carets preserved ---
            // The active view is the SUB view (p3c). Peek a MAIN-view buffer that is NOT that view's
            // mounted selection, so its document is mounted on no view - a true doc-pointer-swap peek.
            g_nibDocActivateAt(1, 0); pump();   // ensure the sub view (p3c) is the active view
            const int mainSel = g_nibDocIndexOfActive(0);
            // Pick the background buffer BY PATH, not by index. It used to take main-view index 0 (or 1),
            // which lands on the boot-time untitled document - a buffer with no line breaks at all, whose
            // EOL mode is therefore whatever Scintilla's Document ctor defaults to. That default is
            // platform-split (Document.cxx: SC_EOL_CRLF under _WIN32, SC_EOL_LF elsewhere), and the host
            // deliberately leaves it alone for a file with no line breaks (main.cpp: "leave the default
            // mode for a file with no line breaks at all"). So the CRLF assertion below passed on Windows
            // by coincidence and failed on Linux/macOS. p3a/p3b are the known-CRLF fixtures this phase
            // created; whichever of them is not the main view's current selection is a genuine unmounted
            // background buffer AND is genuinely CRLF, which is what the assertion claims to test.
            const intptr_t mainSelId = g_nibDocIdAt(0, mainSel);
            const intptr_t idP3a = idForPath(p3a), idP3b = idForPath(p3b);
            const intptr_t bgId = (idP3a && idP3a != mainSelId) ? idP3a : idP3b;
            int bgView = -9, bgIndex = -9;
            check(bgId != 0 && bgId != mainSelId && g_nibDocPosOf(bgId, 0, &bgView, &bgIndex) && bgView == 0,
                  "picked an unmounted CRLF background buffer in the main view");
            nibSciCall(nullptr, -1, SCI_GOTOPOS, 4, 0);                 // known caret on the ACTIVE (sub) view
            coreSciCall(0, SCI_GOTOPOS, 2, 0);                          // known caret on the (inactive) MAIN view
            const long long subCaret  = coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0);
            const long long mainCaret = coreSciCall(0,  SCI_GETCURRENTPOS, 0, 0);
            const int eolBefore = g_nibDocEolGet(bgId);
            check(eolBefore == SC_EOL_CRLF, "host eol_get on a background buffer reads its CRLF mode (doc-swap peek)");
            check(g_nibDocEolSet(bgId, SC_EOL_LF) == 1 && g_nibDocEolGet(bgId) == SC_EOL_LF,
                  "host eol_set converts a background buffer's line endings, eol_get reads it back");
            check(g_nibDocEncodingGet(bgId) == ENC_UTF8, "host encoding_get on a background buffer -> UTF-8 (ASCII fixture)");
            check(g_nibDocEncodingSet(bgId, ENC_UTF8_BOM) == 1 && g_nibDocEncodingGet(bgId) == ENC_UTF8_BOM,
                  "host encoding_set records a background buffer's encoding (save-to-apply)");
            check(coreSciCall(-1, SCI_GETCURRENTPOS, 0, 0) == subCaret,
                  "the ACTIVE (sub) view's caret is unchanged after the background doc-pointer peeks");
            check(coreSciCall(0, SCI_GETCURRENTPOS, 0, 0) == mainCaret,
                  "the inactive MAIN view's caret is unchanged after the background doc-pointer peeks");

            // -- host hook: save a BACKGROUND buffer to disk, byte-compare -----------------------------
            const intptr_t idA = idForPath(p3a);
            check(idA != 0, "resolved p3a's buffer id by path");
            { int va, ia; check(g_nibDocPosOf(idA, 0, &va, &ia) && g_nibDocActivateAt(va, ia) == 1, "activated p3a"); }
            pump();
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 8, reinterpret_cast<intptr_t>("SAVEDBG\n"));   // edit p3a while active
            pump();
            g_nibDocActivateAt(1, 0); pump();   // back to the sub view -> p3a is a background buffer now
            check(g_nibDocSaveById(idA) == 1, "host save_by_id wrote the background buffer p3a");
            check(readWholeFile(p3a).Contains("SAVEDBG"),
                  "save_by_id really wrote p3a's edit to disk (byte truth, background buffer)");

            // -- host hook: rename_untitled rejects a pathful name, accepts a bare label ---------------
            {
                char pbuf[16];
                intptr_t idUntitled = 0;   // the empty-path (untitled) buffer - path_from_id returns 0 for it
                for (int view = 0; view < 2 && !idUntitled; ++view)
                    for (int i = 0, n = g_nibDocViewCount(view); i < n; ++i) {
                        const intptr_t id = g_nibDocIdAt(view, i);
                        if (g_nibDocPathFromId(id, pbuf, sizeof(pbuf)) == 0) { idUntitled = id; break; }
                    }
                check(idUntitled != 0, "found the untitled buffer (no on-disk path)");
                check(g_nibDocRenameUntitled(idUntitled, "has/slash") == 0, "rename_untitled rejects a path-like name");
                check(g_nibDocRenameUntitled(idUntitled, "Scratch") == 1, "rename_untitled accepts a bare tab label");
                check(g_nibDocRenameUntitled(idA, "nope") == 0, "rename_untitled rejects a buffer that has an on-disk path");
            }

            // -- host hook: tab_color_id (no colour -> -1) -------------------------------------------
            check(g_nibDocTabColorId(1, 0) == -1, "tab_color_id on an uncoloured tab -> -1");

            // -- BRIDGE router: fire the probe's Phase-3 table (needs the sub view active) -------------
            g_nibDocActivateAt(1, 0); pump();
            const size_t p3mark = readLogLines().size();
            g_nibInvokeCommand(cFirst + 1);     // the 2nd allocated cmd id -> probe messageProc -> runPhase3Table
            pump(150);
            std::vector<std::string> P = readLogLines();
            P.erase(P.begin(), P.begin() + std::min(p3mark, P.size()));

            check(p3val(P, "nb.primary") == g_nibDocViewCount(0) && p3val(P, "nb.second") == g_nibDocViewCount(1),
                  "GETNBOPENFILES(PRIMARY/SECOND) match the per-view counts");
            check(p3val(P, "nb.all") == p3val(P, "nb.primary") + p3val(P, "nb.second"),
                  "GETNBOPENFILES(ALL) == primary + second");
            check(p3val(P, "idx.sub") >= 0, "GETCURRENTDOCINDEX(SUB_VIEW) reports the sub view's active index");
            check(p3val(P, "pos.m0") == 0,
                  "GETPOSFROMBUFFERID(main index 0) packs (view 0, index 0) == 0");
            check(p3val(P, "pos.s0") == (static_cast<long long>(1) << 30),
                  "GETPOSFROMBUFFERID(sub index 0) sets the view bit: (1<<30)");
            check(p3val(P, "cur") != LLONG_MIN && p3val(P, "cur.back") == p3val(P, "cur"),
                  "GETBUFFERIDFROMPOS(unpack(GETPOSFROMBUFFERID(cur))) round-trips back to the same id");
            check(p3val(P, "enc.after") == 1,
                  "SETBUFFERENCODING(uniUTF8) then GETBUFFERENCODING == 1 (UniMode round-trip)");
            check(p3val(P, "eol.after") == SC_EOL_LF,
                  "SETBUFFERFORMAT(LF) then GETBUFFERFORMAT == LF (EOL round-trip)");
            check(p3val(P, "tabcolor") == -1, "GETTABCOLORID on an uncoloured current tab -> -1");
            check(p3val(P, "makedirty") == TRUE, "MAKECURRENTBUFFERDIRTY answered TRUE");
            check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) != 0, "...and the active buffer is really modified now");

            // clean up: close every Phase-3 buffer back to a single untitled document so the tail
            // assertions (the TBMODIFICATION-once tripwire) run against a settled model. Force-close via
            // the file-close command; dirty buffers are discarded (m_askBeforeClose is off in the sandbox).
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-3 buffers closed - back to a single document");
        }

        // ---- Phase 4: event fidelity (opt-in mask, real modified flags, LANGCHANGED, shortcut read) --
        // Arm the probe's opt-in (3rd allocated cmd id -> runPhase4Table -> NPPM_ADDSCNMODIFIEDFLAGS for
        // SC_PERFORMED_UNDO|SC_MOD_BEFOREDELETE), then drive insert (excluded flags -> silent) and undo
        // (included flags -> NPPN_GLOBALMODIFIED carrying them), a language switch (single NPPN_LANGCHANGED),
        // and check the effective-shortcut read hook both ways. The BEFORESHUTDOWN/CANCELSHUTDOWN/SHUTDOWN
        // ordering is exercised at the very end (the real close), with the SHUTDOWN-once check in main().
        {
            g_nibInvokeCommand(cFirst + 2);   // fire the probe's Phase-4 table (opt-in + a GETSHORTCUTBYCMDID probe)
            pump(120);
            check(g_nibModifiedMask == static_cast<unsigned>(SC_PERFORMED_UNDO | SC_MOD_BEFOREDELETE),
                  "NPPM_ADDSCNMODIFIEDFLAGS pushed the opted-in union mask into the host (perf gate armed)");
            {
                std::vector<std::string> P = readLogLines();
                check(p4val(P, "add") == TRUE, "NPPM_ADDSCNMODIFIEDFLAGS answered TRUE");
                check(p4val(P, "sc.unbound") == FALSE,
                      "NPPM_GETSHORTCUTBYCMDID(this plugin's unbound cmd) -> FALSE");
                check(p4val(P, "sc.unbound.key") == 0xEE,
                      "...and a FALSE return left the caller's ShortcutKey._key untouched (0xEE sentinel)");
                // NPPM_GETSHORTCUTBYCMDID on a PUNCTUATION-bound host command (View > Zoom In, "Ctrl+="):
                // catches wxKeyToVk regressing to raw ASCII ('=' == 0x3D) instead of the real Win32
                // VK_OEM_PLUS (0xBB) - the letter-only Ctrl+S case above can't exercise this path.
                check(p4val(P, "sc.zoomin") == TRUE,
                      "NPPM_GETSHORTCUTBYCMDID(View>Zoom In, Ctrl+=) -> TRUE (bound)");
                check(p4val(P, "sc.zoomin.ctrl") == 1,
                      "...ShortcutKey._isCtrl set");
                check(p4val(P, "sc.zoomin.key") == 0xBB,
                      "...ShortcutKey._key is the real VK_OEM_PLUS (0xBB), not raw ASCII '=' (0x3D)");
            }

            // a dedicated fixture so the undo history is exactly our single edit
            const wxString p4file = work + wxFILE_SEP_PATH + "p4.txt";
            check(writeWholeFile(p4file, "modme\n"), "Phase-4 fixture created");
            g_nibDocOpen(std::string(p4file.utf8_str()).c_str());
            pump();
            const intptr_t idP4 = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(idP4 != 0, "opened p4.txt as the active buffer");

            // (1) insert -> SC_MOD_INSERTTEXT is NOT in the opted-in mask -> no NPPN_GLOBALMODIFIED
            size_t m4 = readLogLines().size();
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 7, reinterpret_cast<intptr_t>("insert\n"));
            pump();
            check(countGm(readLogLines(), m4) == 0,
                  "no NPPN_GLOBALMODIFIED for an insert (opted-in flags exclude SC_MOD_INSERTTEXT) - mask gate works");

            // (2) undo -> SC_PERFORMED_UNDO (+ SC_MOD_BEFOREDELETE) ARE in the mask -> GLOBALMODIFIED carries them
            size_t m4b = readLogLines().size();
            nibSciCall(nullptr, -1, SCI_UNDO, 0, 0);
            pump();
            L = readLogLines();
            check(countGm(L, m4b) >= 1,
                  "undo fired NPPN_GLOBALMODIFIED (opted-in SC_PERFORMED_UNDO matched the mask)");
            const unsigned gmOr = gmOrFrom(L, m4b);
            check((gmOr & SC_PERFORMED_UNDO) && (gmOr & SC_MOD_BEFOREDELETE),
                  "NPPN_GLOBALMODIFIED carries the REAL opted-in modificationType flags (UNDO + BEFOREDELETE)");

            // (3) NPPN_LANGCHANGED: exactly once, carrying the changed buffer's id
            size_t m4c = readLogLines().size();
            check(g_nibDocSetLangById && g_nibDocSetLangById(idP4, kCmdLangC) == 1,
                  "forced language C on the active buffer (set_lang_by_id)");
            pump();
            check(countFrom(readLogLines(), m4c, notifNeedleForPage(NPPN_LANGCHANGED, idP4)) == 1,
                  "NPPN_LANGCHANGED fired exactly once with the changed buffer's id");

            // direct host hook: nib.keymap effective_shortcut, positive (Ctrl+S) and negative (unbound) paths
            {
                uint32_t mods = 0xEE, key = 0xEE;
                check(g_nibKmEffectiveShortcut && g_nibKmEffectiveShortcut(kCmdFileSave, &mods, &key) == 1
                          && (mods & 1) && !(mods & 2) && !(mods & 4) && key == static_cast<uint32_t>('S'),
                      "nib.keymap effective_shortcut(File>Save) -> Ctrl+S (Ctrl bit + key 'S')");
                mods = 0xEE; key = 0xEE;
                check(g_nibKmEffectiveShortcut(cFirst, &mods, &key) == 0 && mods == 0xEE && key == 0xEE,
                      "nib.keymap effective_shortcut(an unbound id) -> 0, out-params untouched");
            }

            // clean up back to a single document (the shutdown test dirties whatever remains)
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 12; ++guard) {
                g_nibInvokeCommand(kCmdFileClose);
                pump();
            }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-4 fixtures closed - back to a single document");
        }

        // ---- Phase 5: sessions + UI-chrome state + lexer registry ----------------------------------
        // (A) DIRECT host-hook session save->parse->load round-trip (deterministic), then (B) the BRIDGE
        // router via the probe's Phase-5 table (visibility toggle+read-back, line-number width mode,
        // CREATELEXER, GETNBUSERLANG, and a full session save/enumerate/load through the NPPM_* path).
        {
            // (A) save the currently-open saved files as a session, parse the N++ shape, load it back ----
            g_nibDocOpen(std::string(fileA.utf8_str()).c_str()); pump();   // one saved file in the session
            const wxString sessPath = g_sandboxRoot + wxFILE_SEP_PATH + "hostsave.session";
            const std::string sessPathU = std::string(sessPath.utf8_str());
            check(g_nibSessSaveCurrent && g_nibSessSaveCurrent(sessPathU.c_str()) == 1,
                  "nib.session save_current wrote a session of the open files");
            {
                wxXmlDocument doc;
                bool okLoad; { wxLogNull nl; okLoad = doc.Load(sessPath) && doc.GetRoot() != nullptr; }
                check(okLoad, "the written session file is well-formed XML");
                bool shapeOk = false, sawFileA = false;
                if (okLoad) {
                    check(doc.GetRoot()->GetName() == "NotepadPlus",
                          "session root is <NotepadPlus> (Notepad++-session-parseable, not just round-trippable)");
                    for (wxXmlNode* s = doc.GetRoot()->GetChildren(); s; s = s->GetNext()) {
                        if (s->GetName() != "Session") continue;
                        for (wxXmlNode* v = s->GetChildren(); v; v = v->GetNext()) {
                            if (v->GetName() != "mainView" && v->GetName() != "subView") continue;
                            for (wxXmlNode* f = v->GetChildren(); f; f = f->GetNext()) {
                                if (f->GetName() != "File") continue;
                                shapeOk = true;
                                if (f->GetAttribute("filename") == fileA) sawFileA = true;
                            }
                        }
                    }
                }
                check(shapeOk, "session has <Session>/<mainView|subView>/<File> nodes (the N++ schema shape)");
                check(sawFileA, "a saved <File> carries the open file's path in its filename attribute");
            }
            int valid = 0;
            check(g_nibSessFileCount && g_nibSessFileCount(sessPathU.c_str(), &valid) == 1 && valid == 1,
                  "nib.session file_count == 1 and reports a valid session XML");
            {
                char fbuf[2048] = {0};
                const int fl = g_nibSessFileAt ? g_nibSessFileAt(sessPathU.c_str(), 0, fbuf, (int)sizeof(fbuf)) : 0;
                check(fl > 0 && wxString::FromUTF8(fbuf, fl) == fileA,
                      "nib.session file_at(0) == the saved file path");
            }
            auto fileOpen = [&](const wxString& path) -> bool {
                char buf[2048];
                for (int view = 0; view < 2; ++view)
                    for (int i = 0, nn = g_nibDocViewCount(view); i < nn; ++i) {
                        const int len = g_nibDocPathFromId(g_nibDocIdAt(view, i), buf, (int)sizeof(buf));
                        if (len > 0 && wxString::FromUTF8(buf, len) == path) return true;
                    }
                return false;
            };
            // close everything (fileA included), then LOAD the session back -> fileA must re-open
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 12; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(!fileOpen(fileA), "drained the session's file before the load round-trip");
            check(g_nibSessLoad && g_nibSessLoad(sessPathU.c_str()) == 1,
                  "nib.session load parsed and opened the session");
            pump();
            check(fileOpen(fileA), "the session's file re-opened after load (save -> parse -> load round-trip)");

            // (B) the BRIDGE router: open a 2nd saved fixture so the bridge session save is non-empty ----
            g_nibDocOpen(std::string(fileB.utf8_str()).c_str()); pump();
            const size_t p5mark = readLogLines().size();
            g_nibInvokeCommand(cFirst + 3);   // the 4th allocated cmd id -> probe messageProc -> runPhase5Table
            pump(150);
            std::vector<std::string> P = readLogLines();
            P.erase(P.begin(), P.begin() + std::min(p5mark, P.size()));

            // visibility: each flag toggled and read back through its paired IS* message (via the router)
            check(p5val(P, "tb.hidden") == TRUE && p5val(P, "tb.shown") == FALSE,
                  "toolbar: HIDETOOLBAR(TRUE)->ISTOOLBARHIDDEN==TRUE, HIDETOOLBAR(FALSE)->FALSE");
            check(p5val(P, "sb.hidden") == TRUE && p5val(P, "sb.shown") == FALSE,
                  "statusbar: HIDESTATUSBAR/ISSTATUSBARHIDDEN toggles and reads back");
            check(p5val(P, "dl.shown") == TRUE && p5val(P, "dl.hidden") == FALSE,
                  "doc-list: SHOWDOCLIST/ISDOCLISTSHOWN toggles and reads back");
            check(p5val(P, "menu.hidden") == FALSE,
                  "menubar: HIDEMENU is a portable documented no-op (ISMENUHIDDEN stays FALSE) - no per-platform detach hack");
            check(p5val(P, "lnw.constant") == LINENUMWIDTH_CONSTANT && p5val(P, "lnw.dynamic") == LINENUMWIDTH_DYNAMIC,
                  "SETLINENUMBERWIDTHMODE / GETLINENUMBERWIDTHMODE round-trips (constant<->dynamic)");
            check(p5val(P, "autoindent") != LLONG_MIN, "ISAUTOINDENTON answered");
            check(p5val(P, "iconset") == (g_nibUiIconSet ? g_nibUiIconSet() : -1),
                  "GETTOOLBARICONSETCHOICE == the host's real icon-set choice");
            check(p5val(P, "lexer.cpp") == 1,
                  "CREATELEXER(\"cpp\") returns a non-null ILexer (Lexilla, cross-platform)");
            check(p5val(P, "lexer.applied") == 1,
                  "the created ILexer applies via SCI_SETILEXER without crashing");
            check(p5val(P, "lexer.empty") == 0, "CREATELEXER(\"\") returns NULL (empty/unknown lexer name)");
            check(p5val(P, "nbuserlang") == (g_nibLexerUserLangCount ? g_nibLexerUserLangCount() : -1),
                  "GETNBUSERLANG == the host's registered Scintillua-language count");
            check(p5val(P, "sess.save") == 1 && p5val(P, "sess.valid") == 1 && p5val(P, "sess.nb") >= 1,
                  "bridge SAVECURRENTSESSION wrote a valid session; GETNBSESSIONFILES counts + validates it");
            check(p5val(P, "sess.get") == TRUE && p5val(P, "sess.f0len") > 0,
                  "bridge GETSESSIONFILES filled the caller's array with non-empty paths");
            check(p5val(P, "sess.load") == 1, "bridge LOADSESSION opened the session's files");
            {
                wxXmlDocument doc; bool okLoad; { wxLogNull nl; okLoad = doc.Load(probeSessionPathStr()) && doc.GetRoot() != nullptr; }
                check(okLoad && doc.GetRoot()->GetName() == "NotepadPlus",
                      "the bridge-written session file parses as a <NotepadPlus> session (N++-parseable)");
            }
            // the toggles restored the frame; the host state getters agree with the frame members
            check(g_nibUiChromeGet && g_nibUiChromeGet(0) == 1 && g_nibUiChromeGet(2) == 1,
                  "toolbar + statusbar restored to shown after the toggle test");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-5 buffers closed - back to a single document");
        }

        // ---- Phase 6: long-tail file lifecycle + the -pluginMessage delivery -----------------------
        // Every Phase-6 notification is driven by a DIRECT host call (no modal dialog, no OS input
        // injection): the programmatic rename/delete seams (g_nibRenameActive/g_nibRecycleActive - the
        // dialog-free cores of renameFile()/recycleFile()), the Sort command (DOCORDERCHANGED), the
        // read-only toggle command (READONLYCHANGED), a fresh open (FILEBEFORELOAD), and
        // nibFireCmdlinePluginMsg (the -pluginMessage -> NPPN_CMDLINEPLUGINMSG path). Each asserts the
        // probe's beNotified log carries the right NPPN_* code + buffer id, and the before/after ordering.
        {
            const wxString p6a = work + wxFILE_SEP_PATH + "p6a.txt";
            const wxString p6b = work + wxFILE_SEP_PATH + "p6b.txt";
            check(writeWholeFile(p6a, "rename me\n") && writeWholeFile(p6b, "second\n"),
                  "Phase-6 fixtures created");

            // -- SNAPSHOTDIRTYFILELOADED: main() seeded a Recovery/seed1 manifest entry + .bak file into
            // the sandbox BEFORE wxEntry(), so WxnApp::OnInit()'s restoreSession() -> restoreRecoveryBackups()
            // already restored it (and fired the event) before runAll() ever started - search from index 0,
            // id-agnostic (the fresh recovered page's id was never captured by this harness).
            check(findFrom(readLogLines(), 0, notifNeedlePrefix(NPPN_SNAPSHOTDIRTYFILELOADED)) >= 0,
                  "NPPN_SNAPSHOTDIRTYFILELOADED fired for the seeded dirty-recovery backup restored at startup");

            // -- FILEBEFORELOAD (id 0) precedes FILEOPENED (the opened file's id) --
            mark = readLogLines().size();
            g_nibDocOpen(std::string(p6a.utf8_str()).c_str());
            pump();
            const intptr_t id6a = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(id6a != 0, "opened p6a.txt (active buffer id is non-zero)");
            L = readLogLines();
            {
                const int iBl = findFrom(L, mark, notifNeedle(NPPN_FILEBEFORELOAD, 0));
                const int iOp = findFrom(L, mark, notifNeedleForPage(NPPN_FILEOPENED, id6a));
                check(iBl >= 0 && iOp >= 0 && iBl < iOp,
                      "NPPN_FILEBEFORELOAD (id 0) precedes NPPN_FILEOPENED for the opened file");
            }

            // -- READONLYCHANGED carries the buffer id on a read-only toggle --
            mark = readLogLines().size();
            g_nibInvokeCommand(kCmdEditToggleReadOnly);   // -> toggleReadOnly()
            pump();
            check(findFrom(readLogLines(), mark, notifNeedleForPage(NPPN_READONLYCHANGED, id6a)) >= 0,
                  "NPPN_READONLYCHANGED fired with the buffer id when read-only was toggled");
            g_nibInvokeCommand(kCmdEditToggleReadOnly); pump();   // toggle back to writable

            // -- rename triple: BEFORE_RENAME precedes RENAMED, same id (the page id survives the rename) --
            const wxString p6aRenamed = work + wxFILE_SEP_PATH + "p6a-renamed.txt";
            mark = readLogLines().size();
            check(g_nibRenameActive && g_nibRenameActive(std::string(p6aRenamed.utf8_str()).c_str()) == 1,
                  "programmatic rename of the active file succeeded");
            pump();
            L = readLogLines();
            {
                const int iBr = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORERENAME, id6a));
                const int iRn = findFrom(L, mark, notifNeedleForPage(NPPN_FILERENAMED, id6a));
                check(iBr >= 0 && iRn >= 0 && iBr < iRn,
                      "NPPN_FILEBEFORERENAME precedes NPPN_FILERENAMED with the same buffer id");
            }
            check(wxFileExists(p6aRenamed) && !wxFileExists(p6a), "rename really moved the file on disk");

            // -- rename cancel: a rename into a nonexistent directory fails -> BEFORE_RENAME then RENAMECANCEL --
            // wxLogNull: the underlying wxRenameFile() logs this expected failure via wxLogSysError, which
            // a GUI app's default log target renders as a modal error dialog - exactly the kind of real,
            // un-auto-answered popup this headless harness must never leave sitting on screen (the
            // CloseDialogHook only answers the "wxNote" confirm-close prompt, not a generic wxLog dialog).
            const wxString badPath = work + wxFILE_SEP_PATH + "no_such_dir" + wxFILE_SEP_PATH + "x.txt";
            mark = readLogLines().size();
            { wxLogNull noLog; check(g_nibRenameActive(std::string(badPath.utf8_str()).c_str()) == 0,
                  "rename into a nonexistent directory fails"); }
            pump();
            L = readLogLines();
            {
                const int iBr = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFORERENAME, id6a));
                const int iRc = findFrom(L, mark, notifNeedleForPage(NPPN_FILERENAMECANCEL, id6a));
                check(iBr >= 0 && iRc >= 0 && iBr < iRc,
                      "a failed rename fires NPPN_FILEBEFORERENAME then NPPN_FILERENAMECANCEL, same id");
            }

            // -- DOCORDERCHANGED: open a 2nd file, Sort by name -> the tab order changes --
            g_nibDocOpen(std::string(p6b.utf8_str()).c_str()); pump();
            check(g_nibDocCount && g_nibDocCount() >= 2, "two documents open for the sort");
            const intptr_t idSortActive = g_nibDocActiveId ? g_nibDocActiveId() : 0;   // sortTabs carries the active page's id
            mark = readLogLines().size();
            g_nibInvokeCommand(kCmdWindowSortFnAsc); pump();   // -> sortTabs(Name, asc)
            check(findFrom(readLogLines(), mark, notifNeedleForPage(NPPN_DOCORDERCHANGED, idSortActive)) >= 0,
                  "NPPN_DOCORDERCHANGED fired when the tab order changed (Sort by name)");

            // -- delete triple: re-activate the renamed file, delete it -> BEFORE_DELETE precedes DELETED --
            { int dv = -1, di = -1;
              check(g_nibDocPosOf(id6a, 0, &dv, &di) && g_nibDocActivateAt(dv, di) == 1,
                    "re-activated the renamed file for the delete test"); }
            pump();
            check(g_nibDocActiveId && g_nibDocActiveId() == id6a, "the renamed file is the active buffer before delete");
            mark = readLogLines().size();
            check(g_nibRecycleActive && g_nibRecycleActive() == 1, "programmatic delete of the active file succeeded");
            pump();
            L = readLogLines();
            {
                const int iBd = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREDELETE, id6a));
                const int iDl = findFrom(L, mark, notifNeedleForPage(NPPN_FILEDELETED, id6a));
                check(iBd >= 0 && iDl >= 0 && iBd < iDl,
                      "NPPN_FILEBEFOREDELETE precedes NPPN_FILEDELETED with the same buffer id");
            }
            check(!wxFileExists(p6aRenamed), "delete really removed the file from disk");

            // -- delete failure: remove the file out from under its OPEN buffer, then attempt the same
            // programmatic delete again -> BEFORE_DELETE still fires (the buffer still has a path to try),
            // but the actual removal fails (already gone) -> DELETE_FAILED, not DELETED. Cross-platform:
            // POSIX remove() fails ENOENT; Windows SHFileOperationW likewise reports the file not found.
            const wxString p6c = work + wxFILE_SEP_PATH + "p6c.txt";
            check(writeWholeFile(p6c, "will vanish\n"), "p6c.txt fixture created");
            g_nibDocOpen(std::string(p6c.utf8_str()).c_str()); pump();
            const intptr_t id6c = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(id6c != 0, "opened p6c.txt (active buffer id is non-zero)");
            check(wxRemoveFile(p6c), "removed p6c.txt out from under its open buffer (simulates external deletion)");
            mark = readLogLines().size();
            // wxLogNull: on POSIX, recycleActive()'s fallback path calls wxRemoveFile() again on the
            // already-gone file, whose expected ENOENT failure wxRemoveFile() reports via wxLogSysError -
            // same un-auto-answered-dialog risk as the rename-cancel case above (Windows takes the
            // SHFileOperationW branch instead, which never logs, but the guard is harmless there too).
            { wxLogNull noLog; check(g_nibRecycleActive && g_nibRecycleActive() == 0,
                  "programmatic delete of an already-gone file reports failure"); }
            pump();
            L = readLogLines();
            {
                const int iBd = findFrom(L, mark, notifNeedleForPage(NPPN_FILEBEFOREDELETE, id6c));
                const int iDf = findFrom(L, mark, notifNeedleForPage(NPPN_FILEDELETEFAILED, id6c));
                check(iBd >= 0 && iDf >= 0 && iBd < iDf,
                      "NPPN_FILEBEFOREDELETE precedes NPPN_FILEDELETEFAILED, same id, when the delete fails");
            }
            check(g_nibDocActiveId && g_nibDocActiveId() == id6c,
                  "a failed delete leaves the buffer open (no close on failure)");

            // -- -pluginMessage: nibFireCmdlinePluginMsg delivers NPPN_CMDLINEPLUGINMSG (id 0) to the plugin --
            mark = readLogLines().size();
            nibFireCmdlinePluginMsg("compare:left=a.txt&right=b.txt");   // the host's CLI-delivery seam
            pump();
            check(findFrom(readLogLines(), mark, notifNeedle(NPPN_CMDLINEPLUGINMSG, 0)) >= 0,
                  "nibFireCmdlinePluginMsg delivered NPPN_CMDLINEPLUGINMSG to the plugin (-pluginMessage path)");

            // -- -pluginMessage over the "reuse an existing window" IPC handoff: the OTHER delivery path,
            // which used to drop the text silently (the payload never carried it, and OnExec never parsed
            // it). Drive the REAL wire format through WxnIpcConnection::OnExec - the same virtual call a
            // second wxnote process's Execute() lands on - end-to-end into g_ipcOpenRequest's
            // nibFireCmdlinePluginMsg call, not just the direct CLI seam exercised above.
            //
            // Heap-allocated and DELIBERATELY never deleted: wxDDEConnection's destructor (the Windows
            // backend wxConnection aliases to) unconditionally dereferences m_client when m_server is
            // null (build/_deps/wxwidgets-src/src/msw/dde.cpp ~line 511-512) - safe only for a connection
            // wx's own DDE handshake produced via OnAcceptConnection/MakeConnection (which sets one of
            // those), never for one constructed bare like this. OnExec() itself touches none of that
            // base-class connection state, so calling it directly is safe; only destructing it isn't.
            // A single leaked object in a one-shot test binary is harmless and keeps this portable (no
            // reach into wx's Windows-only DDE internals to work around it, no #ifdef _WIN32 special case).
            mark = readLogLines().size();
            {
                auto* conn = new WxnIpcConnection();
                check(conn->OnExec(wxEmptyString, "\x01PLUGINMSG=ipc-handoff-message\n"),
                      "WxnIpcConnection::OnExec accepted a PLUGINMSG= reuse-window payload");
            }
            pump();
            check(findFrom(readLogLines(), mark, notifNeedle(NPPN_CMDLINEPLUGINMSG, 0)) >= 0,
                  "-pluginMessage survives the reuse-window IPC handoff (OnExec -> g_ipcOpenRequest -> NPPN_CMDLINEPLUGINMSG)");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "Phase-6 buffers closed - back to a single document");
        }

        // ---- Phase 7: raw editor input (nib.events v5) ---------------------------------------------
        // SCN_CHARADDED / SCN_MARGINCLICK / SCN_DWELLSTART / SCN_HOTSPOTCLICK: the signals an
        // autocompletion, tag-closing, bracket-helper or hover-tooltip plugin is built on. Nothing in the
        // event set reported a keystroke or a margin hit before v5, which is why that whole class of
        // plugin was inert under the bridge.
        //
        // These are the only forwarded events with no direct host seam to drive: they originate inside
        // Scintilla's own key/mouse handling, which needs the real OS input this harness deliberately does
        // not do (see the file header). So each is driven by sending the wxEVT_STC_* event the control
        // itself would send, straight at the editor. Everything downstream of that is the real path -
        // onStcCharAdded / onStcMarginClick, nibFireEvent, the bridge's SCN_* translation, the probe's
        // beNotified. Only Scintilla's generation of the event is stood in for, and that is Scintilla's
        // behaviour, not ours. The payload assertions are the point: a wrong union member or a missed
        // field copy still produces a correctly-coded notification with a zeroed body.
        {
            // ';' is deliberately inert in onStcCharAdded: not a bracket (no auto-pair), not '(' (no call
            // tip), not '}' (no dedent) and not a word character (no autocompletion popup left open over
            // the tail assertions). What remains is the v5 fire.
            mark = readLogLines().size();
            { wxStyledTextEvent e(wxEVT_STC_CHARADDED); e.SetEventObject(g_view); e.SetKey(';'); g_view->ProcessWindowEvent(e); }
            pump();
            L = readLogLines();
            check(findFrom(L, mark, notifNeedlePrefix(SCN_CHARADDED)) >= 0,
                  "SCN_CHARADDED reached the probe (NIB_EV_CHAR_ADDED -> bridge -> beNotified)");
            check(findFrom(L, mark, "{\"k\":\"ca\",\"ch\":59}") >= 0,
                  "...carrying the character in scn.ch (';' == 59), not a zeroed payload");

            // Margin 3, NOT the bookmark margin 1: this pins that the host relays a margin it has no
            // behaviour of its own on, which is exactly the case a plugin owning its own margin needs.
            mark = readLogLines().size();
            { wxStyledTextEvent e(wxEVT_STC_MARGINCLICK); e.SetEventObject(g_view); e.SetPosition(0); e.SetMargin(3); e.SetModifiers(SCMOD_CTRL); g_view->ProcessWindowEvent(e); }
            pump();
            L = readLogLines();
            check(findFrom(L, mark, notifNeedlePrefix(SCN_MARGINCLICK)) >= 0, "SCN_MARGINCLICK reached the probe");
            check(findFrom(L, mark, "{\"k\":\"mc\",\"m\":3,\"mod\":2}") >= 0,
                  "...carrying the margin number and the SCMOD_CTRL modifier bits");

            // Dwell and hotspot share one bridge case each; assert the code arrives for one of each pair
            // (the payload copy is the same three lines the two asserted above already cover).
            mark = readLogLines().size();
            { wxStyledTextEvent e(wxEVT_STC_DWELLSTART);    e.SetEventObject(g_view); e.SetPosition(1); e.SetX(11); e.SetY(22); g_view->ProcessWindowEvent(e); }
            { wxStyledTextEvent e(wxEVT_STC_DWELLEND);      e.SetEventObject(g_view); e.SetPosition(1); e.SetX(11); e.SetY(22); g_view->ProcessWindowEvent(e); }
            { wxStyledTextEvent e(wxEVT_STC_HOTSPOT_CLICK); e.SetEventObject(g_view); e.SetPosition(1); e.SetModifiers(0); g_view->ProcessWindowEvent(e); }
            pump();
            L = readLogLines();
            check(findFrom(L, mark, notifNeedlePrefix(SCN_DWELLSTART))  >= 0, "SCN_DWELLSTART reached the probe");
            check(findFrom(L, mark, notifNeedlePrefix(SCN_DWELLEND))    >= 0, "SCN_DWELLEND reached the probe");
            check(findFrom(L, mark, notifNeedlePrefix(SCN_HOTSPOTCLICK))>= 0, "SCN_HOTSPOTCLICK reached the probe");
        }

        // ---- autocomplete windowed harvest: bounded per-keystroke cost, end-to-end ------------------
        // autoComplete used to copy + scan the WHOLE buffer on every word character typed, on every file
        // (300 ms/keystroke at 16 MiB - funclist_selftest --bench). Now it harvests at most a 1 MiB
        // caret-centered window on any size of file. Driven through the real typing path: the same
        // synthesized wxEVT_STC_CHARADDED the Phase-7 tests use -> onStcCharAdded -> autoComplete ->
        // (maybe) wxAutoCompShow, asserted via SCI_AUTOCACTIVE. The positive control comes FIRST: it
        // proves the popup machinery works in this harness at all, so every "no popup" assertion below
        // pins its specific mechanism rather than a popup that could never show.
        {
            auto typeTailWor = [&](const char* tail) {   // append <tail>, caret to end, type the final 'r'
                appendText(tail);
                nibSciCall(nullptr, -1, SCI_GOTOPOS, coreSciCall(-1, SCI_GETLENGTH, 0, 0), 0);
                wxStyledTextEvent e(wxEVT_STC_CHARADDED); e.SetEventObject(g_view); e.SetKey('r');
                g_view->ProcessWindowEvent(e);
                pump();
            };
            auto autocActive = [&]{ return coreSciCall(-1, SCI_AUTOCACTIVE, 0, 0); };

            // Positive control: a small document. "worry"/"worker" match the typed prefix "wor" and are
            // strictly longer, so the popup must show. (.txt: keyword-less, so these are document words.)
            const wxString acSmall = work + wxFILE_SEP_PATH + "ac_small.txt";
            check(writeWholeFile(acSmall, "worry worker\n"), "autocomplete fixtures created");
            openDoc(acSmall);
            typeTailWor("wor");
            check(autocActive() == 1,
                  "small file: typing 'wor' pops document-word completions (the harness CAN show the popup)");
            nibSciCall(nullptr, -1, SCI_AUTOCCANCEL, 0, 0);

            // Large-file page (one 60,000-char line trips the byLine threshold at load): the harvest is
            // NOT gated off - the 60 KB document fits the window whole, so "word" completes exactly as it
            // did in 0.13.0. Pins the review finding that a first draft's blanket largeFile gate killed
            // completion on small byLine-tripped files for zero cost saving.
            const wxString acLine = work + wxFILE_SEP_PATH + "ac_longline.txt";
            { std::string one; one.reserve(60006); for (int k = 0; k < 12000; ++k) one += "word "; one += "\n";
              check(writeWholeFile(acLine, one.c_str()), "long-line (byLine large-file) fixture created"); }
            openDoc(acLine);
            typeTailWor("\nwor");
            check(autocActive() == 1,
                  "byLine large-file page under the window cap: document words still complete (no blanket gate)");
            nibSciCall(nullptr, -1, SCI_AUTOCCANCEL, 0, 0);

            // The windowed band, end-to-end (docLen > 1 MiB): a word 2 MiB behind the caret is OUTSIDE
            // the harvest window and must not complete - the old whole-document scan would have found it.
            // Then words appended NEAR the caret do complete: the window harvests, the far word merely
            // fell outside it. hStart > 0 here, so this also executes the subrange-rangeText path.
            const wxString acWin = work + wxFILE_SEP_PATH + "ac_window.txt";
            { std::string doc = "worldpeace\n" + filler(3u << 20);
              check(writeWholeFile(acWin, doc.c_str()), "3 MiB windowed-band fixture created"); }
            openDoc(acWin);
            typeTailWor("\nwor");
            check(autocActive() == 0,
                  "windowed band: a matching word 2 MiB from the caret is outside the window - no popup");
            typeTailWor(" worry worked\nwor");
            check(autocActive() == 1,
                  "windowed band: matching words near the caret ARE harvested - the window works, it is just bounded");
            nibSciCall(nullptr, -1, SCI_AUTOCCANCEL, 0, 0);

            // Clip wiring at the call site: the window's head cut lands exactly on the 'w' of
            // "AAworcabcq", so the window-relative text STARTS with the plausible fragment "worcabcq...".
            // clipHead must drop that run; if the flags are dropped or swapped at the call site (they
            // default to false, so that compiles), the fragment completes and this fires. Placement math:
            // caret at EOF makes the window [docLen-1MiB, docLen]; with tail b = 1 MiB - 12 bytes and the
            // 4-byte "\nwor" append, docLen-1MiB lands on head+2 - the 'w' - independent of head size.
            const wxString acClip = work + wxFILE_SEP_PATH + "ac_clip.txt";
            { std::string doc = filler(4096) + "AAworcabcq" + "\n" + filler((1u << 20) - 12 - 1);
              check(writeWholeFile(acClip, doc.c_str()), "clip-trap fixture created"); }
            openDoc(acClip);
            typeTailWor("\nwor");
            check(autocActive() == 0,
                  "clip wiring: a word fragment created by the window's head cut is not offered as a completion");

            // The style-offset base: on a styled >1 MiB document the only matching words live in a
            // comment right before the caret, so the prose filter must drop them - but only if GetStyleAt
            // probes DOCUMENT positions (pos + hStart). Delete the '+ base' offset and the probes land
            // ~1 MiB early, on plain code bytes, and the comment words complete. (.cpp so Lexilla styles
            // and the filter is armed; no C++ keyword starts with "wor", so keywords cannot mask it.)
            const wxString acBase = work + wxFILE_SEP_PATH + "ac_base.cpp";
            { std::string doc; doc.reserve((1200u << 10) + 32);
              while (doc.size() < (1200u << 10)) doc += "int a=1;\n";
              doc += "// worqqq worqqzz\n";
              check(writeWholeFile(acBase, doc.c_str()), "style-offset fixture created"); }
            openDoc(acBase);
            typeTailWor("\nwor");
            check(autocActive() == 0,
                  "style-offset base: comment-only matches near the caret are filtered at their DOCUMENT positions");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "autocomplete fixtures closed - back to a single document");
        }

        // ---- crash-safety backup: skip-if-unchanged, direct UTF-8 bytes, atomic replace, cadence ----
        // Driven through g_backupTick - the frame-installed seam that runs one pass of the 30 s timer
        // body (allPages -> pageDirty -> peekDoc -> throttle -> backupUnsavedChanges) - because waiting
        // 30 s of wall clock per assertion is not a test. Detection idiom: DELETE the .bak, tick, and
        // see whether it comes back. A skipped snapshot leaves it deleted; a written one recreates it.
        {
            const wxString recDir = g_sandboxUserData + wxFILE_SEP_PATH + "RecoveryBackups";
            // The .bak for OUR page is whichever file in the recovery dir contains `marker` - ids are
            // session-serial and other tests' pages may have left snapshots, so find by content.
            auto bakContaining = [&](const char* marker) -> wxString {
                wxArrayString baks;
                wxDir::GetAllFiles(recDir, &baks, "*.bak", wxDIR_FILES);
                for (const wxString& b : baks) {
                    wxFile f(b); wxString all;
                    if (f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && all.Contains(marker)) return b;
                }
                return {};
            };
            auto tmpCount = [&]{
                wxArrayString tmps;
                return (int)wxDir::GetAllFiles(recDir, &tmps, "*.tmp", wxDIR_FILES);
            };

            // Multi-byte UTF-8 in the fixture: the old path round-tripped the document through wxString
            // (UTF-16) and back; the new one must write the document's own bytes, verbatim.
            const wxString bkFix = work + wxFILE_SEP_PATH + "bk.txt";
            check(writeWholeFile(bkFix, "z\xC3\xB3\xC5\x82w BKMARK1\n"), "backup fixture created (multi-byte UTF-8)");
            openDoc(bkFix);
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit1\n"));
            check(g_backupTick != nullptr, "the frame installed the backup-tick seam");
            g_backupTick(); pump();
            const wxString bak1 = bakContaining("BKMARK1");
            check(!bak1.empty(), "a dirty buffer's first tick writes its .bak");
            {
                wxFile f(bak1); wxString all;
                check(f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && all == wxString::FromUTF8("z\xC3\xB3\xC5\x82w BKMARK1\nedit1\n"),
                      "the .bak holds the document's exact bytes - multi-byte UTF-8 survives the direct write");
            }
            check(tmpCount() == 0, "no .tmp left behind - the atomic-rename path cleaned up");

            // Skip-if-unchanged: no edits since the snapshot, so the next tick must not rewrite. The
            // serials say 'already snapshotted', so a hand-deleted .bak stays deleted until an edit.
            check(removeFound(bak1), "(.bak removed to make a rewrite observable)");
            g_backupTick(); pump();
            check(bakContaining("BKMARK1").empty(),
                  "an unchanged dirty buffer is NOT re-snapshotted every tick (skip-if-unchanged)");

            // One more edit advances the serial: the next tick rewrites, and the content catches up.
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit2\n"));
            g_backupTick(); pump();
            const wxString bak2 = bakContaining("edit2");
            check(!bak2.empty(), "an edit re-arms the snapshot: the .bak is rewritten with the new content");

            // Cadence: a > 32 MiB buffer skips ticks that fall inside its stretched interval. The first
            // snapshot never waits (proved by its .bak appearing); the immediate second tick - even with
            // a NEW edit, so skip-if-unchanged cannot explain it - is throttled.
            const wxString bkBig = work + wxFILE_SEP_PATH + "bk_big.txt";
            { std::string big; big.reserve((33u << 20) + 32);
              while (big.size() < (33u << 20)) { big.append(63, 'q'); big += '\n'; }
              big += "BKMARK2\n";
              check(writeWholeFile(bkBig, big.c_str()), "33 MiB cadence fixture created"); }
            openDoc(bkBig);
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit3\n"));
            g_backupTick(); pump();
            const wxString bigBak = bakContaining("BKMARK2");
            check(!bigBak.empty(), "big buffer: the first snapshot is never rationed");
            nibSciCall(nullptr, -1, SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("edit4\n"));
            check(removeFound(bigBak), "(big .bak removed to make a rewrite observable)");
            g_backupTick(); pump();
            check(bakContaining("BKMARK2").empty(),
                  "big buffer: a tick inside the stretched interval is skipped even with fresh edits (cadence)");

            // clean up back to a single document for the tail assertions
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "backup fixtures closed - back to a single document");

            // ---- hardening round (adversarial review of the block above) ---------------------------

            // (a) Atomicity is real, not vacuous: a decoy planted at the .tmp path must be consumed by
            // the write-temp-then-rename cycle. A truncate-in-place revert never touches the .tmp, so
            // the decoy survives and tmpCount() catches it - the review showed the plain tmpCount()==0
            // assertion above passes vacuously under exactly that revert.
            const wxString bkK = work + wxFILE_SEP_PATH + "bk_atomic.txt";
            check(writeWholeFile(bkK, "BKMARK7\n"), "atomicity fixture created");
            openDoc(bkK);
            appendText("d0\n");
            g_backupTick(); pump();
            const wxString bakK = bakContaining("BKMARK7");
            check(!bakK.empty(), "atomicity fixture snapshotted");
            check(writeWholeFile(bakK + ".tmp", "decoy"), "(decoy planted at the .tmp path)");
            appendText("d1\n");
            g_backupTick(); pump();
            check(tmpCount() == 0, "the write really goes through .tmp: the planted decoy was consumed by the rename");
            { wxFile f(bakK); wxString all; check(f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && all.Contains("d1"),
                  "...and the .bak carries the new content"); }

            // (b) Failure must not stamp success: block the rename with a DIRECTORY squatting on the
            // .bak path. The failed tick must leave no .tmp (the cleanup leg) and record nothing, so
            // the next unblocked tick retries WITHOUT a new edit. Stamping before the rename lands
            // would serial-skip that retry forever - and the exit backup would inherit the lie.
            check(removeFound(bakK), "(.bak removed for the rename-blocker test)");
            check(wxFileName::Mkdir(bakK), "(directory blocker squatting on the .bak path)");
            appendText("d2\n");
            g_backupTick(); pump();
            check(tmpCount() == 0, "failed rename: the .tmp was cleaned up, not leaked");
            check(wxRmdir(bakK), "(blocker removed)");
            g_backupTick(); pump();   // NO new edit between the failed tick and this one
            { wxFile f(bakK); wxString all; check(f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && all.Contains("d2"),
                  "a failed snapshot is retried on the next tick - failure recorded no false success stamp"); }
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }

            // (c) The background EOL-convert hand-bump: the one event-masked mutation. Snapshot a CRLF
            // buffer, background it, convert to LF through the nib seam, and the next tick must rewrite
            // the .bak with LF content - deleting the hand-bump leaves the serials equal and the stale
            // CRLF snapshot in place forever.
            const wxString bkE = work + wxFILE_SEP_PATH + "bk_eol.txt";
            check(writeWholeFile(bkE, "BKMARK8 a\r\nb\r\n"), "EOL fixture created (CRLF)");
            openDoc(bkE);
            appendText("e1\n");
            const intptr_t idE = g_nibDocActiveId ? g_nibDocActiveId() : 0;
            check(idE != 0, "EOL fixture id resolved");
            const wxString bkF = work + wxFILE_SEP_PATH + "bk_eol_other.txt";
            check(writeWholeFile(bkF, "other\n"), "(second doc to background the EOL fixture)");
            openDoc(bkF);
            g_backupTick(); pump();
            const wxString bakE = bakContaining("BKMARK8");
            check(!bakE.empty(), "background CRLF buffer snapshotted");
            check(g_nibDocEolSet && g_nibDocEolSet(idE, SC_EOL_LF) == 1, "background EOL convert via the nib seam");
            check(removeFound(bakE), "(.bak removed to make the re-arm observable)");
            g_backupTick(); pump();
            { const wxString again = bakContaining("BKMARK8"); wxFile f(again); wxString all;
              check(!again.empty() && f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && !all.Contains("\r"),
                    "the masked background mutation re-armed the snapshot, and the .bak holds the converted LF bytes"); }
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }

            // (d) Split view: an edit arriving through the NON-active view's handle (nib.sci view 0 -
            // how npp-bridge plugins write) must credit the MAIN view's mounted page, not whichever
            // page has focus. Crediting the active page froze the mutated page's .bak at pre-plugin
            // content, through the exit backup too (review: HIGH).
            const wxString bkG = work + wxFILE_SEP_PATH + "bk_split.txt";
            const wxString bkH = work + wxFILE_SEP_PATH + "bk_split_other.txt";
            check(writeWholeFile(bkG, "BKMARK9\n") && writeWholeFile(bkH, "other\n"), "split fixtures created");
            openDoc(bkG);
            appendText("s1\n");                                   // bkG dirty in the MAIN view
            g_backupTick(); pump();
            const wxString bakG = bakContaining("BKMARK9");
            check(!bakG.empty(), "split fixture snapshotted before the split");
            openDoc(bkH);
            g_nibInvokeCommand(kCmdViewGotoAnotherView); pump(120);   // bkH -> SUB view; active = SUB; bkG mounted inactive in MAIN
            coreSciCall(0, SCI_APPENDTEXT, 3, reinterpret_cast<intptr_t>("s2\n"));   // plugin-style write to the INACTIVE main view
            pump();
            check(removeFound(bakG), "(.bak removed to make the re-arm observable)");
            g_backupTick(); pump();
            { const wxString again = bakContaining("BKMARK9"); wxFile f(again); wxString all;
              check(!again.empty() && f.IsOpened() && f.ReadAll(&all, wxConvUTF8) && all.Contains("s2"),
                    "an edit through the inactive view's handle re-arms THAT page's snapshot (per-pane serial attribution)"); }
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }

            // (e) Undo back to the savepoint retires the snapshot: the stale .bak used to survive a
            // clean exit and RESURRECT the deliberately-discarded edits at the next launch.
            const wxString bkI = work + wxFILE_SEP_PATH + "bk_undo.txt";
            check(writeWholeFile(bkI, "BKMARKA base\n"), "undo fixture created");
            openDoc(bkI);
            appendText("junk\n");
            g_backupTick(); pump();
            check(!bakContaining("BKMARKA").empty(), "undo fixture snapshotted while dirty");
            nibSciCall(nullptr, -1, SCI_UNDO, 0, 0); pump();
            check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) == 0, "undo really returned to the save point");
            check(bakContaining("BKMARKA").empty(),
                  "reaching the savepoint retired the .bak - discarded edits can no longer resurrect at next launch");
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }

            // (f) Rename refreshes recovery: the manifest stores Path/Title, and skip-if-unchanged used
            // to freeze the OLD path there forever (restore would then split the edits from their file).
            const wxString bkJ  = work + wxFILE_SEP_PATH + "bk_ren.txt";
            const wxString bkJ2 = work + wxFILE_SEP_PATH + "bk_ren2.txt";
            check(writeWholeFile(bkJ, "BKMARKB\n"), "rename fixture created");
            openDoc(bkJ);
            appendText("r1\n");
            g_backupTick(); pump();
            const wxString bakJ = bakContaining("BKMARKB");
            check(!bakJ.empty(), "rename fixture snapshotted");
            check(g_nibRenameActive && g_nibRenameActive(std::string(bkJ2.utf8_str()).c_str()) == 1, "fixture renamed on disk");
            check(removeFound(bakJ), "(.bak removed to make the manifest re-arm observable)");
            g_backupTick(); pump();   // NO edit since the rename
            check(!bakContaining("BKMARKB").empty(),
                  "a rename re-arms the snapshot so the recovery manifest catches the new path without waiting for an edit");
            for (int guard = 0; g_nibDocCount && g_nibDocCount() > 1 && guard < 20; ++guard) { g_nibInvokeCommand(kCmdFileClose); pump(); }
            check(g_nibDocCount && g_nibDocCount() == 1, "hardening fixtures closed - back to a single document");
        }

        // ---- (d) allocated command ids round-trip through the wx dispatcher ------------------------
        check(cFirst > 32767,
              "(d) allocated ids sit above 32767 (WM_COMMAND sign-wraps them; wrapped dispatch driven below)");
        for (int k = 0; k < 2; ++k)
        {
            // k=0 dispatches the TRUE id (what a wx-native menu/toolbar event carries). k=1 dispatches
            // the SIGN-WRAPPED value MSW's 16-bit WM_COMMAND path would sign-extend it to (64xxx wraps
            // negative): onCommand's & 0xFFFF mask must recover the real id, so the probe must still
            // log the TRUE id. A positive id alone cannot drive that recovery branch (x & 0xFFFF is
            // the identity below 65536), so this is what actually exercises the sign-wrap hazard.
            const int trueId = cFirst + k;
            const int sentId = (k == 0) ? trueId : trueId - 65536;
            char what[200], needle[64];
            mark = readLogLines().size();
            g_nibInvokeCommand(sentId);                        // wx-event dispatch -> alloc sinks -> bridge -> probe
            pump();
            L = readLogLines();
            std::snprintf(needle, sizeof(needle), "{\"k\":\"cmd\",\"id\":%d}", trueId);
            std::snprintf(what, sizeof(what),
                          "(d) allocated cmd id %d (dispatched as %d) reaches the probe's messageProc via the wx dispatcher",
                          trueId, sentId);
            check(findFrom(L, mark, needle) >= 0, what);
        }

        // ---- NPPN_TBMODIFICATION fired exactly once across the whole session -----------------------
        // Today once-per-boot holds by construction (both plugin loaders run inside the bridge's
        // activate() BEFORE its single notifyNpp(NPPN_TBMODIFICATION)); this assertion pins it as
        // CONTRACT. If a runtime/late plugin-load feature ever lands, it must emit TBMODIFICATION
        // once per late-loaded plugin (so that plugin's toolbar-init path runs) - this tripwire is
        // what forces that design decision instead of letting late loaders silently never hear it.
        L = readLogLines();
        check(countFrom(L, 0, notifNeedle(NPPN_TBMODIFICATION, 0)) == 1,
              "NPPN_TBMODIFICATION delivered exactly once for this plugin-load pass");

        // ---- Phase 4: BEFORESHUTDOWN / CANCELSHUTDOWN on a vetoed close, then the real close --------
        // Dirty the remaining buffer so confirmClose reaches the (headlessly auto-answered) save prompt.
        nibSciCall(nullptr, -1, SCI_APPENDTEXT, 10, reinterpret_cast<intptr_t>("dirtyexit\n"));
        pump();
        check(coreSciCall(-1, SCI_GETMODIFY, 0, 0) != 0, "active buffer dirtied for the shutdown-veto test");

        // (1) a VETOED close: the hook answers Cancel -> confirmClose returns false. Expect the
        //     BEFORESHUTDOWN -> CANCELSHUTDOWN pair and NO SHUTDOWN; the frame survives.
        g_closeAnswer = wxID_CANCEL;
        mark = readLogLines().size();
        if (wxWindow* top = GetTopWindow()) top->Close(false);   // CanVeto() -> the veto path runs
        pump(80);
        L = readLogLines();
        {
            const int iBefore = findFrom(L, mark, notifNeedle(NPPN_BEFORESHUTDOWN, 0));
            const int iCancel = findFrom(L, mark, notifNeedle(NPPN_CANCELSHUTDOWN, 0));
            check(iBefore >= 0, "NPPN_BEFORESHUTDOWN fired on the close attempt");
            check(iCancel >= 0 && iCancel > iBefore,
                  "NPPN_CANCELSHUTDOWN fired after BEFORESHUTDOWN (the cancelled shutdown)");
            check(countFrom(L, mark, notifNeedle(NPPN_SHUTDOWN, 0)) == 0,
                  "NO NPPN_SHUTDOWN on a vetoed close (the app stays up)");
        }
        check(GetTopWindow() != nullptr, "the frame survived the vetoed close");

        std::printf("  ..  runAll checks done; driving the real shutdown (SHUTDOWN-once verified in main)\n");
        std::fflush(stdout);

        // (2) the REAL close: forced (CanVeto()==false) skips the prompt loop entirely -> teardown ->
        //     CallAfter-deferred unloadNibPlugins -> deactivate -> the single NPPN_SHUTDOWN. That fires
        //     AFTER this function returns, so main() (post-wxEntry) makes the "exactly once" assertion.
        g_closeAnswer = wxID_NO;   // disarm the veto (a forced close does not consult it anyway)
        if (wxWindow* top = GetTopWindow()) top->Close(true);
    }
};

wxIMPLEMENT_APP_NO_MAIN(BridgeSelfTestApp);

int main(int argc, char** argv)
{
    // The host's plugin-facing command ids DELIBERATELY sit above 32767 (NIB_CMD_BASE 63000+, the
    // nib.alloc pool 64000+ - recovered from the 16-bit WM_COMMAND path by onCommand's & 0xFFFF
    // mask), which trips wx's advisory wxMenuItemBase "invalid itemid value" assert on every
    // Extensions-menu append in an assert-enabled wx build (wxDEBUG_LEVEL defaults to 1 even in
    // release). The shipped GUI app swallows exactly that one assertion via WxnApp::OnAssertFailure
    // (see main.cpp) so it never reaches a user's screen. Here we reach for the blunter
    // wxDisableAsserts() instead: this is a non-interactive console harness, so ANY wx assertion -
    // not just the itemid one - must be prevented from popping wx's modal debug MessageBox (which,
    // with no one to click it, would hang CI) or abort()ing the process mid-run.
    wxDisableAsserts();
    // ...and for the same reason, silence LOGGING for the whole run. wxLogError/wxLogSysError pop a
    // modal wxMessageBox, which in a non-interactive harness nobody can dismiss: the suite then hangs
    // forever instead of failing. That is not hypothetical - a single wxRemoveFile("") in a fixture
    // (an assertion helper handed an empty path by a lookup that found nothing) turned a one-line FAIL
    // into a wedged CI run. A failed assertion must stay a printed FAIL, never a dialog.
    wxLog::EnableLogging(false);

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec);
    if (ec) root = fs::path(".");
    root /= "wxnote_bridge_selftest";
    fs::remove_all(root, ec);                       // hermetic: every run starts from a blank sandbox
    fs::create_directories(root / "userdata", ec);
    g_sandboxRoot     = wxString::FromUTF8(root.u8string().c_str());
    g_sandboxUserData = wxString::FromUTF8((root / "userdata").u8string().c_str());

#ifdef _WIN32
    // Drop a stale probe log (the pid-stamp check would catch it anyway; this keeps runs tidy).
    wchar_t exe[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::remove(fs::path(exe).parent_path() / "plugins" / "Config" / "wxn_probe.jsonl", ec);
#else
    // POSIX: the bridge scans <user-data>/plugins/<Name>/<Name>.so - stage the probe there from the
    // build tree (bin/nib/example/example_plugin.so, the recompiled-plugin CI artifact).
    fs::path exeDir = fs::absolute(fs::path(argv[0]), ec).parent_path();
    const char* soName =
#ifdef __APPLE__
        "example_plugin.dylib";
#else
        "example_plugin.so";
#endif
    fs::path probeSrc = exeDir / "nib" / "example" / soName;
    fs::path probeDir = root / "userdata" / "plugins" / "example_plugin";
    fs::create_directories(probeDir, ec);
    // Report staging failures LOUDLY. These calls used to swallow their error_code, so when the build
    // stopped producing example_plugin.so (a generator expression baked verbatim into the target's
    // SUFFIX - see packages/npp-bridge/example/CMakeLists.txt) the copy silently did nothing and the
    // suite failed 117 assertions deep with no hint of the cause. Every POSIX run now prints the exact
    // paths, so the next staging break is one line of log instead of a bisect.
    ec.clear();
    fs::copy_file(probeSrc, probeDir / soName, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        std::fprintf(stderr,
                     "bridge_selftest: FATAL - could not stage the probe plugin.\n"
                     "  from: %s\n  to:   %s\n  why:  %s\n"
                     "  (the probe is what every notification assertion reads; without it the suite is meaningless)\n",
                     probeSrc.string().c_str(), (probeDir / soName).string().c_str(), ec.message().c_str());
        std::error_code lec;
        if (!fs::exists(probeSrc, lec))
        {
            std::fprintf(stderr, "  the source does not exist. Contents of %s:\n", probeSrc.parent_path().string().c_str());
            lec.clear();
            for (fs::directory_iterator di(probeSrc.parent_path(), lec), de; !lec && di != de; di.increment(lec))
                std::fprintf(stderr, "    %s\n", di->path().filename().string().c_str());
        }
        return 2;   // fail fast: a cascade of 117 downstream failures hides this one real cause
    }
    std::fprintf(stderr, "bridge_selftest: staged probe -> %s\n", (probeDir / soName).string().c_str());
#endif

    const int rc = wxEntry(argc, argv);

    // NPPN_SHUTDOWN is delivered from the CallAfter-deferred unloadNibPlugins() - i.e. AFTER runAll()
    // returned and the app finished exiting - so it can only be asserted here, once wxEntry has come back.
    {
        const std::vector<std::string> L = readLogLines();
        const std::string needle = notifNeedle(NPPN_SHUTDOWN, 0);
        int n = 0;
        for (const auto& s : L) if (s.find(needle) != std::string::npos) ++n;
        check(n == 1, "NPPN_SHUTDOWN delivered exactly once, at real exit (CallAfter-deferred unload path)");
    }
    // The nib.documents v5 hooks (main.cpp:2608-2721) must be nulled by onCloseWindow's pre-CallAfter
    // teardown, same as their nib.ui/session/lexer/keymap siblings - otherwise a plugin's NPPN_SHUTDOWN
    // handler (delivered above, after the frame is destroyed) reaches a dangling `[this]` capture. These
    // are the same static globals main.cpp defines (this TU #includes it), so a direct read after wxEntry
    // has returned - frame long gone - both proves the null-out AND cannot itself use-after-free.
    check(!g_nibDocViewCount && !g_nibDocIdAt && !g_nibDocPosOf && !g_nibDocIndexOfActive &&
          !g_nibDocActivateAt && !g_nibDocSetLangById && !g_nibDocEncodingGet && !g_nibDocEncodingSet &&
          !g_nibDocEolGet && !g_nibDocEolSet && !g_nibDocSaveActiveAs && !g_nibDocSaveById &&
          !g_nibDocSetDirtyActive && !g_nibDocRenameUntitled && !g_nibDocTabColorId,
          "nib.documents v5 host hooks are all nulled after real shutdown (no dangling `this` capture survives)");
    std::printf(g_failCount ? "\nFAILED  (%d passed, %d failed)\n"
                            : "\nPASSED  (%d passed, %d failed)\n", g_pass, g_failCount);
    std::fflush(stdout);
    return rc != 0 ? rc : (g_failCount ? 1 : 0);
}
