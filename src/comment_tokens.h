#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// comment_tokens - what a comment looks like in each language the app can open.
//
// Until this table existed, Edit > Comment/Uncomment (Ctrl+/) hardcoded "//" and Stream Comment
// hardcoded "/* */" for EVERY buffer. Pressing Ctrl+/ in a .py, .lua, .sql, .yaml or .ps1 file
// inserted "//", which is not a comment in any of them: the file was silently corrupted and
// nothing warned. Every comment command now keys off the buffer's language through this header.
//
// Deliberately a standalone header over plain std::string/const char* - no wx, no Scintilla, no
// main.cpp - so the whole table and the two resolvers below are unit-testable without an editor
// (tests/comment_tokens_test.cpp links nothing at all). The editor-side loops in main.cpp call
// wxnLineCommentLen()/wxnLineCommentInsert() rather than re-deriving the rules, so the test pins
// the behaviour the commands actually get.
//
// Keys are lowercase and deliberately share the vocabulary flLangKey() (main.cpp) already uses for
// the Function List - "cpp", "python", "js", "sh", "ini", ... - so a user's functionList.conf
// `ext` override names a language here too, for free.
//
// NOT covered: languages registered at runtime through nib.langdef (the udl-compat plugin's
// translated userDefineLang.xml files). udl-compat's UdlDef already carries lineComment /
// blockCommentOpen / blockCommentClose (packages/udl-compat/udl_scintillua.h), parsed from the
// UDL, but nib.langdef/1's register(name, exts, lua) has no channel to hand them back to the host,
// so a UDL buffer falls through to the extension table below. Sharing THIS struct with UdlDef
// would not help - these rows are static const char* literals, UdlDef's are owning std::strings
// parsed at runtime, and udl-compat is a separate GPL package that deliberately depends on no core
// header. Closing the gap properly means a nib.langdef/2 that carries the three tokens; that is an
// public plugin-ABI bump and is left as a follow-up.

#include <cstddef>
#include <cstring>
#include <string>

// How one language spells a comment. An empty `line` means the language has NO line-comment form
// (CSS, HTML, Smalltalk, JSON); an empty `blockOpen` means no block form (Python, YAML, shell).
// A language with neither (JSON, Intel HEX, Diff) must leave the buffer untouched - see the
// callers in main.cpp, which report it in the status bar instead of inserting something wrong.
struct WxnCommentStyle
{
    const char* line       = "";
    const char* blockOpen  = "";
    const char* blockClose = "";
    // Batch's `rem` and Forth's `\` are words, not punctuation: they only comment when followed by
    // whitespace, so they are inserted with a trailing space and only RECOGNISED with one.
    bool lineNeedsSpace  = false;
    bool lineCaseless    = false;   // batch: REM/rem/Rem are all the same token
    // Does a line ending in ':' open an indented block? True for the Python family and YAML, false
    // for everything else - autoIndentOnNewline used to assume true universally, which indented the
    // line after every C++ `public:` label and every "Note:" in a plain-text file.
    bool colonOpensBlock = false;

    bool hasLine()  const { return line && line[0]; }
    bool hasBlock() const { return blockOpen && blockOpen[0] && blockClose && blockClose[0]; }
    bool empty()    const { return !hasLine() && !hasBlock(); }
};

struct WxnCommentLang
{
    const char*     key;    // canonical language key (flLangKey vocabulary where they overlap)
    const char*     name;   // display name; matches wxnLangTable's `name` VERBATIM for menu languages
    WxnCommentStyle style;
};

