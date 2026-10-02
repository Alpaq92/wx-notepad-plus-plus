#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// lang_detect - which Language-menu language a file opens as.
//
// The detection itself is Scintillua's. lexer.detect(), in the lexer.lua wxNote already ships (see
// scintillua::Engine::detect), knows ~340 extensions and whole file names (Makefile, CMakeLists.txt,
// Dockerfile, PKGBUILD, Rakefile...) plus a set of first-line patterns (shebangs, the XML prolog,
// #cloud-config). It answers with a Scintillua lexer name, while wxNote highlights through Lexilla and
// names its languages by wxnLangTable (menu_data_language.h). This header is the glue between them:
//
//   wxnLangForScintillua  Scintillua lexer name -> Language-menu name
//   wxnLangExtOverride    the handful of extensions where wxNote deliberately answers differently
//   wxnDetectLanguage     the whole resolution order:
//
//   1. the user's own mappings (WxnUserExtMaps): the Style Configurator's "User ext.", then a
//      functionlist.yaml `extensions` list. The Function List and the comment commands honour both, so
//      one entry re-types a file for all three;
//   2. wxnLangExtOverride;
//   3. Scintillua, on the file name as typed, then lower-cased ("FOO.CPP" is C++ too);
//   4. the comment-token table's extension/file-name map (comment_tokens.h), which knows languages
//      Scintillua has no entry for (Kotlin, JSON5, Raku, SAS, Inno Setup, Intel HEX...), then the
//      active theme's `ext` attributes for an extension none of that places;
//   5. the first line: Scintillua's patterns, then wxnSniffExtFromContent.
//
// The name outranks the content, as it always has here: an .html page that opens with an XML prolog
// is still HTML. A name that identifies a language wxNote cannot highlight (Elixir, Groovy, AWK...)
// stops at Normal Text rather than letting its shebang turn it into a shell script. Backup copies
// (.bak/.orig/.old/.new, a trailing ~) are judged by the name underneath, so "main.cpp.orig" is C++.
//
// Deliberately standalone - std only, no wx, no Scintilla, no Lua. Scintillua is reached through a
// callback and "is this a Language-menu name?" through another, so tests/lang_detect_test.cpp drives
// the real engine against the real table while the editor passes its own.

#include "comment_tokens.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

inline std::string wxnLangLower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// Everything after the last dot of a bare file name, or "" when there is none or it ends the name.
// The same rule as main.cpp's wxnExtOfName - the one functionlist.yaml's `extensions` are matched
// against - so ".bashrc" yields "bashrc".
inline std::string wxnLangExtOf(const std::string& name)
{
    const std::size_t d = name.rfind('.');
    return (d == std::string::npos || d + 1 == name.size()) ? std::string() : name.substr(d + 1);
}

// The name a backup copy stands for: trailing '~'s and any .bak/.back/.orig/.old/.new peeled off.
// Scintillua strips the same suffixes (bar .bak, the one Windows editors write), but only after its
// own lookups fail; doing it up front lets the override and comment tables see the real name too.
inline std::string wxnLangStripBackup(std::string name)
{
    while (!name.empty() && name.back() == '~') name.pop_back();
    for (;;)
    {
        const std::size_t d = name.rfind('.');
        if (d == std::string::npos || d == 0) break;   // ".bak" alone is a name, not a suffix
        const std::string e = wxnLangLower(name.substr(d + 1));
        if (e != "bak" && e != "back" && e != "orig" && e != "old" && e != "new") break;
        name.erase(d);
    }
    return name;
}

