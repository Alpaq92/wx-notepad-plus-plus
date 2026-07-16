// SPDX-License-Identifier: GPL-3.0-or-later
//
// udl-compat - translate a Notepad++ User-Defined Language into a Scintillua lexer.
// Copyright 2026 The wxNote Authors. See LICENSE (GPL-3.0-or-later).
//
// This module is GPL, not Apache-2.0 like wxNote's core, because it reproduces
// Notepad++'s UDL file format to interoperate with it. See LICENSING.md.
//
// The translation itself is deliberately kept as a pure function over plain
// std::string/std::vector types (UdlDef -> Lua source string), with no
// wxWidgets or Scintilla dependency, so it can be unit-tested standalone. The
// runtime plugin (a Nib plugin registering a Scintillua language provider) adapts
// wxNote's own UdlLanguage (src/udl.h, which moves into this package) into a
// UdlDef and feeds translateUdlToScintillua()'s output to the Scintillua engine.

#pragma once

#include <string>
#include <vector>

namespace udlcompat {

// The subset of a Notepad++ UDL that maps cleanly onto a Scintillua lexer. Field
// names mirror the UDL model in wxNote's src/udl.h; the runtime plugin populates
// this from a parsed userDefineLang.xml.
struct UdlDef {
    struct Delimiter {                 // one open/close (optionally escaped) span
        std::string open;
        std::string close;             // empty => single-line, ends at line end
        std::string escape;            // empty => no escape char
    };

    std::string              name;                 // language name (Scintillua lexer name)
    std::string              ext;                  // space-separated file extensions (no dots), e.g. "ini conf"
    std::vector<std::string> keywords[8];          // Keywords1..8
    bool                     keywordsCaseInsensitive = false;
    std::string              lineComment;           // e.g. "//"  (empty => none)
    std::string              blockCommentOpen;      // e.g. "/*"
    std::string              blockCommentClose;     // e.g. "*/"
    std::vector<Delimiter>   delimiters;            // string/char spans
    std::string              operators;             // literal operator chars, e.g. "+-*/=<>"
    bool                     hasNumbers = true;     // emit a lexer.number rule
};

// Produce a Scintillua lexer (Lua source) that reproduces the UDL's highlighting.
// Deterministic and side-effect free. Only emits Scintillua constructs confirmed
// against real bundled lexers (lexer.new, add_rule, tag, word_match, to_eol,
// range, number, lpeg.S); see udl_scintillua.cpp for the per-construct notes.
std::string translateUdlToScintillua(const UdlDef& udl);

}  // namespace udlcompat