// The table. One row per built-in language (the 112 of wxnLangTable, so a manual Language-menu pick
// always resolves) plus the handful of extension-only variants the menu has no entry for (SCSS/LESS
// take "//" where plain CSS does not - that difference alone is a corruption class).
//
// Sources: for the exotic entries the vendored Lexilla lexer is authoritative and was read rather
// than guessed - e.g. BaanC's "|" (LexBaan.cxx), Visual Prolog's "%" (LexVisualProlog.cxx),
// Forth's "\ " and "( )" (LexForth.cxx), Smalltalk's double-quote spans (LexSmalltalk.cxx).
inline const WxnCommentLang* wxnCommentLangTable(std::size_t& n)
{
    // Shorthands keep the rows to one line each: L=line only, B=block only, LB=both.
    #define WXN_L(t)          WxnCommentStyle{ t,  "",  "" }
    #define WXN_B(o, c)       WxnCommentStyle{ "", o,   c  }
    #define WXN_LB(t, o, c)   WxnCommentStyle{ t,  o,   c  }
    static const WxnCommentLang t[] = {
        // ---- key ------------ display name -------------- style ------------------------------
        { "abl",          "ABL (OpenEdge)",       WXN_LB("//", "/*", "*/") },
        { "actionscript", "ActionScript",         WXN_LB("//", "/*", "*/") },
        { "ada",          "Ada",                  WXN_L ("--") },
        { "asciidoc",     "AsciiDoc",             WXN_L ("//") },
        { "asn1",         "ASN.1",                WXN_LB("--", "/*", "*/") },
        // ASP/JSP/PHP are mixed HTML+script documents and the correct token depends on where the
        // caret is, which a line-based command cannot know. Each takes the form of its SCRIPT half
        // (that is where code that wants commenting lives); JSP has no line form, so it keeps the
        // JSP-specific block spelling rather than HTML's, which would not nest inside a scriptlet.
        { "asp",          "ASP",                  WXN_L ("'") },
        { "asm",          "Assembly",             WXN_L (";") },
        { "au3",          "AutoIt",               WXN_LB(";", "#cs", "#ce") },
        { "avs",          "AviSynth",             WXN_LB("#", "/*", "*/") },
        { "baan",         "BaanC",                WXN_L ("|") },
        { "batch",        "Batch",                WxnCommentStyle{ "rem", "", "", true, true } },
        { "bib",          "BibTeX",               WXN_L ("%") },
        { "blitzbasic",   "BlitzBasic",           WXN_L (";") },
        { "c",            "C",                    WXN_LB("//", "/*", "*/") },
        { "cs",           "C#",                   WXN_LB("//", "/*", "*/") },
        { "cpp",          "C++",                  WXN_LB("//", "/*", "*/") },
        { "caml",         "Caml",                 WXN_B ("(*", "*)") },
        { "cil",          "CIL",                  WXN_LB("//", "/*", "*/") },
        { "clarion",      "Clarion",              WXN_L ("!") },
        { "cmake",        "CMake",                WXN_LB("#", "#[[", "]]") },
        { "cobol",        "COBOL",                WXN_L ("*>") },
        { "coffeescript", "CoffeeScript",         WxnCommentStyle{ "#", "###", "###", false, false, true } },
        { "csound",       "Csound",               WXN_L (";") },
        // Plain CSS has NO line comment - "//" is not valid CSS - but the SCSS/LESS/SASS
        // preprocessors do. Same lexer, different rule; that is why they are separate rows.
        { "css",          "CSS",                  WXN_B ("/*", "*/") },
        { "scss",         "SCSS",                 WXN_LB("//", "/*", "*/") },
        { "less",         "LESS",                 WXN_LB("//", "/*", "*/") },
        { "d",            "D",                    WXN_LB("//", "/*", "*/") },
        { "dart",         "Dart",                 WXN_LB("//", "/*", "*/") },
        { "dataflex",     "DataFlex",             WXN_LB("//", "/*", "*/") },
        { "diff",         "Diff",                 WxnCommentStyle{} },        // a patch has no comment form
        { "dockerfile",   "Dockerfile",           WXN_L ("#") },
        { "eiffel",       "Eiffel",               WXN_L ("--") },
        { "erlang",       "Erlang",               WXN_L ("%") },
        { "escript",      "ESCRIPT",              WXN_LB("//", "/*", "*/") },
        { "fsharp",       "F#",                   WXN_LB("//", "(*", "*)") },
        { "forth",        "Forth",                WxnCommentStyle{ "\\", "", "", true } },
        { "f77",          "Fortran (fixed form)", WXN_L ("!") },
        { "fortran",      "Fortran (free form)",  WXN_L ("!") },
        { "freebasic",    "FreeBasic",            WXN_LB("'", "/'", "'/") },
        { "gdscript",     "GDScript",             WxnCommentStyle{ "#", "", "", false, false, true } },
        { "po",           "gettext PO",           WXN_L ("#") },
        { "go",           "Go",                   WXN_LB("//", "/*", "*/") },
        { "gui4cli",      "Gui4Cli",              WXN_LB("//", "/*", "*/") },
        { "haskell",      "Haskell",              WXN_LB("--", "{-", "-}") },
        { "hollywood",    "Hollywood",            WXN_LB(";", "/*", "*/") },
        { "html",         "HTML",                 WXN_B ("<!--", "-->") },
        { "inno",         "Inno Setup",           WXN_L (";") },
        { "ihex",         "Intel HEX",            WxnCommentStyle{} },        // record format, no comments
        { "java",         "Java",                 WXN_LB("//", "/*", "*/") },
        { "js",           "JavaScript",           WXN_LB("//", "/*", "*/") },
        { "json",         "JSON",                 WxnCommentStyle{} },        // JSON has no comment form at all
        { "json5",        "JSON5",                WXN_LB("//", "/*", "*/") },
        { "jsp",          "JSP",                  WXN_B ("<%--", "--%>") },
        { "julia",        "Julia",                WXN_LB("#", "#=", "=#") },
        { "kix",          "KIXtart",              WXN_LB(";", "/*", "*/") },
        { "kotlin",       "Kotlin",               WXN_LB("//", "/*", "*/") },
        { "latex",        "LaTeX",                WXN_L ("%") },
        { "lisp",         "LISP",                 WXN_LB(";", "#|", "|#") },
        { "lua",          "Lua",                  WXN_LB("--", "--[[", "]]") },
        { "makefile",     "Makefile",             WXN_L ("#") },
        { "markdown",     "Markdown",             WXN_B ("<!--", "-->") },
        { "matlab",       "MATLAB",               WXN_LB("%", "%{", "%}") },
        { "maxima",       "Maxima",               WXN_B ("/*", "*/") },
        { "metapost",     "MetaPost",             WXN_L ("%") },
        { "mmixal",       "MMIXAL",               WXN_L ("%") },
        { "modula",       "Modula-3",             WXN_B ("(*", "*)") },
        { "mssql",        "MS SQL",               WXN_LB("--", "/*", "*/") },
        { "mysql",        "MySQL",                WXN_LB("#", "/*", "*/") },
        { "nim",          "Nim",                  WxnCommentStyle{ "#", "#[", "]#", false, false, true } },
        { "nix",          "Nix",                  WXN_LB("#", "/*", "*/") },
        { "nncrontab",    "nnCron",               WXN_L ("#") },
        { "nsis",         "NSIS",                 WXN_LB(";", "/*", "*/") },
        { "objc",         "Objective-C",          WXN_LB("//", "/*", "*/") },
        { "octave",       "Octave",               WXN_LB("%", "%{", "%}") },
        { "oscript",      "OScript",              WXN_LB("//", "/*", "*/") },
        { "pascal",       "Pascal",               WXN_LB("//", "{", "}") },
        { "perl",         "Perl",                 WXN_L ("#") },
        { "php",          "PHP",                  WXN_LB("//", "/*", "*/") },
        { "ps",           "PostScript",           WXN_L ("%") },
        { "pov",          "POV-Ray",              WXN_LB("//", "/*", "*/") },
        { "powershell",   "PowerShell",           WXN_LB("#", "<#", "#>") },
        { "props",        "Properties",           WXN_L ("#") },
        { "purebasic",    "PureBasic",            WXN_L (";") },
        { "python",       "Python",               WxnCommentStyle{ "#", "", "", false, false, true } },
        { "r",            "R",                    WXN_L ("#") },
        { "raku",         "Raku",                 WXN_L ("#") },
        { "rebol",        "Rebol",                WXN_L (";") },
        { "registry",     "Registry",             WXN_L (";") },
        { "rc",           "Resource file",        WXN_LB("//", "/*", "*/") },
        // Ruby's =begin/=end must both sit in column 1, which the stream-comment command cannot
        // promise for an arbitrary selection, so Ruby advertises the line form only.
        { "ruby",         "Ruby",                 WXN_L ("#") },
        { "rust",         "Rust",                 WXN_LB("//", "/*", "*/") },
        { "sas",          "SAS",                  WXN_LB("//", "/*", "*/") },
        { "scheme",       "Scheme",               WXN_LB(";", "#|", "|#") },
        { "sh",           "Shell",                WXN_L ("#") },
        { "smalltalk",    "Smalltalk",            WXN_B ("\"", "\"") },
        { "spice",        "SPICE",                WXN_L ("*") },
        { "sql",          "SQL",                  WXN_LB("--", "/*", "*/") },
        { "srec",         "S-Record",             WxnCommentStyle{} },        // record format, no comments
        { "stata",        "Stata",                WXN_LB("//", "/*", "*/") },
        { "swift",        "Swift",                WXN_LB("//", "/*", "*/") },
        { "tcl",          "TCL",                  WXN_L ("#") },
        { "tehex",        "Tektronix hex",        WxnCommentStyle{} },        // record format, no comments
        { "tex",          "TeX",                  WXN_L ("%") },
        { "toml",         "TOML",                 WXN_L ("#") },
        { "txt2tags",     "txt2tags",             WXN_L ("%") },
        { "typescript",   "TypeScript",           WXN_LB("//", "/*", "*/") },
        { "vbscript",     "VBScript",             WXN_L ("'") },
        { "verilog",      "Verilog",              WXN_LB("//", "/*", "*/") },
        { "vhdl",         "VHDL",                 WXN_L ("--") },
        { "vb",           "Visual Basic",         WXN_L ("'") },
        { "visualprolog", "Visual Prolog",        WXN_LB("%", "/*", "*/") },
        { "xml",          "XML",                  WXN_B ("<!--", "-->") },
        { "yaml",         "YAML",                 WxnCommentStyle{ "#", "", "", false, false, true } },
        { "zig",          "Zig",                  WXN_L ("//") },
        // ---- extension-only languages with no Language-menu entry of their own ----
        // INI keeps the traditional ';' while .conf/.cfg/.properties take '#'; lumping all of them
        // into one "ini" bucket (as flLangKey does, which only needs a Function List rule) would
        // hand TOML and .conf files a ';' that comments nothing.
        { "ini",          "INI",                  WXN_L (";") },
        { "conf",         "Config file",          WXN_L ("#") },
        { "elixir",       "Elixir",               WXN_L ("#") },
        { "scala",        "Scala",                WXN_LB("//", "/*", "*/") },
        { "groovy",       "Groovy",               WXN_LB("//", "/*", "*/") },
    };
    #undef WXN_L
    #undef WXN_B
    #undef WXN_LB
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// Row for a canonical key, or nullptr if the key is unknown.
inline const WxnCommentLang* wxnCommentLangForKey(const std::string& key)
{
    if (key.empty()) return nullptr;
    std::size_t n; const WxnCommentLang* t = wxnCommentLangTable(n);
    for (std::size_t i = 0; i < n; ++i) if (key == t[i].key) return &t[i];
    return nullptr;
}

// Comment style for a canonical key. An unknown key yields the all-empty style, which every caller
// already treats as "this buffer has no comment form" - the safe answer for plain text.
inline WxnCommentStyle wxnCommentStyleForKey(const std::string& key)
{
    const WxnCommentLang* l = wxnCommentLangForKey(key);
    return l ? l->style : WxnCommentStyle{};
}

// A Language-menu pick hands back wxnLangTable's `name` verbatim (it is deliberately untranslated -
// see menu_data_language.h), and this table stores the same spelling, so the match is exact.
// "" for Normal Text or any name with no row.
inline std::string wxnCommentLangKeyForName(const std::string& displayName)
{
    if (displayName.empty()) return {};
    std::size_t n; const WxnCommentLang* t = wxnCommentLangTable(n);
    for (std::size_t i = 0; i < n; ++i) if (displayName == t[i].name) return t[i].key;
    return {};
}

// Canonical key for a file, from its LOWERCASED base name (e.g. "main.cpp", "makefile",
// "dockerfile.dev"). Extension first, then the handful of languages whose NAME carries the language
// - the same precedence flLangKey() uses, so an explicit extension always wins. "" = don't know.
inline std::string wxnCommentLangKeyForFileName(const std::string& lowerBaseName)
{
    std::string ext;
    const std::size_t dot = lowerBaseName.find_last_of('.');
    // A leading dot is part of the name, not an extension separator: ".bashrc" has no extension.
    if (dot != std::string::npos && dot > 0) ext = lowerBaseName.substr(dot + 1);

    struct ExtMap { const char* ext; const char* key; };
    static const ExtMap kExt[] = {
        { "c", "c" }, { "h", "c" },
        { "cpp", "cpp" }, { "cc", "cpp" }, { "cxx", "cpp" }, { "c++", "cpp" }, { "hpp", "cpp" },
        { "hxx", "cpp" }, { "hh", "cpp" }, { "ino", "cpp" }, { "inl", "cpp" },
        { "cs", "cs" }, { "java", "java" }, { "rc", "rc" },
        { "mm", "objc" },   // ".m" is deliberately absent: MATLAB and Objective-C both claim it, and
                            // guessing wrong is exactly the corruption this table exists to stop
        { "js", "js" }, { "jsx", "js" }, { "mjs", "js" }, { "cjs", "js" },
        { "ts", "typescript" }, { "tsx", "typescript" },
        { "json", "json" }, { "json5", "json5" }, { "jsonc", "json5" },
        { "py", "python" }, { "pyw", "python" }, { "pyi", "python" },
        { "rb", "ruby" }, { "rake", "ruby" }, { "gemspec", "ruby" },
        { "pl", "perl" }, { "pm", "perl" }, { "pod", "perl" },
        { "raku", "raku" }, { "rakumod", "raku" },
        { "php", "php" }, { "php3", "php" }, { "php4", "php" }, { "php5", "php" }, { "phtml", "php" },
        { "go", "go" }, { "rs", "rust" }, { "swift", "swift" }, { "kt", "kotlin" }, { "kts", "kotlin" },
        { "dart", "dart" }, { "zig", "zig" }, { "d", "d" }, { "jl", "julia" }, { "nim", "nim" },
        { "scala", "scala" }, { "sc", "scala" }, { "groovy", "groovy" }, { "gradle", "groovy" },
        { "ex", "elixir" }, { "exs", "elixir" },
        { "lua", "lua" }, { "sql", "sql" }, { "ddl", "sql" },
        { "sh", "sh" }, { "bash", "sh" }, { "zsh", "sh" }, { "ksh", "sh" }, { "fish", "sh" },
        { "ps1", "powershell" }, { "psm1", "powershell" }, { "psd1", "powershell" },
        { "bat", "batch" }, { "cmd", "batch" },
        { "yml", "yaml" }, { "yaml", "yaml" }, { "toml", "toml" },
        { "ini", "ini" }, { "reg", "registry" },
        { "cfg", "conf" }, { "conf", "conf" }, { "properties", "props" },
        { "md", "markdown" }, { "markdown", "markdown" }, { "mdown", "markdown" }, { "mkd", "markdown" },
        { "adoc", "asciidoc" }, { "asciidoc", "asciidoc" },
        { "html", "html" }, { "htm", "html" }, { "xhtml", "html" }, { "vue", "html" }, { "svelte", "html" },
        { "xml", "xml" }, { "svg", "xml" }, { "xaml", "xml" }, { "xsd", "xml" }, { "xsl", "xml" },
        { "xslt", "xml" }, { "vcxproj", "xml" }, { "csproj", "xml" }, { "plist", "xml" }, { "resx", "xml" },
        { "jsp", "jsp" }, { "asp", "asp" }, { "aspx", "asp" },
        { "css", "css" }, { "scss", "scss" }, { "sass", "scss" }, { "less", "less" },
        { "mk", "makefile" }, { "mak", "makefile" }, { "make", "makefile" },
        { "cmake", "cmake" },
        { "r", "r" }, { "nix", "nix" },
        { "hs", "haskell" }, { "lhs", "haskell" },
        { "ada", "ada" }, { "adb", "ada" }, { "ads", "ada" },
        { "vhd", "vhdl" }, { "vhdl", "vhdl" }, { "sv", "verilog" }, { "svh", "verilog" },
        { "erl", "erlang" }, { "hrl", "erlang" },
        { "tex", "tex" }, { "sty", "tex" }, { "bib", "bib" }, { "mp", "metapost" },
        { "lisp", "lisp" }, { "lsp", "lisp" }, { "el", "lisp" }, { "cl", "lisp" }, { "clj", "lisp" },
        { "scm", "scheme" }, { "ss", "scheme" }, { "rkt", "scheme" },
        { "asm", "asm" }, { "s", "asm" }, { "nasm", "asm" },
        { "vb", "vb" }, { "bas", "vb" }, { "frm", "vb" }, { "vbs", "vbscript" },
        { "f", "f77" }, { "for", "f77" }, { "f77", "f77" },
        { "f90", "fortran" }, { "f95", "fortran" }, { "f03", "fortran" },
        { "pas", "pascal" }, { "pp", "pascal" }, { "dpr", "pascal" },
        { "ml", "caml" }, { "mli", "caml" }, { "fs", "fsharp" }, { "fsi", "fsharp" }, { "fsx", "fsharp" },
        { "tcl", "tcl" }, { "matlab", "matlab" }, { "coffee", "coffeescript" },
        { "gd", "gdscript" }, { "po", "po" }, { "pot", "po" },
        { "e", "eiffel" }, { "st", "smalltalk" }, { "cob", "cobol" }, { "cbl", "cobol" },
        { "do", "stata" }, { "ado", "stata" }, { "sas", "sas" }, { "mac", "maxima" },
        { "pov", "pov" }, { "clw", "clarion" }, { "il", "cil" }, { "nsi", "nsis" }, { "nsh", "nsis" },
        { "iss", "inno" }, { "au3", "au3" }, { "avs", "avs" }, { "avsi", "avs" },
        { "diff", "diff" }, { "patch", "diff" },
        { "hex", "ihex" }, { "srec", "srec" }, { "s19", "srec" },
        { "mod", "modula" }, { "i3", "modula" }, { "m3", "modula" },
        { "as", "actionscript" }, { "eps", "ps" },
    };
    if (!ext.empty())
        for (const auto& m : kExt) if (ext == m.ext) return m.key;

    // Files whose NAME carries the language. Checked after the extension table so "Makefile.in"
    // style suffixes never override an explicit one; matched by prefix so "Dockerfile.dev" works.
    auto startsWith = [&](const char* p) {
        const std::size_t n = std::strlen(p);
        return lowerBaseName.size() >= n && lowerBaseName.compare(0, n, p) == 0;
    };
    if (lowerBaseName == "makefile" || lowerBaseName == "gnumakefile" || startsWith("makefile."))
        return "makefile";
    if (lowerBaseName == "dockerfile" || startsWith("dockerfile.") || startsWith("containerfile"))
        return "dockerfile";
    if (startsWith("cmakelists"))                                    return "cmake";
    if (lowerBaseName == "gemfile" || lowerBaseName == "rakefile")   return "ruby";
    if (lowerBaseName == ".bashrc" || lowerBaseName == ".bash_profile" || lowerBaseName == ".zshrc" ||
        lowerBaseName == ".profile")
        return "sh";
    if (lowerBaseName == ".rprofile")                                return "r";
    if (lowerBaseName == ".gitignore" || lowerBaseName == ".gitattributes" ||
        lowerBaseName == ".dockerignore" || lowerBaseName == ".editorconfig")
        return "conf";
    return {};
}

// Bytes of line-comment token sitting at `from` in `line`, including ONE optional following space
// (so "# x" and "#x" both uncomment cleanly), or 0 if the line is not commented there.
// Word-shaped tokens (batch `rem`, Forth `\`) only count when whitespace or the line end follows,
// so "remark" is never mistaken for a commented "ark".
inline std::size_t wxnLineCommentLen(const std::string& line, std::size_t from, const WxnCommentStyle& cs)
{
    if (!cs.hasLine() || from > line.size()) return 0;
    const std::size_t n = std::strlen(cs.line);
    if (n == 0 || from + n > line.size()) return 0;
    auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i < n; ++i)
    {
        const char a = cs.lineCaseless ? low(line[from + i]) : line[from + i];
        const char b = cs.lineCaseless ? low(cs.line[i])     : cs.line[i];
        if (a != b) return 0;
    }
    const bool atEnd = (from + n >= line.size());
    const char next  = atEnd ? '\0' : line[from + n];
    if (cs.lineNeedsSpace && !atEnd && next != ' ' && next != '\t' && next != '\r' && next != '\n')
        return 0;
    return n + ((next == ' ') ? 1u : 0u);
}

