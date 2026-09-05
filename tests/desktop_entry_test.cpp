// SPDX-License-Identifier: Apache-2.0
//
// desktop_entry_test - the .desktop rewriting behind AppImage self-integration (src/desktop_entry.h),
// driven against plain strings with no filesystem and no Linux.
//
// An AppImage installs nothing, so the 84 MimeType entries wxNote declares reach the desktop only if
// the running AppImage writes its own .desktop into ~/.local/share/applications. Two things there go
// wrong quietly rather than loudly, and both are pinned here:
//
//   * Quoting. An AppImage lives wherever the user dropped it - "~/My Apps/wxNote.AppImage" is
//     ordinary. An Exec value that does not quote that path is not an error: the desktop parses it as
//     two arguments and the launcher simply never works.
//   * The MIME list. It is single-sourced in installer/linux/wxnote.desktop precisely so it cannot
//     drift, which only holds if rewriting copies it through untouched. The last test here integrates
//     the REAL shipped file and requires its MimeType line to survive byte for byte.
//
// Same shape as comment_tokens_test: free functions over sample strings, linking nothing.
//
//   cmake --build build --target desktop_entry_test && build/bin/desktop_entry_test
//
#include "desktop_entry.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

static void expectEq(const std::string& got, const std::string& want, const std::string& what)
{
    const bool ok = (got == want);
    check(ok, what);
    if (!ok) std::printf("        want [%s]  got [%s]\n", want.c_str(), got.c_str());
}

static bool contains(const std::string& hay, const std::string& needle)
{ return hay.find(needle) != std::string::npos; }

// The bundled entry, trimmed to the parts that matter here.
static const char* kBundled =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=wxNote\n"
    "Exec=wxnote %F\n"
    "Icon=wxnote\n"
    "Terminal=false\n"
    "Categories=Utility;TextEditor;Development;\n"
    "# a comment that must survive\n"
    "MimeType=text/plain;text/x-c++src;application/json;\n";

