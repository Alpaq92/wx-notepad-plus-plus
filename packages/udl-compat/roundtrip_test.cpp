// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl-compat - end-to-end integration test.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// Proves the translator's output actually works: it translates a UDL into a
// Scintillua lexer, then loads and runs that lexer under a REAL embedded
// Lua + LPeg + Scintillua runtime and checks the produced token tags. This is
// the verification a pure-string test cannot give. It is gated behind the
// UDL_COMPAT_SCINTILLUA_TEST CMake option (which fetches Lua, LPeg, and
// Scintillua's lexer.lua) so the default build needs no network or Lua.
//
// argv[1] is a directory that already contains Scintillua's lexer.lua; this
// program writes the generated lexer next to it and points Scintillua there.

#include "udl_scintillua.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int luaopen_lpeg(lua_State*);
}

#include <cstdio>
#include <fstream>
#include <string>

// Driver: set the lexer search path to `dir`, load the generated lexer, lex a
// snippet, and return a sorted, comma-joined set of the token tags seen.
static const char* kDriver =
    "local dir = ...\n"
    "package.path = dir .. '/?.lua;' .. package.path\n"
    "local lexer = require('lexer')\n"
    "local lex = lexer.load('udl_roundtrip')\n"
    "local text = 'if x then\\n  y = \"hi\" // c\\n  n = 42 + 3\\nend'\n"
    "local toks = lex:lex(text, 1)\n"
    "local seen = {}\n"
    "for i = 1, #toks, 2 do seen[tostring(toks[i])] = true end\n"
    "local out = {}\n"
    "for k in pairs(seen) do out[#out + 1] = k end\n"
    "table.sort(out)\n"
    "return table.concat(out, ',')\n";

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: udl_scintillua_roundtrip <dir-with-lexer.lua>\n"); return 2; }
    const std::string dir = argv[1];

    udlcompat::UdlDef u;
    u.name = "udl_roundtrip";
    u.keywords[0] = {"if", "then", "else", "end"};
    u.lineComment = "//";
    u.delimiters = {{"\"", "\"", ""}};
    u.operators = "=+-";
    const std::string lua = udlcompat::translateUdlToScintillua(u);
    { std::ofstream o(dir + "/udl_roundtrip.lua", std::ios::binary); o << lua; }

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_requiref(L, "lpeg", luaopen_lpeg, 0);
    lua_pop(L, 1);

    if (luaL_loadstring(L, kDriver) != LUA_OK) {
        std::fprintf(stderr, "driver load error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    lua_pushstring(L, dir.c_str());
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        std::fprintf(stderr, "lexing error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    const char* res = lua_tostring(L, -1);
    const std::string tags = res ? res : "";
    lua_close(L);

    std::printf("token tags seen: %s\n", tags.c_str());
    const char* need[] = {"keyword", "string", "comment", "number", "operator"};
    int fail = 0;
    for (const char* t : need)
        if (tags.find(t) == std::string::npos) { std::printf("MISSING expected tag: %s\n", t); fail = 1; }
    std::printf(fail ? "ROUNDTRIP FAILED\n" : "ROUNDTRIP OK\n");
    return fail;
}