// Scintillua lexer name -> Language-menu name. A Scintillua language with no row here (Elixir, Scala,
// Groovy, Protobuf...) has no Lexilla-highlighted equivalent in wxNote, so its files open as text.
struct WxnLangScRow { const char* sc; const char* lang; };
inline const WxnLangScRow* wxnLangScintilluaTable(std::size_t& n)
{
    static const WxnLangScRow t[] = {
        { "actionscript", "ActionScript" }, { "ada", "Ada" },
        { "arduino", "C++" },                  // sketches are C++, as comment_tokens.h already has .ino
        { "asm", "Assembly" }, { "asp", "ASP" }, { "autoit", "AutoIt" },
        { "bash", "Shell" }, { "batch", "Batch" }, { "bibtex", "BibTeX" }, { "c", "C" }, { "caml", "Caml" },
        { "clojure", "LISP" },                 // the Lisp lexer - comment_tokens.h files .clj under lisp too
        { "cmake", "CMake" }, { "coffeescript", "CoffeeScript" }, { "cpp", "C++" }, { "csharp", "C#" },
        { "css", "CSS" }, { "cuda", "C++" }, { "d", "D" }, { "dart", "Dart" },
        { "desktop", "Properties" },           // key=value files: freedesktop entries, INI, systemd units
        { "diff", "Diff" }, { "dockerfile", "Dockerfile" }, { "eiffel", "Eiffel" }, { "erlang", "Erlang" },
        { "fish", "Shell" }, { "forth", "Forth" },
        { "fortran", "Fortran (free form)" },  // the fixed-form extensions are overridden below
        { "fsharp", "F#" }, { "gettext", "gettext PO" }, { "go", "Go" }, { "haskell", "Haskell" },
        { "html", "HTML" }, { "ini", "Properties" }, { "java", "Java" }, { "javascript", "JavaScript" },
        { "json", "JSON" }, { "jsp", "JSP" }, { "julia", "Julia" }, { "latex", "LaTeX" },
        { "less", "CSS" },                     // the CSS lexer; comment_tokens.h still gives LESS/SCSS "//"
        { "lisp", "LISP" }, { "lua", "Lua" }, { "makefile", "Makefile" }, { "markdown", "Markdown" },
        { "matlab", "MATLAB" }, { "networkd", "Properties" }, { "nim", "Nim" }, { "nix", "Nix" },
        { "nsis", "NSIS" }, { "objective_c", "Objective-C" }, { "pascal", "Pascal" }, { "perl", "Perl" },
        { "php", "PHP" }, { "pkgbuild", "Shell" }, { "powershell", "PowerShell" },
        { "props", "Properties" }, { "ps", "PostScript" }, { "python", "Python" }, { "r", "R" },
        { "rails", "Ruby" }, { "rebol", "Rebol" },
        { "rhtml", "HTML" },                   // ERB templates: the markup is what the HTML lexer can read
        { "ruby", "Ruby" }, { "rust", "Rust" }, { "sass", "CSS" }, { "scheme", "Scheme" },
        { "smalltalk", "Smalltalk" }, { "sql", "SQL" }, { "swift", "Swift" }, { "systemd", "Properties" },
        { "tcl", "TCL" }, { "toml", "TOML" }, { "txt2tags", "txt2tags" }, { "typescript", "TypeScript" },
        { "vb", "Visual Basic" }, { "verilog", "Verilog" }, { "vhdl", "VHDL" },
        { "wsf", "XML" },                      // Windows Script Files are XML
        { "xml", "XML" }, { "yaml", "YAML" }, { "zig", "Zig" },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}
inline const char* wxnLangForScintillua(const std::string& sc)
{
    std::size_t n; const WxnLangScRow* t = wxnLangScintilluaTable(n);
    for (std::size_t i = 0; i < n; ++i) if (sc == t[i].sc) return t[i].lang;
    return "";
}

// Lower-case extensions where wxNote answers differently from Scintillua. `lang` is the answer, where
// "" means the NAME decides nothing - the extension has too many owners to guess from, so only the
// content (step 5) may still place the file.
struct WxnLangExtRow { const char* ext; const char* lang; };
inline const WxnLangExtRow* wxnLangExtOverrideTable(std::size_t& n)
{
    static const WxnLangExtRow t[] = {
        // Claimed by several languages. A wrong guess recolours the file AND hands Ctrl+/ the wrong
        // comment token; comment_tokens.h and flLangKey() refuse ".m" for exactly that reason.
        { "m",   "" },                         // MATLAB / Objective-C
        { "sc",  "" },                         // Scala worksheets / SCons (Scintillua: Python)
        { "inc", "" },                         // PHP / Pascal / assembler / POV-Ray / C includes
        { "cls", "" },                         // Visual Basic class / LaTeX class
        { "p",   "" },                         // Pascal / OpenEdge ABL / Prolog
        { "asc", "" },                         // ActionScript / AsciiDoc / PGP ASCII armour
        { "sch", "" },                         // Scheme / KiCad and Eagle schematics
        { "changes", "" }, { "sources", "" },  // Smalltalk image files / Debian .changes and apt .sources
        // wxNote has a closer language than the one Scintillua names.
        { "r",   "R" },                        // Scintillua keeps lower-case .r for REBOL
        { "reg", "Registry" },                 // Scintillua: INI - wxNote has a registry lexer
        { "vbs", "VBScript" },                 // Scintillua has one VB lexer; wxNote keeps VBScript apart
        { "gd",  "GDScript" },                 // Scintillua: GAP. Godot's GDScript is the likelier file
        { "mac", "Maxima" },                   // Scintillua: ANSYS APDL, which wxNote has no lexer for
        { "vh",  "Verilog" },                  // a Verilog header (Scintillua: VHDL)
        { "h",   "C" },                        // what comment_tokens.h and the status bar call .h (same lexer and keywords as C++)
        { "f",   "Fortran (fixed form)" },     // wxNote lexes fixed and free form apart; Scintillua has one Fortran
        { "for", "Fortran (fixed form)" },
        { "f77", "Fortran (fixed form)" },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}
// The override for `lowerExt`, or nullptr when it has none.
inline const char* wxnLangExtOverride(const std::string& lowerExt)
{
    std::size_t n; const WxnLangExtRow* t = wxnLangExtOverrideTable(n);
    for (std::size_t i = 0; i < n; ++i) if (lowerExt == t[i].ext) return t[i].lang;
    return nullptr;
}

// comment_tokens.h key -> Language-menu name. The table's extension-only rows have no menu language
// of their own: INI/config files go to Properties and SCSS/LESS to CSS (the lexers that read them);
// Elixir, Scala and Groovy come back under their own names, which the caller's menu check rejects.
inline std::string wxnLangForCommentKey(const std::string& key)
{
    if (key == "ini" || key == "conf")  return "Properties";
    if (key == "scss" || key == "less") return "CSS";
    const WxnCommentLang* l = wxnCommentLangForKey(key);
    return l ? l->name : "";
}

// What a document's first bytes say about its language, as a canonical extension ("py", "sh",
// "html", "json"...), or "" for no idea. Runs after Scintillua's first-line patterns and adds what
// they lack: node and pwsh shebangs, any other interpreter as shell (shell beats plain text), an HTML
// doctype, and JSON's shape - an opening brace or bracket with a quoted key and a colon in `head`.
inline std::string wxnSniffExtFromContent(const std::string& head)
{
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
    auto trimmed = [&](const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && isSpace(s[a])) ++a;
        while (b > a && isSpace(s[b - 1])) --b;
        return s.substr(a, b - a);
    };
    auto startsWith = [](const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; };

    const std::string first = trimmed(head.substr(0, head.find_first_of("\r\n")));
    if (startsWith(first, "#!"))
    {
        std::vector<std::string> parts;               // "#!/usr/bin/env python3 -u" -> {/usr/bin/env, python3, -u}
        std::string cur;
        for (char c : first.substr(2) + " ")
        {
            if (c == ' ' || c == '\t') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        auto base = [](const std::string& s) { const std::size_t k = s.rfind('/'); return k == std::string::npos ? s : s.substr(k + 1); };
        std::string interp = parts.empty() ? std::string() : base(parts[0]);
        if (interp == "env" && parts.size() > 1) interp = base(parts[1]);
        if (startsWith(interp, "python")) return "py";
        if (startsWith(interp, "perl"))   return "pl";
        if (startsWith(interp, "ruby"))   return "rb";
        if (startsWith(interp, "node"))   return "js";
        if (startsWith(interp, "lua"))    return "lua";
        if (startsWith(interp, "pwsh"))   return "ps1";
        return "sh";                                  // sh/bash/zsh/dash/... or an unknown interpreter
    }
    const std::string t  = trimmed(head);
    const std::string tl = wxnLangLower(t.substr(0, 16));
    if (startsWith(tl, "<?xml"))                                return "xml";
    if (startsWith(tl, "<!doctype html") || startsWith(tl, "<html")) return "html";
    if (!t.empty() && (t[0] == '{' || t[0] == '[') &&
        (t.find("\":") != std::string::npos || t.find("\" :") != std::string::npos))
        return "json";
    return "";
}

// ---- extensions the user adds ------------------------------------------------------------------
//
// Three places map an extension to a language on top of the built-in tables, strongest first:
//   toLang       the Style Configurator's "User ext." field. Kept in wxNote's settings, not in the
//                theme as Notepad++ does: here the theme follows dark/light mode by itself, and the
//                light default is read-only once installed, so a per-theme list would come and go;
//   toKey        functionlist.yaml `extensions` lists, in its comment/Function List key vocabulary;
//   themeToLang  the active theme's per-lexer `extensions` lists - where Notepad++ keeps them, so a
//                theme imported from Notepad++ brings them along. Used only for an extension
//                the built-in tables leave open: a theme is about colours, and stock ones carry stray
//                values (Twilight files .po under bash and .wpl under XML).
// All keys are lower-case extensions without the dot, as wxnUserExtNormalize makes them.
struct WxnUserExtMaps
{
    const std::map<std::string, std::string>* toLang      = nullptr;   // extension -> Language-menu name
    const std::map<std::string, std::string>* toKey       = nullptr;   // extension -> comment/Function List key
    const std::map<std::string, std::string>* themeToLang = nullptr;   // extension -> Language-menu name
};

// One extension as a user types it - "inc", ".inc", "*.INC" - as the key the maps use, or "" when it
// cannot be an extension at all (a path, a wildcard, a name with a dot in it).
inline std::string wxnUserExtNormalize(std::string s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    s = s.substr(a, b - a);
    if (s.compare(0, 2, "*.") == 0) s.erase(0, 2);
    else if (!s.empty() && s[0] == '.') s.erase(0, 1);
    if (s.empty()) return std::string();
    for (char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || std::strchr(".*?/\\:\"<>| ,;", c)) return std::string();
    }
    return wxnLangLower(s);
}

// A typed list - separated by spaces, commas or semicolons - as normalised extensions in the order
// typed, each once. Whatever is not an extension is dropped.
inline std::vector<std::string> wxnUserExtParse(const std::string& list)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        const std::string e = wxnUserExtNormalize(cur);
        if (!e.empty() && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        cur.clear();
    };
    for (char c : list)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';') flush();
        else cur += c;
    }
    flush();
    return out;
}