// The exact text to insert to comment a line out.
inline std::string wxnLineCommentInsert(const WxnCommentStyle& cs)
{
    std::string s = cs.hasLine() ? cs.line : "";
    if (cs.lineNeedsSpace) s += ' ';
    return s;
}

// What comment/uncomment has to do to ONE line. The editor loop in main.cpp does nothing but turn
// this into Scintilla calls, so all the reasoning - where the token goes, whether the line is
// already commented, whether to skip it - is here where a test can reach it without an editor.
enum WxnCommentMode { WxnCommentToggle, WxnCommentAdd, WxnCommentRemove };

struct WxnLineCommentEdit
{
    bool        applies   = false;   // false: leave this line alone (blank, or already as asked)
    bool        commented = false;   // was it already commented? then strip, else insert
    std::size_t at        = 0;       // first non-blank byte - the token goes here, so indentation survives
    std::size_t end       = 0;       // one past the last non-blank - where a closing token goes
    std::size_t headLen   = 0;       // bytes to remove at `at`      (uncommenting)
    std::size_t tailLen   = 0;       // bytes to remove at `end - tailLen` (uncommenting a block-wrapped line)
    std::string open;                // text to insert at `at`       (commenting)
    std::string close;               // text to insert at `end`      (commenting; empty for line comments)
};

