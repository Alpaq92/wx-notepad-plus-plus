// SPDX-License-Identifier: Apache-2.0
//
// cmdline_compat_test - Notepad++'s launch options rewritten into wxNote's (src/cmdline_compat.h), and
// the language a -l names, driven with plain strings.
//
// What goes wrong here goes wrong quietly: a Notepad++ shortcut with -nosession that wxCmdLineParser
// reads as -n -o -s..., or a -lcpp that picks no language, still starts the editor - just not the way
// the shortcut asked. So each spelling is pinned to what wxNote's own parser then receives.
//
//   cmake --build build --target cmdline_compat_test && build/bin/cmdline_compat_test
//
#include "cmdline_compat.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    ok ? ++g_pass : ++g_fail;
}

static std::string join(const std::vector<std::string>& v)
{
    std::string o;
    for (size_t i = 1; i < v.size(); ++i) o += (i > 1 ? " " : "") + v[i];
    return o;
}

// argv after the program name, space-separated in and out.
static void expectArgs(const std::vector<std::string>& in, const std::string& want, const std::string& what)
{
    std::vector<std::string> a{ "wxnote" };
    a.insert(a.end(), in.begin(), in.end());
    const std::string got = join(wxncli::normalizeArgs(a));
    check(got == want, what);
    if (got != want) std::printf("        want [%s]  got [%s]\n", want.c_str(), got.c_str());
}

static void expectLang(const std::string& in, const std::string& want)
{
    const std::string got = wxncli::languageFor(in);
    check(got == want, "-l" + in + " -> " + (want.empty() ? "(none)" : want));
    if (got != want) std::printf("        want [%s]  got [%s]\n", want.c_str(), got.c_str());
}

int main()
{
    std::printf("wxNote's own options pass through\n");
    expectArgs({ "-g", "12,3", "-n", "-r", "a.txt" }, "-g 12,3 -n -r a.txt", "short options untouched");
    expectArgs({ "--goto=4", "--no-session", "-R", "b.txt" }, "--goto=4 --no-session -R b.txt", "long options untouched");
    expectArgs({ "-l", "Python", "x.py" }, "-l Python x.py", "-l with a separate value untouched");
    expectArgs({ "-q", "new.txt" }, "-q new.txt", "-q (create) is not a Notepad++ -q... option");
    expectArgs({ "-" }, "-", "stdin's lone dash untouched");

    std::printf("\nNotepad++ spellings\n");
    expectArgs({ "-multiInst", "a.txt" }, "-n a.txt", "-multiInst is -n");
    expectArgs({ "-nosession" }, "--no-session", "-nosession is --no-session");
    expectArgs({ "-ro", "a.txt" }, "-R a.txt", "-ro is -R");
    expectArgs({ "-noPlugin" }, "--safe", "-noPlugin is --safe");
    expectArgs({ "-pluginMessage=hi there" }, "--pluginMessage=hi there", "-pluginMessage= is the long option");
    expectArgs({ "-pluginMessage", "hi" }, "--pluginMessage hi", "-pluginMessage with a separate value");
    expectArgs({ "-ns", "a.txt", "-rs" }, "--ns a.txt --rs", "Notepad4's -ns / -rs are the long options");
    expectArgs({ "-openSession", "s.yaml" }, "--session s.yaml", "-openSession takes the next argument as its file");
    expectArgs({ "-lpython", "x.py" }, "--language=python x.py", "-l glued to its language");
    expectArgs({ "-n42", "a.txt" }, "a.txt --goto=42", "-n42 is line 42");
    expectArgs({ "-n42", "-c7", "a.txt" }, "a.txt --goto=42,7", "-n42 -c7 is line 42, column 7");
    expectArgs({ "-c7", "a.txt" }, "a.txt", "a column alone goes nowhere");
    expectArgs({ "-g", "5", "-n42" }, "-g 5", "wxNote's own -g wins over -n42");

    std::printf("\nNotepad++ options wxNote has no use for are dropped, not refused\n");
    expectArgs({ "-notabbar", "-systemtray", "-loadingTime", "-alwaysOnTop", "a.txt" }, "a.txt", "window options");
    expectArgs({ "-x100", "-y200", "-p300", "a.txt" }, "a.txt", "position options");
    expectArgs({ "-LfR", "-titleAdd=dev", "-settingsDir=C:\\s", "-udl=Mine", "a.txt" }, "a.txt", "valued options");
    expectArgs({ "-qnHello", "-qtText", "-qfC:\\f.txt", "-qSpeed2", "-quickPrint", "-monitor" }, "", "ghost typing and printing");
    expectArgs({ "-xyz" }, "-xyz", "an unknown option is left for wxCmdLineParser to refuse");

    std::printf("\nafter --, everything is a file name\n");
    expectArgs({ "--", "-nosession", "-n42" }, "-- -nosession -n42", "nothing rewritten after --");

    std::printf("\n-l languages\n");
    expectLang("Python", "Python");
    expectLang("python", "Python");
    expectLang("cpp", "C++");
    expectLang("C++", "C++");
    expectLang("cs", "C#");
    expectLang("fsharp", "");
    expectLang("objc", "Objective-C");
    expectLang("bash", "Shell");
    expectLang("javascript.js", "JavaScript");
    expectLang("props", "Properties");
    expectLang("ini", "Properties");
    expectLang("rc", "Resource file");
    expectLang("vb", "Visual Basic");
    expectLang("mssql", "MS SQL");
    expectLang("fortran", "Fortran (free form)");
    expectLang("fortran77", "Fortran (fixed form)");
    expectLang("normal", "Normal Text");
    expectLang("Normal Text", "Normal Text");
    expectLang("nosuchlanguage", "");
    expectLang("", "");

    // Every Language-menu name, as written in the menu, finds itself.
    size_t n = 0;
    const WxnLang* t = wxnLangTable(n);
    int missed = 0;
    for (size_t i = 0; i < n; ++i)
        if (wxncli::languageFor(t[i].name) != t[i].name) { std::printf("        %s\n", t[i].name); ++missed; }
    check(missed == 0, "every Language-menu name finds itself");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
