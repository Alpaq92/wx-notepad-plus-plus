// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl2scintillua - convert a Notepad++ userDefineLang.xml into Scintillua lexers.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Standalone command-line front end to the udl-compat translator (no wxWidgets /
// Scintilla / Lua needed to run it). Reads a userDefineLang.xml and writes the
// generated Scintillua Lua lexer to stdout (or to a file with -o).
//   udl2scintillua path/to/userDefineLang.xml [-o out.lua]

#include "udl_parse.h"
#include "udl_scintillua.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    std::string inPath, outPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (inPath.empty()) inPath = a;
    }
    if (inPath.empty()) {
        std::fprintf(stderr, "usage: udl2scintillua <userDefineLang.xml> [-o out.lua]\n");
        return 2;
    }

    const std::string xml = udlcompat::readFileToString(inPath);
    if (xml.empty()) { std::fprintf(stderr, "error: cannot open %s\n", inPath.c_str()); return 1; }

    udlcompat::UdlDef udl;
    std::string err;
    if (!udlcompat::parseUserDefineLangXml(xml, udl, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const std::string lua = udlcompat::translateUdlToScintillua(udl);

    if (outPath.empty()) {
        std::fputs(lua.c_str(), stdout);
    } else {
        std::ofstream out(outPath, std::ios::binary);
        if (!out) { std::fprintf(stderr, "error: cannot write %s\n", outPath.c_str()); return 1; }
        out << lua;
        std::fprintf(stderr, "wrote %s (language '%s')\n", outPath.c_str(), udl.name.c_str());
    }
    return 0;
}