inline std::string wxnUserExtJoin(const std::vector<std::string>& exts)
{
    std::string s;
    for (const std::string& e : exts) { if (!s.empty()) s += ' '; s += e; }
    return s;
}

// Notepad++ LexerType name -> Language-menu name. A theme block is named for Notepad++'s language, so
// this is what a block's `extensions`, or an extension typed under its Style Configurator entry, opens
// files as. "" for the blocks that are not a language of their own: the search-results and error-list
// panes, ANSI escapes, JavaScript embedded in HTML, DOS-style NFO art, and wxNote's genericLangDef.
struct WxnLangNppRow { const char* npp; const char* lang; };
inline const WxnLangNppRow* wxnLangNppTable(std::size_t& n)
{
    static const WxnLangNppRow t[] = {
        { "actionscript", "ActionScript" }, { "ada", "Ada" }, { "asm", "Assembly" }, { "asn1", "ASN.1" },
        { "asp", "ASP" }, { "autoit", "AutoIt" }, { "avs", "AviSynth" }, { "baanc", "BaanC" },
        { "bash", "Shell" }, { "batch", "Batch" }, { "blitzbasic", "BlitzBasic" }, { "c", "C" },
        { "caml", "Caml" }, { "cmake", "CMake" }, { "cobol", "COBOL" }, { "coffeescript", "CoffeeScript" },
        { "cpp", "C++" }, { "cs", "C#" }, { "csound", "Csound" }, { "css", "CSS" }, { "d", "D" },
        { "diff", "Diff" }, { "erlang", "Erlang" }, { "escript", "ESCRIPT" }, { "forth", "Forth" },
        { "fortran", "Fortran (free form)" }, { "fortran77", "Fortran (fixed form)" },
        { "freebasic", "FreeBasic" }, { "gdscript", "GDScript" }, { "go", "Go" }, { "gui4cli", "Gui4Cli" },
        { "haskell", "Haskell" }, { "hollywood", "Hollywood" }, { "html", "HTML" },
        { "ihex", "Intel HEX" }, { "inno", "Inno Setup" }, { "java", "Java" },
        { "javascript.js", "JavaScript" }, { "json", "JSON" }, { "json5", "JSON5" }, { "kix", "KIXtart" },
        { "latex", "LaTeX" }, { "lisp", "LISP" }, { "lua", "Lua" }, { "makefile", "Makefile" },
        { "matlab", "MATLAB" }, { "mmixal", "MMIXAL" }, { "mssql", "MS SQL" }, { "nim", "Nim" },
        { "nncrontab", "nnCron" }, { "nsis", "NSIS" }, { "objc", "Objective-C" },
        { "oscript", "OScript" }, { "pascal", "Pascal" }, { "perl", "Perl" }, { "php", "PHP" },
        { "postscript", "PostScript" }, { "powershell", "PowerShell" }, { "props", "Properties" },
        { "purebasic", "PureBasic" }, { "python", "Python" }, { "r", "R" }, { "raku", "Raku" },
        { "rc", "Resource file" }, { "rebol", "Rebol" }, { "registry", "Registry" }, { "ruby", "Ruby" },
        { "rust", "Rust" }, { "sas", "SAS" }, { "scheme", "Scheme" }, { "smalltalk", "Smalltalk" },
        { "spice", "SPICE" }, { "sql", "SQL" }, { "srec", "S-Record" }, { "swift", "Swift" },
        { "tcl", "TCL" }, { "tehex", "Tektronix hex" }, { "tex", "TeX" }, { "toml", "TOML" },
        { "txt2tags", "txt2tags" }, { "typescript", "TypeScript" }, { "vb", "Visual Basic" },
        { "verilog", "Verilog" }, { "vhdl", "VHDL" }, { "visualprolog", "Visual Prolog" },
        { "xml", "XML" }, { "yaml", "YAML" },
        // Not languages: panes, overlays and the shared palette.
        { "errorlist", "" }, { "escseq", "" }, { "javascript", "" }, { "nfo", "" },
        { "searchResult", "" }, { "genericLangDef", "" },
        // Nor INI here: wxNote has no INI language - .ini files open as Properties, coloured from the
        // props block - so an extension typed under "ini" would comment with Properties' '#' where .ini
        // itself takes ';' (comment_tokens.h). Extensions for these files go under "props".
        { "ini", "" },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}
inline std::string wxnLangForNppLexerType(const std::string& npp)
{
    std::size_t n; const WxnLangNppRow* t = wxnLangNppTable(n);
    for (std::size_t i = 0; i < n; ++i) if (npp == t[i].npp) return t[i].lang;
    return std::string();
}

// The Language-menu name `fileName` (a bare name, no directory) opens as, or "" for Normal Text.
//   head          the document's first bytes (the editor passes up to 512), for step 5
//   user          the user's extension mappings (see WxnUserExtMaps); all may be null
//   scDetect      (fileName, firstLine) -> Scintillua lexer name or "" (scintillua::Engine::detect)
//   knownLang     is this a wxnLangTable name?
template <class ScDetect, class KnownLang>
std::string wxnDetectLanguage(const std::string& fileName, const std::string& head,
                              const WxnUserExtMaps& user, ScDetect&& scDetect, KnownLang&& knownLang)
{
    auto usable = [&](const std::string& n) { return !n.empty() && knownLang(n); };
    auto lookup = [](const std::map<std::string, std::string>* m, const std::string& key) {
        if (!m || key.empty()) return std::string();
        const auto it = m->find(key);
        return it == m->end() ? std::string() : it->second;
    };
    const std::string name  = wxnLangStripBackup(fileName);
    const std::string lower = wxnLangLower(name);
    const std::string ext   = wxnLangExtOf(lower);

    // 1. the user's own mappings. The extension as written goes first and the one under a backup
    //    suffix second, so a user who maps "bak" itself gets what they asked for.
    for (const std::string& e : { wxnLangExtOf(wxnLangLower(fileName)), ext })
    {
        const std::string a = lookup(user.toLang, e);
        if (usable(a)) return a;
        const std::string b = wxnLangForCommentKey(lookup(user.toKey, e));
        if (usable(b)) return b;
    }

    // 2-4. the name. Once Scintillua or the comment table recognises it, their verdict is final,
    // even when wxNote cannot highlight that language.
    if (const char* o = wxnLangExtOverride(ext))
    {
        if (usable(o)) return o;
    }
    else
    {
        std::string sc = scDetect(name, std::string());
        if (sc.empty() && lower != name) sc = scDetect(lower, std::string());
        if (!sc.empty())
        {
            const std::string n = wxnLangForScintillua(sc);
            return usable(n) ? n : std::string();
        }
        const std::string key = wxnCommentLangKeyForFileName(lower);
        if (!key.empty())
        {
            const std::string n = wxnLangForCommentKey(key);
            return usable(n) ? n : std::string();
        }
    }

    // 4b. the active theme's ext attributes, only for an extension nothing above placed
    {
        const std::string t = lookup(user.themeToLang, ext);
        if (usable(t)) return t;
    }

    // 5. the content
    const std::string first = head.substr(0, head.find_first_of("\r\n"));
    if (!first.empty())
    {
        const std::string n = wxnLangForScintillua(scDetect(std::string(), first));
        if (usable(n)) return n;
    }
    const std::string sniffed = wxnSniffExtFromContent(head);
    if (!sniffed.empty())
    {
        const std::string n = wxnLangForCommentKey(wxnCommentLangKeyForFileName("x." + sniffed));
        if (usable(n)) return n;
    }
    return std::string();
}

// The extensions worth trying for wxnDefaultExtensions: `scKeys` (the keys of Scintillua's detect()
// table - extensions in any case, plus whole names such as "Makefile" and "CMakeLists.txt") and the
// override and comment tables, normalised. Dotted names drop out; sorted, each once.
inline std::vector<std::string> wxnLangCandidateExts(const std::vector<std::string>& scKeys)
{
    std::set<std::string> s;
    auto add = [&](const std::string& k) { const std::string e = wxnUserExtNormalize(k); if (!e.empty()) s.insert(e); };
    for (const std::string& k : scKeys) add(k);
    std::size_t n;
    const WxnLangExtRow* ov = wxnLangExtOverrideTable(n);
    for (std::size_t i = 0; i < n; ++i) add(ov[i].ext);
    const WxnCommentExtRow* ct = wxnCommentExtTable(n);
    for (std::size_t i = 0; i < n; ++i) add(ct[i].ext);
    return std::vector<std::string>(s.begin(), s.end());
}

// What the Style Configurator shows as "Default ext.": Language-menu name -> the extensions that open
// as it with no user mapping. Each candidate goes through the real detection as "x.<ext>", so the list
// cannot disagree with what opening such a file does. Extensions in each list are sorted.
template <class ScDetect, class KnownLang>
std::map<std::string, std::vector<std::string>> wxnDefaultExtensions(const std::vector<std::string>& candidates,
                                                                     ScDetect&& scDetect, KnownLang&& knownLang)
{
    std::map<std::string, std::vector<std::string>> out;
    for (const std::string& e : candidates)
    {
        const std::string lang = wxnDetectLanguage("x." + e, std::string(), WxnUserExtMaps{}, scDetect, knownLang);
        if (!lang.empty()) out[lang].push_back(e);
    }
    return out;
}
