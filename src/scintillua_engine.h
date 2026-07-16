// SPDX-License-Identifier: Apache-2.0
//
// scintillua_engine - wxNote's native language-highlighting engine.
// Copyright 2026 The wxNote Authors.
//
// Embeds Lua + LPeg + Scintillua to highlight text with dynamically-registered
// lexers (Scintillua Lua LPeg grammars). It is a generic "load a lexer, lex some
// text" engine with no Notepad++ specifics, so it is Apache-2.0 like the rest of
// the core. Plugins (e.g. the optional GPL udl-compat) hand it lexer source through
// the nib.langdef capability; the host drives it from the editor's container-lexer
// styling callback. Nothing here reproduces a Notepad++ format.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace scintillua {

// One lexed token: the bytes from the previous token's end up to `endPos` (a byte
// offset into the lexed text) carry the tag `tag` - a Scintillua tag name such as
// "keyword", "string", "comment", or "whitespace.<lang>".
struct Token {
    int         endPos = 0;
    std::string tag;
};

class Engine {
public:
    // lexerLuaDir : read-only directory containing Scintillua's `lexer.lua`
    //               (bundled next to the executable).
    // writableDir : a writable directory where registered lexers are written as
    //               `<name>.lua` (use the per-user data dir, not the install dir).
    Engine(const std::string& lexerLuaDir, const std::string& writableDir);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // True once the Lua state, LPeg, and Scintillua's lexer.lua all loaded.
    bool ok() const { return ready_; }
    const std::string& lastError() const { return err_; }

    // Register (or replace) a lexer named `name` from Scintillua Lua source. Writes it
    // and validates by loading it; returns false and sets lastError() on invalid Lua.
    bool registerLexer(const std::string& name, const std::string& luaSource);

    // Lex `text` with the named lexer; tokens in document order, empty on error.
    std::vector<Token> lex(const std::string& name, const std::string& text);
    // Zero-copy overload: lex `len` bytes at `data` (e.g. a Scintilla range pointer) - avoids
    // copying the whole buffer into a std::string on the STYLENEEDED hot path.
    std::vector<Token> lex(const std::string& name, const char* data, size_t len);

    // Lex AND compute per-line fold levels in a single pass (one grammar match reused for both).
    // Returns the tokens exactly like lex(); additionally, when `foldLevels` is non-null it is
    // filled with one fold-level per line (index 0 = document line 0). It is left EMPTY when the
    // lexer declares no fold points, so a caller can distinguish "this language doesn't fold" from
    // "every line is at the base level". Each value is already in Scintilla's SC_FOLDLEVEL* bit
    // layout (base 0x400, header 0x2000, white 0x1000) - pass it straight to SCI_SETFOLDLEVEL.
    std::vector<Token> lexAndFold(const std::string& name, const char* data, size_t len,
                                  std::vector<int>* foldLevels);

private:
    // Map a display name to a collision-free lexer key (its `<key>.lua` filename and require()/cache
    // key). Distinct names that would sanitize to the same identifier (e.g. "C++" and "C--") get
    // distinct keys, so they never share a file or cached grammar. `create` mints a new unique key
    // for an unseen name; when false, an unregistered name returns "" (its lexer doesn't exist).
    std::string lexerKey(const std::string& name, bool create);

    // Read a {tag, endpos, tag, endpos, ...} token table sitting on top of the Lua stack into
    // `out` (in document order), then pop it. Shared by lex() and lexAndFold().
    void readTokenTableTop(std::vector<Token>& out);

    lua_State*  L_ = nullptr;
    std::string writableDir_;
    std::string err_;
    bool        ready_ = false;
    std::unordered_map<std::string, std::string> keyByName_;   // display name -> unique lexer key
};

}  // namespace scintillua
