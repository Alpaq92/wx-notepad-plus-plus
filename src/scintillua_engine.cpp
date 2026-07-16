// SPDX-License-Identifier: Apache-2.0
//
// scintillua_engine - wxNote's native language-highlighting engine.
// Copyright 2026 The wxNote Authors.

#include "scintillua_engine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int luaopen_lpeg(lua_State*);   // LPeg's module entry (built into the runtime)
}

#include <cstdio>
#include <fstream>
#include <iterator>

namespace scintillua {
namespace {

// Lua-side helpers set up once per engine:
//   _wxn_lex(name, text) -> loads the named lexer (cached) and returns its
//     {tag, endpos, tag, endpos, ...} token list.
//   _wxn_validate(name)  -> (ok, err): (re)load the named lexer, reporting failure.
const char* kBootstrap =
    "local _cache = {}\n"
    "function _wxn_lex(name, text)\n"
    "  local lex = _cache[name]\n"
    "  if not lex then lex = require('lexer').load(name); _cache[name] = lex end\n"
    "  return lex:lex(text, 1)\n"
    "end\n"
    "function _wxn_validate(name)\n"
    "  _cache[name] = nil\n"
    "  local ok, res = pcall(function() return require('lexer').load(name) end)\n"
    "  if ok then _cache[name] = res end\n"
    "  return ok, (ok and '' or tostring(res))\n"
    "end\n"
    // _wxn_lexfold(name, text) -> tokens, folds. Same token list as _wxn_lex, plus (when the
    // grammar declares fold points or indentation folding) a {line -> Scintilla-fold-level} table
    // from Scintillua's lex:fold(). Scintillua's standalone library populates neither `fold`
    // (defaults off) nor `style_at` (the byte->tag lookup fold() needs to reject fold symbols that
    // land inside strings/comments), so wire both here from the single lex pass before folding.
    "function _wxn_lexfold(name, text)\n"
    "  local L = require('lexer')\n"
    "  local lex = _cache[name]\n"
    "  if not lex then lex = L.load(name); _cache[name] = lex end\n"
    "  local tokens = lex:lex(text, 1)\n"
    "  local folds = nil\n"
    "  if lex._fold_points or lex._fold_by_indentation then\n"
    "    L.property['fold'] = '1'\n"
    "    local n = #tokens\n"
    "    L.style_at = setmetatable({}, { __index = function(_, pos)\n"
    "      local lo, hi, ans = 1, n // 2, 'default'\n"        // token k: tag=tokens[2k-1], one-past-end=tokens[2k]
    "      while lo <= hi do\n"
    "        local mid = (lo + hi) // 2\n"
    "        if tokens[mid * 2] > pos then ans = tokens[mid * 2 - 1]; hi = mid - 1 else lo = mid + 1 end\n"
    "      end\n"
    "      return ans\n"
    "    end })\n"
    "    L.fold_level = setmetatable({}, { __index = function() return L.FOLD_BASE end })\n"
    "    folds = lex:fold(text, 1, L.FOLD_BASE)\n"
    "  end\n"
    "  return tokens, folds\n"
    "end\n";

// Lua's package.path matches with forward slashes on every platform; normalize so a Windows
// backslash in the search directory doesn't confuse the `?`-substitution pattern.
std::string fwd(std::string s)
{
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

// A lexer name becomes both a filename (<dir>/<name>.lua) and a require() key, so fold it to
// identifier characters: keeps arbitrary display names (spaces, punctuation) from breaking the
// path or traversing out of the writable dir. Not injective (e.g. "C++"/"C--" both -> "C__"), so
// callers must go through Engine::lexerKey() to get a collision-free key, never use this directly
// as the on-disk/cache key.
std::string sanitizeName(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        out += ok ? c : '_';
    }
    return out.empty() ? std::string("udl") : out;
}

}  // namespace

Engine::Engine(const std::string& lexerLuaDir, const std::string& writableDir)
    : writableDir_(writableDir)
{
    L_ = luaL_newstate();
    if (!L_) { err_ = "luaL_newstate failed"; return; }
    luaL_openlibs(L_);
    luaL_requiref(L_, "lpeg", luaopen_lpeg, 0);
    lua_pop(L_, 1);

    // Search registered lexers (writable dir) first, then Scintillua's lexer.lua. Set package.path
    // through the C API (push the directories as a string value) rather than string-formatting them
    // into Lua source - so a ']]' or any other Lua metacharacter in the install/data path can't break
    // the assignment (which would otherwise silently disable all highlighting).
    {
        const std::string prefix = fwd(writableDir) + "/?.lua;" + fwd(lexerLuaDir) + "/?.lua;";
        lua_getglobal(L_, "package");                       // package
        lua_getfield(L_, -1, "path");                       // package, oldpath
        const char* cur = lua_tostring(L_, -1);
        const std::string newPath = prefix + (cur ? cur : "");
        lua_pop(L_, 1);                                     // package
        lua_pushlstring(L_, newPath.data(), newPath.size());
        lua_setfield(L_, -2, "path");                       // package.path = newPath
        lua_pop(L_, 1);                                     // (empty)
    }
    if (luaL_dostring(L_, "require('lexer')") != LUA_OK) {
        err_ = std::string("could not load lexer.lua: ") + lua_tostring(L_, -1);
        return;
    }
    if (luaL_dostring(L_, kBootstrap) != LUA_OK) { err_ = lua_tostring(L_, -1); return; }
    ready_ = true;
}

Engine::~Engine()
{
    if (L_) lua_close(L_);
}

// Give each distinct display name a unique, collision-free lexer key. Reuses the key for a name
// already seen (so re-registering the same language reuses its file); otherwise mints one from the
// sanitized base, adding a numeric suffix if some other name already claimed that base.
std::string Engine::lexerKey(const std::string& name, bool create)
{
    auto it = keyByName_.find(name);
    if (it != keyByName_.end()) return it->second;
    if (!create) return std::string();
    const std::string base = sanitizeName(name);
    std::string key = base;
    auto taken = [&](const std::string& k) {
        for (const auto& kv : keyByName_) if (kv.second == k) return true;
        return false;
    };
    for (int n = 2; taken(key); ++n) key = base + "_" + std::to_string(n);
    keyByName_[name] = key;
    return key;
}

bool Engine::registerLexer(const std::string& name, const std::string& luaSource)
{
    if (!ready_) return false;
    const bool existed = keyByName_.find(name) != keyByName_.end();
    const std::string safe = lexerKey(name, /*create=*/true);
    const std::string path = writableDir_ + "/" + safe + ".lua";

    // Preserve the currently-working file so a rejected re-registration doesn't destroy it.
    std::string backup;
    bool hadFile = false;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) { hadFile = true; backup.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
    }
    {
        std::ofstream o(path, std::ios::binary);
        if (!o) { err_ = "cannot write " + path; if (!existed) keyByName_.erase(name); return false; }
        o << luaSource;
    }