// `lineNoEol` is one line with its EOL already stripped - a closing token must land BEFORE the
// line break, not after it. A language with no line-comment form but a block form gets each line
// wrapped in the block pair instead, which is what "comment this line out" means in CSS or HTML.
inline WxnLineCommentEdit wxnPlanLineComment(const std::string& lineNoEol, const WxnCommentStyle& cs,
                                             WxnCommentMode mode)
{
    WxnLineCommentEdit e;
    if (cs.empty()) return e;
    e.open  = cs.hasLine() ? wxnLineCommentInsert(cs) : std::string(cs.blockOpen);
    e.close = cs.hasLine() ? std::string()            : std::string(cs.blockClose);

    const std::size_t nb = lineNoEol.find_first_not_of(" \t");
    if (nb == std::string::npos) return e;               // blank line: never commented
    e.at  = nb;
    e.end = lineNoEol.find_last_not_of(" \t") + 1;

    if (cs.hasLine())
        e.headLen = wxnLineCommentLen(lineNoEol, nb, cs);
    else if (e.end >= nb + e.open.size() + e.close.size() &&
             lineNoEol.compare(nb, e.open.size(), e.open) == 0 &&
             lineNoEol.compare(e.end - e.close.size(), e.close.size(), e.close) == 0)
    { e.headLen = e.open.size(); e.tailLen = e.close.size(); }

    e.commented = e.headLen > 0;
    if ((mode == WxnCommentAdd && e.commented) || (mode == WxnCommentRemove && !e.commented)) return e;
    e.applies = true;
    return e;
}
