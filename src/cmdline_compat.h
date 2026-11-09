#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// wxNote - cmdline_compat.h: Notepad++'s spellings of the launch options, turned into wxNote's own before
// wxCmdLineParser sees them, and the language a `-l` names.
// Copyright 2026 The wxNote Authors.
//
// wxNote's options are Notepad4-style short letters with a dash (-g line,col, -n, -r) plus a --long
// name for each. Notepad++ users have scripts and shortcuts with its single-dash words (-multiInst,
// -nosession, -openSession, -ro) and glued values (-n42, -c7, -lpython), which wxCmdLineParser cannot
// read: it would take -nosession for the switches -n -o -s... and refuse the launch. So they are
// rewritten first. Notepad++ options wxNote has no use for (-notabbar, -x100, ...) are dropped rather
// than refused, so such a command line still opens its files. Its -r (recursive wildcards) is not
// taken: in wxNote -r is "reuse the running window", as in Notepad4.
//
// No wx: tests/cmdline_compat_test.cpp drives it with plain strings.

#include "lang_table.h"

#include <cctype>
#include <string>
#include <vector>

namespace wxncli {

namespace detail {
inline bool digits(const std::string& s)
{
    if (s.empty()) return false;
    for (const char c : s) if (c < '0' || c > '9') return false;
    return true;
}
inline bool startsWith(const std::string& s, const char* p, std::string* rest = nullptr)
{
    const std::string pre(p);
    if (s.compare(0, pre.size(), pre) != 0) return false;
    if (rest) *rest = s.substr(pre.size());
    return true;
}
}   // namespace detail

// args[0] is the program. Everything after a bare "--" is a file name and passes through untouched.
inline std::vector<std::string> normalizeArgs(const std::vector<std::string>& args)
{
    using detail::digits;
    using detail::startsWith;
    std::vector<std::string> out;
    std::string line, col;
    bool sawGoto = false, literal = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        if (i == 0 || literal || a.size() < 2 || a[0] != '-' || a[1] == '-')
        {
            if (a == "--") literal = true;
            if (!literal && (startsWith(a, "--goto") || a == "-g")) sawGoto = true;
            out.push_back(a);
            continue;
        }
        std::string v;
        if (a == "-notabbar" || a == "-systemtray" || a == "-loadingTime" || a == "-alwaysOnTop"
            || a == "-quickPrint" || a == "-monitor" || a == "-qSpeed1" || a == "-qSpeed2" || a == "-qSpeed3"
            || startsWith(a, "-titleAdd=") || startsWith(a, "-settingsDir=") || startsWith(a, "-udl=")
            || startsWith(a, "-qn") || startsWith(a, "-qt") || startsWith(a, "-qf")
            || ((startsWith(a, "-x", &v) || startsWith(a, "-y", &v) || startsWith(a, "-p", &v)) && digits(v))
            || (startsWith(a, "-L", &v) && !v.empty()))
            continue;                                               // Notepad++ options wxNote has no use for
        if (a == "-g") { sawGoto = true; out.push_back(a); }
        else if (a == "-ns" || a == "-rs") out.push_back("-" + a);                        // Notepad4's /ns, /rs
        else if (a == "-pluginMessage" || startsWith(a, "-pluginMessage=")) out.push_back("-" + a);
        else if (a == "-ro") out.push_back("-R");
        else if (a == "-nosession") out.push_back("--no-session");
        else if (a == "-multiInst") out.push_back("-n");
        else if (a == "-noPlugin") out.push_back("--safe");
        else if (a == "-openSession") out.push_back("--session");   // its file follows as the next argument
        else if (startsWith(a, "-n", &v) && digits(v)) line = v;    // -n42: line 42
        else if (startsWith(a, "-c", &v) && digits(v)) col = v;     // -c7: column 7
        else if (startsWith(a, "-l", &v) && !v.empty()) out.push_back("--language=" + v);   // -lpython
        else out.push_back(a);
    }
    if (!line.empty() && !sawGoto) out.push_back("--goto=" + line + (col.empty() ? std::string() : "," + col));
    return out;
}

// The Language-menu name a `-l` value stands for: the menu's own name in any case ("python", "C++"), the
// same with punctuation dropped ("cpp" for C++, "cs" for C#, "mssql"), or one of Notepad++'s -l names
// that differ ("objc", "bash", "javascript.js"). "" for none; "Normal Text" for plain text.
inline std::string languageFor(const std::string& written)
{
    auto squash = [](const std::string& s) {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '+') { o += 'p'; continue; }
            if (c == '#') { o += 's'; continue; }
            if (std::isalnum(c)) o += static_cast<char>(std::tolower(c));
        }
        return o;
    };
    static const struct { const char* npp; const char* name; } kAliases[] = {
        { "normal", "" }, { "normaltext", "" }, { "txt", "" }, { "text", "" }, { "objc", "Objective-C" }, { "bash", "Shell" }, { "sh", "Shell" },
        { "javascriptjs", "JavaScript" }, { "js", "JavaScript" }, { "props", "Properties" }, { "ini", "Properties" },
        { "rc", "Resource file" }, { "vb", "Visual Basic" }, { "asm", "Assembly" }, { "fortran", "Fortran (free form)" },
        { "fortran77", "Fortran (fixed form)" }, { "inno", "Inno Setup" }, { "kix", "KIXtart" }, { "nncrontab", "nnCron" },
        { "ihex", "Intel HEX" }, { "srec", "S-Record" }, { "tehex", "Tektronix hex" }, { "po", "gettext PO" },
    };
    const std::string w = squash(written);
    if (w.empty()) return std::string();
    for (const auto& a : kAliases) if (w == a.npp) return *a.name ? std::string(a.name) : std::string("Normal Text");
    size_t n = 0;
    const WxnLang* t = wxnLangTable(n);
    for (size_t i = 0; i < n; ++i) if (squash(t[i].name) == w) return std::string(t[i].name);
    return std::string();
}

}   // namespace wxncli