    lua_getglobal(L_, "_wxn_validate");
    lua_pushstring(L_, safe.c_str());
    bool ok = false;
    if (lua_pcall(L_, 1, 2, 0) != LUA_OK) {
        const char* e = lua_tostring(L_, -1);
        err_ = e ? e : "validate failed";
        lua_pop(L_, 1);
    } else {
        ok = lua_toboolean(L_, -2) != 0;
        if (!ok) { const char* e = lua_tostring(L_, -1); err_ = e ? e : "lexer failed to load"; }
        lua_pop(L_, 2);
    }

    if (!ok) {
        // Roll back to the previous good state: restore the old file (or remove ours) and, if this
        // name had no working lexer before, drop its key so lex() reports it as unregistered.
        if (hadFile) { std::ofstream o(path, std::ios::binary); o << backup; }
        else         { std::remove(path.c_str()); }
        if (!existed) keyByName_.erase(name);
    }
    return ok;
}

std::vector<Token> Engine::lex(const std::string& name, const std::string& text)
{
    return lex(name, text.data(), text.size());
}

std::vector<Token> Engine::lex(const std::string& name, const char* data, size_t len)
{
    std::vector<Token> out;
    if (!ready_ || !data) return out;
    const std::string safe = lexerKey(name, /*create=*/false);
    if (safe.empty()) return out;                 // never registered -> no lexer to run
    lua_getglobal(L_, "_wxn_lex");
    lua_pushstring(L_, safe.c_str());
    lua_pushlstring(L_, data, len);
    if (lua_pcall(L_, 2, 1, 0) != LUA_OK) {
        const char* e = lua_tostring(L_, -1);
        err_ = e ? e : "lex failed";
        lua_pop(L_, 1);
        return out;
    }
    readTokenTableTop(out);
    return out;
}

std::vector<Token> Engine::lexAndFold(const std::string& name, const char* data, size_t len,
                                      std::vector<int>* foldLevels)
{
    std::vector<Token> out;
    if (foldLevels) foldLevels->clear();
    if (!ready_ || !data) return out;
    const std::string safe = lexerKey(name, /*create=*/false);
    if (safe.empty()) return out;
    lua_getglobal(L_, "_wxn_lexfold");
    lua_pushstring(L_, safe.c_str());
    lua_pushlstring(L_, data, len);
    if (lua_pcall(L_, 2, 2, 0) != LUA_OK) {       // -> tokens, folds (folds may be nil)
        const char* e = lua_tostring(L_, -1);
        err_ = e ? e : "lexfold failed";
        lua_pop(L_, 1);
        return out;
    }
    // The fold table (top of stack) maps 1-based line -> Scintilla fold-level; it is a dense
    // sequence for every line the buffer spans, so read it as line i -> foldLevels[i - 1].
    if (foldLevels && lua_istable(L_, -1)) {
        const lua_Integer n = luaL_len(L_, -1);
        foldLevels->reserve(static_cast<size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L_, -1, i);
            foldLevels->push_back(static_cast<int>(lua_tointeger(L_, -1)));
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);                               // pop folds; tokens table now on top
    readTokenTableTop(out);
    return out;
}

// Reads the {tag, endpos, ...} pairs of the token table on top of the stack, then pops it.
void Engine::readTokenTableTop(std::vector<Token>& out)
{
    if (lua_istable(L_, -1)) {
        const lua_Integer n = luaL_len(L_, -1);
        for (lua_Integer i = 1; i + 1 <= n; i += 2) {
            lua_rawgeti(L_, -1, i);
            const char* tag = lua_tostring(L_, -1);
            std::string t = tag ? tag : "";
            lua_pop(L_, 1);
            lua_rawgeti(L_, -1, i + 1);
            const int pos = static_cast<int>(lua_tointeger(L_, -1));
            lua_pop(L_, 1);
            out.push_back({pos, std::move(t)});
        }
    }
    lua_pop(L_, 1);
}

}  // namespace scintillua
