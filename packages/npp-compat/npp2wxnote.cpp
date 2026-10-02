// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp2wxnote - convert Notepad++'s files into wxNote's, from the command line.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// The same translations the npp-compat plugin runs inside wxNote (npp_translate.h), for converting
// files without wxNote running - and for the maintainer converting a theme to bundle:
//
//   npp2wxnote theme       <theme.xml>       <out.yaml>
//   npp2wxnote contextmenu <contextMenu.xml> <out.yaml>
//   npp2wxnote workspace   <workspace.xml>   <out.yaml>
//   npp2wxnote languages   <langs.xml>       <out.yaml> [<langs.model.xml> [<theme.xml>]]
//                          (the changes made to Notepad++'s langs.model.xml, and the theme's user-defined
//                          keywords, as languages.yaml)
//   npp2wxnote config      <config.xml>      (prints the settings.yaml lines)
//   npp2wxnote session     <session.xml>     (prints the files it lists)
//   npp2wxnote detect      <file.xml>        (prints what kind of Notepad++ file it is)
//
// Paths are UTF-8 throughout: on Windows the arguments are taken as UTF-16 (wmain) and every file is
// opened through std::filesystem, so a folder named outside the console's code page still works.

#include "npp_translate.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace nppcompat;
namespace fs = std::filesystem;

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeFile(const std::string& path, const std::string& text)
{
    std::ofstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    f << text;
    f.close();
    return !f.fail();
}

static int usage()
{
    std::fprintf(stderr,
        "usage: npp2wxnote theme|contextmenu|workspace <in.xml> <out.yaml>\n"
        "       npp2wxnote languages <langs.xml> <out.yaml> [<langs.model.xml> [<theme.xml>]]\n"
        "       npp2wxnote config|session|detect <in.xml>\n");
    return 2;
}

static int run(const std::vector<std::string>& args)
{
    if (args.size() < 3) return usage();
    const std::string& cmd = args[1];
    const std::string& in = args[2];
    std::string xml, err;
    if (!readFile(in, xml)) { std::fprintf(stderr, "cannot read %s\n", in.c_str()); return 1; }

    if (cmd == "detect") { std::printf("%s\n", nppFileKindName(detectNppFile(xml))); return 0; }
    if (cmd == "config")
    {
        ConfigTranslation t;
        if (!settingsFromConfig(xml, t, &err)) { std::fprintf(stderr, "%s: %s\n", in.c_str(), err.c_str()); return 1; }
        for (const auto& kv : t.settings) std::printf("%s: %s\n", kv.first.c_str(), kv.second.c_str());
        for (const std::string& s : t.notTranslated) std::printf("# not translated: %s\n", s.c_str());
        return 0;
    }
    if (cmd == "session")
    {
        std::vector<std::string> files;
        if (!sessionFilesFromNpp(xml, files, &err)) { std::fprintf(stderr, "%s: %s\n", in.c_str(), err.c_str()); return 1; }
        for (const std::string& f : files) std::printf("%s\n", f.c_str());
        return 0;
    }

    if (args.size() < 4) return usage();
    const std::string& out = args[3];
    std::string yaml;
    std::vector<std::string> notTranslated;
    bool ok = false;
    if (cmd == "theme")
    {
        wxntheme::Theme t;
        ok = themeFromNpp(xml, t, &err);
        if (ok) { yaml = wxntheme::emit(t); ok = !yaml.empty(); if (!ok) err = "could not write the theme"; }
    }
    else if (cmd == "contextmenu") ok = contextMenuFromNpp(xml, yaml, notTranslated, &err);
    else if (cmd == "languages")
    {
        std::string model, theme;
        if (args.size() > 4 && !readFile(args[4], model)) { std::fprintf(stderr, "cannot read %s\n", args[4].c_str()); return 1; }
        if (args.size() > 5 && !readFile(args[5], theme)) { std::fprintf(stderr, "cannot read %s\n", args[5].c_str()); return 1; }
        LanguagesTranslation t;
        ok = languagesFromNpp(xml, model, theme, t, &err);
        yaml = t.yaml.empty() ? std::string("# Nothing to import: no changes to Notepad++'s language definitions.\nlanguages:\n") : t.yaml;
        notTranslated = t.notTranslated;
    }
    else if (cmd == "workspace")
    {
        const size_t slash = in.find_last_of("/\\");
        ok = workspaceFromNpp(xml, slash == std::string::npos ? std::string(".") : in.substr(0, slash), yaml, &err);
    }
    else return usage();

    if (!ok) { std::fprintf(stderr, "%s: %s\n", in.c_str(), err.c_str()); return 1; }
    if (!writeFile(out, yaml)) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    for (const std::string& s : notTranslated) std::fprintf(stderr, "not translated: %s\n", s.c_str());
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        const auto u = fs::path(argv[i]).u8string();
        args.emplace_back(u.begin(), u.end());
    }
    return run(args);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