int main()
{
    std::printf("desktop_entry_test\n");

    // ---- quoting ---------------------------------------------------------------------------------
    std::printf("\nExec quoting\n");
    expectEq(wxnDesktopQuote("/opt/wxNote.AppImage"), "\"/opt/wxNote.AppImage\"", "plain path is quoted");
    expectEq(wxnDesktopQuote("/home/u/My Apps/wxNote.AppImage"),
             "\"/home/u/My Apps/wxNote.AppImage\"", "a path with a space stays one argument");
    // The four characters the spec requires to be backslash-escaped inside a quoted argument. A path
    // containing $ is not exotic - "$HOME" appears literally in plenty of directory names.
    expectEq(wxnDesktopQuote("/a/$b"),   "\"/a/\\$b\"",   "$ is escaped");
    expectEq(wxnDesktopQuote("/a/\"b"),  "\"/a/\\\"b\"",  "double quote is escaped");
    expectEq(wxnDesktopQuote("/a/\\b"),  "\"/a/\\\\b\"",  "backslash is escaped");
    expectEq(wxnDesktopQuote("/a/`b"),   "\"/a/\\`b\"",   "backtick is escaped");

    // ---- splitting a program from its field codes -------------------------------------------------
    std::printf("\nExec argument tail\n");
    expectEq(wxnDesktopExecArgs("wxnote %F"), "%F", "bare program, one field code");
    expectEq(wxnDesktopExecArgs("wxnote"), "", "bare program, no arguments");
    expectEq(wxnDesktopExecArgs("/usr/bin/wxnote -n %U"), "-n %U", "fixed argument is kept with the field code");
    // Re-parsing our OWN output is not hypothetical: every launch re-integrates to catch a moved
    // AppImage, so this exact string is fed back in. Splitting on the first space would return
    // `Apps/wxNote.AppImage" %F` and corrupt the entry a little more on each run.
    expectEq(wxnDesktopExecArgs("\"/home/u/My Apps/wxNote.AppImage\" %F"), "%F",
             "quoted program with a space is one token");
    expectEq(wxnDesktopExecArgs("  wxnote   %F"), "%F", "leading and inner whitespace tolerated");

    // ---- reading a key ----------------------------------------------------------------------------
    std::printf("\nkey lookup\n");
    expectEq(wxnDesktopValue(kBundled, "Exec"), "wxnote %F", "Exec is read");
    expectEq(wxnDesktopValue(kBundled, "Type"), "Application", "Type is read");
    expectEq(wxnDesktopValue(kBundled, "TryExec"), "", "absent key returns empty");
    // A key of the same name in a later group must not answer for [Desktop Entry] - that is how the
    // moved-AppImage check would end up comparing against an action's path instead of the launcher's.
    const std::string twoGroups =
        "[Desktop Entry]\nExec=a %F\n\n[Desktop Action new]\nExec=b %F\n";
    expectEq(wxnDesktopValue(twoGroups, "Exec"), "a %F", "only the [Desktop Entry] group answers");

    // ---- the rewrite ------------------------------------------------------------------------------
    std::printf("\nintegration\n");
    const std::string out = wxnIntegrateDesktopEntry(kBundled, "/home/u/Apps/wxNote.AppImage",
                                                     "/home/u/.local/share/icons/wxnote.svg");
    expectEq(wxnDesktopValue(out, "Exec"), "\"/home/u/Apps/wxNote.AppImage\" %F",
             "Exec points at the AppImage and keeps %F");
    expectEq(wxnDesktopValue(out, "TryExec"), "/home/u/Apps/wxNote.AppImage",
             "TryExec is added when the bundled file has none");
    expectEq(wxnDesktopValue(out, "Icon"), "/home/u/.local/share/icons/wxnote.svg",
             "Icon becomes an absolute path");
    // The whole reason integration rewrites rather than generates.
    expectEq(wxnDesktopValue(out, "MimeType"), "text/plain;text/x-c++src;application/json;",
             "MimeType survives byte for byte");
    expectEq(wxnDesktopValue(out, "Categories"), "Utility;TextEditor;Development;", "Categories survive");
    check(contains(out, "# a comment that must survive"), "comments survive");
    check(contains(out, "[Desktop Entry]"), "the group header survives");

    // Every launch re-integrates, so a second pass must be a no-op. Without the quoted-program handling
    // in wxnDesktopExecArgs this is the test that fails, and it fails by slow corruption rather than
    // by error - exactly the kind of bug that reaches users.
    const std::string again = wxnIntegrateDesktopEntry(out, "/home/u/Apps/wxNote.AppImage",
                                                       "/home/u/.local/share/icons/wxnote.svg");
    expectEq(again, out, "integration is idempotent");

    // A path with a space, round-tripped: quote it, write it, read it back, and recover the original.
    const std::string spaced = wxnIntegrateDesktopEntry(kBundled, "/home/u/My Apps/wx Note.AppImage", "/i.svg");
    expectEq(wxnDesktopValue(spaced, "Exec"), "\"/home/u/My Apps/wx Note.AppImage\" %F",
             "spaced AppImage path stays one Exec argument");
    expectEq(wxnDesktopValue(spaced, "TryExec"), "/home/u/My Apps/wx Note.AppImage",
             "TryExec carries the raw path, unquoted");
    expectEq(wxnIntegrateDesktopEntry(spaced, "/home/u/My Apps/wx Note.AppImage", "/i.svg"), spaced,
             "idempotent for a spaced path too");

    // An entry that already carries TryExec gets it REPLACED, not duplicated - a second TryExec line
    // would leave the desktop reading whichever it saw first, which may be the stale one.
    const std::string withTry =
        "[Desktop Entry]\nType=Application\nExec=old %F\nTryExec=/old/path\nIcon=x\n";
    const std::string fixed = wxnIntegrateDesktopEntry(withTry, "/new/wxNote.AppImage", "/new/i.svg");
    expectEq(wxnDesktopValue(fixed, "TryExec"), "/new/wxNote.AppImage", "existing TryExec is replaced");
    size_t tryCount = 0;
    for (size_t p = fixed.find("TryExec="); p != std::string::npos; p = fixed.find("TryExec=", p + 1)) ++tryCount;
    check(tryCount == 1, "exactly one TryExec line");

    // A [Desktop Action] group is left alone (see the note in desktop_entry.h).
    const std::string acted = wxnIntegrateDesktopEntry(twoGroups, "/new/x.AppImage", "/i.svg");
    check(contains(acted, "Exec=b %F"), "a later group's Exec is not rewritten");

    // ---- the real shipped entry --------------------------------------------------------------------
    // Read as DATA, like comment_tokens_test reads the language table: this is the file the AppImage
    // actually carries, and the test that would catch someone regenerating it instead of rewriting it.
    std::printf("\nthe shipped installer/linux/wxnote.desktop\n");
    std::ifstream f(DESKTOP_FILE);
    if (!f)
    {
        check(false, "could open " DESKTOP_FILE);
    }
    else
    {
        std::ostringstream ss; ss << f.rdbuf();
        const std::string real = ss.str();
        const std::string mime = wxnDesktopValue(real, "MimeType");
        check(!mime.empty(), "the shipped entry declares MimeType");
        // Whatever the list currently is, integration must not touch it.
        const std::string realOut = wxnIntegrateDesktopEntry(real, "/opt/wxNote.AppImage", "/opt/i.svg");
        expectEq(wxnDesktopValue(realOut, "MimeType"), mime, "the shipped MIME list is preserved exactly");
        // text/plain is the one type the entry has always declared; if it ever vanishes the editor has
        // stopped being offered for the most ordinary file there is.
        check(contains(mime, "text/plain;"), "text/plain is still declared");
        expectEq(wxnDesktopValue(realOut, "Exec"), "\"/opt/wxNote.AppImage\" %F",
                 "the shipped entry's %F is preserved through integration");
        expectEq(wxnIntegrateDesktopEntry(realOut, "/opt/wxNote.AppImage", "/opt/i.svg"), realOut,
                 "idempotent on the shipped entry");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
