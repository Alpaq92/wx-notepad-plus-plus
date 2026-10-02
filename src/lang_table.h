#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// lang_table - the Language menu's languages: each one's name and the Lexilla lexer that highlights it.
// No wx, so what has no business linking wx can know the languages too - the GPL npp-compat package,
// which maps Notepad++'s keyword lists onto them, and the tests. menu_data_language.h builds the menu
// from it.

#include "command_ids.h"

#include <cstddef>
#include <string>

// The app's full built-in Language list, each mapped to the Lexilla lexer that highlights it (the
// CreateLexer name, which doubles as the theme/styler key for per-token colours). Shared so it both
// POPULATES the Language menu - bucketed into A/B/C... submenus - and DISPATCHES a manual
// pick (force that lexer on the active buffer). Entries are grouped contiguously by first letter so the
// submenu builder can split on letter changes. Lexers without a Lexilla module fall back to Normal Text.
struct WxnLang { int id; const char* name; const char* lexer; };
inline const WxnLang* wxnLangTable(size_t& n)
{
    static const WxnLang t[] = {
        { kCmdLangAbl,           "ABL (OpenEdge)",        "abl"          },
        // "cpp", as in Notepad++. Lexilla's "as" is the GNU ASSEMBLER lexer (LexAsm.cxx), not ActionScript.
        { kCmdLangFlash,         "ActionScript",          "cpp"          },
        { kCmdLangAda,           "Ada",                   "ada"          },
        { kCmdLangAsciidoc,      "AsciiDoc",              "asciidoc"     },
        { kCmdLangAsn1,          "ASN.1",                 "asn1"         },
        { kCmdLangAsp,           "ASP",                   "hypertext"    },
        { kCmdLangAsm,           "Assembly",              "asm"          },
        { kCmdLangAu3,           "AutoIt",                "au3"          },
        { kCmdLangAvs,           "AviSynth",              "avs"          },
        { kCmdLangBaanc,         "BaanC",                 "baan"         },
        { kCmdLangBatch,         "Batch",                 "batch"        },
        { kCmdLangBibtex,        "BibTeX",                "bib"          },
        { kCmdLangBlitzbasic,    "BlitzBasic",            "blitzbasic"   },
        { kCmdLangC,             "C",                     "cpp"          },
        { kCmdLangCs,            "C#",                    "cpp"          },
        { kCmdLangCpp,           "C++",                   "cpp"          },
        { kCmdLangCaml,          "Caml",                  "caml"         },
        { kCmdLangCil,           "CIL",                   "cil"          },
        { kCmdLangClarion,       "Clarion",               "clarionnocase" },
        { kCmdLangCmake,         "CMake",                 "cmake"        },
        { kCmdLangCobol,         "COBOL",                 "COBOL"        },
        { kCmdLangCoffeeScript,  "CoffeeScript",          "coffeescript" },
        { kCmdLangCsound,        "Csound",                "csound"       },
        { kCmdLangCss,           "CSS",                   "css"          },
        { kCmdLangD,             "D",                     "d"            },
        { kCmdLangDart,          "Dart",                  "dart"         },
        { kCmdLangDataflex,      "DataFlex",              "dataflex"     },
        { kCmdLangDiff,          "Diff",                  "diff"         },
        { kCmdLangDockerfile,    "Dockerfile",            "bash"         },
        { kCmdLangEiffel,        "Eiffel",                "eiffel"       },
        { kCmdLangErlang,        "Erlang",                "erlang"       },
        { kCmdLangEscript,       "ESCRIPT",               "escript"      },
        { kCmdLangFsharp,        "F#",                    "fsharp"       },
        { kCmdLangForth,         "Forth",                 "forth"        },
        { kCmdLangFortran77,     "Fortran (fixed form)",  "f77"          },
        { kCmdLangFortran,       "Fortran (free form)",   "fortran"      },
        { kCmdLangFreebasic,     "FreeBasic",             "freebasic"    },
        { kCmdLangGdscript,      "GDScript",              "gdscript"     },
        { kCmdLangGettextpo,     "gettext PO",            "po"           },
        { kCmdLangGolang,        "Go",                    "cpp"          },
        { kCmdLangGui4cli,       "Gui4Cli",               "gui4cli"      },
        { kCmdLangHaskell,       "Haskell",               "haskell"      },
        { kCmdLangHollywood,     "Hollywood",             "hollywood"    },
        { kCmdLangHtml,          "HTML",                  "hypertext"    },
        { kCmdLangInno,          "Inno Setup",            "inno"         },
        { kCmdLangIhex,          "Intel HEX",             "ihex"         },
        { kCmdLangJava,          "Java",                  "cpp"          },
        { kCmdLangJs,            "JavaScript",            "cpp"          },
        { kCmdLangJson,          "JSON",                  "json"         },
        { kCmdLangJson5,         "JSON5",                 "json"         },
        { kCmdLangJsp,           "JSP",                   "hypertext"    },
        { kCmdLangJulia,         "Julia",                 "julia"        },
        { kCmdLangKix,           "KIXtart",               "kix"          },
        // Kotlin already had everything BUT this row: keywords, autocomplete, function-list rules,
        // .kt/.kts detection and the raw-string-aware comment mask. Without the row it could only ever
        // be reached by opening a file with the right extension - never chosen for a buffer that has
        // the wrong one, and never shown as the active language. Lexer "cpp", same as Swift/TypeScript.
        { kCmdLangKotlin,        "Kotlin",                "cpp"          },
        { kCmdLangLatex,         "LaTeX",                 "latex"        },
        { kCmdLangLisp,          "LISP",                  "lisp"         },
        { kCmdLangLua,           "Lua",                   "lua"          },
        { kCmdLangMakefile,      "Makefile",              "makefile"     },
        { kCmdLangMarkdown,      "Markdown",              "markdown"     },
        { kCmdLangMatlab,        "MATLAB",                "matlab"       },
        { kCmdLangMaxima,        "Maxima",                "maxima"       },
        { kCmdLangMetapost,      "MetaPost",              "metapost"     },
        { kCmdLangMmixal,        "MMIXAL",                "mmixal"       },
        { kCmdLangModula,        "Modula-3",              "modula"       },
        { kCmdLangMssql,         "MS SQL",                "mssql"        },
        { kCmdLangMysql,         "MySQL",                 "mysql"        },
        { kCmdLangNim,           "Nim",                   "nim"          },
        { kCmdLangNix,           "Nix",                   "nix"          },
        { kCmdLangNncrontab,     "nnCron",                "nncrontab"    },
        { kCmdLangNsis,          "NSIS",                  "nsis"         },
        { kCmdLangObjc,          "Objective-C",           "cpp"          },
        { kCmdLangOctave,        "Octave",                "octave"       },
        { kCmdLangOscript,       "OScript",               "oscript"      },
        { kCmdLangPascal,        "Pascal",                "pascal"       },
        { kCmdLangPerl,          "Perl",                  "perl"         },
        { kCmdLangPhp,           "PHP",                   "hypertext"    },
        { kCmdLangPs,            "PostScript",            "ps"           },
        { kCmdLangPovray,        "POV-Ray",               "pov"          },
        { kCmdLangPowershell,    "PowerShell",            "powershell"   },
        { kCmdLangProps,         "Properties",            "props"        },
        { kCmdLangPurebasic,     "PureBasic",             "purebasic"    },
        { kCmdLangPython,        "Python",                "python"       },
        { kCmdLangR,             "R",                     "r"            },
        { kCmdLangRaku,          "Raku",                  "raku"         },
        { kCmdLangRebol,         "Rebol",                 "rebol"        },
        { kCmdLangRegistry,      "Registry",              "registry"     },
        { kCmdLangRc,            "Resource file",         "cpp"          },
        { kCmdLangRuby,          "Ruby",                  "ruby"         },
        { kCmdLangRust,          "Rust",                  "rust"         },
        { kCmdLangSas,           "SAS",                   "sas"          },
        { kCmdLangScheme,        "Scheme",                "lisp"         },
        { kCmdLangBash,          "Shell",                 "bash"         },
        { kCmdLangSmalltalk,     "Smalltalk",             "smalltalk"    },
        { kCmdLangSpice,         "SPICE",                 "spice"        },
        { kCmdLangSql,           "SQL",                   "sql"          },
        { kCmdLangSrec,          "S-Record",              "srec"         },
        { kCmdLangStata,         "Stata",                 "stata"        },
        { kCmdLangSwift,         "Swift",                 "cpp"          },
        { kCmdLangTcl,           "TCL",                   "tcl"          },
        { kCmdLangTehex,         "Tektronix hex",         "tehex"        },
        { kCmdLangTex,           "TeX",                   "tex"          },
        { kCmdLangToml,          "TOML",                  "toml"         },
        { kCmdLangTxt2tags,      "txt2tags",              "txt2tags"     },
        { kCmdLangTypescript,    "TypeScript",            "cpp"          },
        { kCmdLangVbscript,      "VBScript",              "vbscript"     },
        { kCmdLangVerilog,       "Verilog",               "verilog"      },
        { kCmdLangVhdl,          "VHDL",                  "vhdl"         },
        { kCmdLangVb,            "Visual Basic",          "vb"           },
        { kCmdLangVisualProlog,  "Visual Prolog",         "visualprolog" },
        { kCmdLangXml,           "XML",                   "xml"          },
        { kCmdLangYaml,          "YAML",                  "yaml"         },
        { kCmdLangZig,           "Zig",                   "zig"          },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}
inline const WxnLang* wxnLangFind(int id)
{
    size_t n; const WxnLang* t = wxnLangTable(n);
    for (size_t i = 0; i < n; ++i) if (t[i].id == id) return &t[i];
    return nullptr;
}
// By the `name` column, exactly as spelled there (lang_detect.h answers in these names).
inline const WxnLang* wxnLangFindByName(const std::string& name)
{
    size_t n; const WxnLang* t = wxnLangTable(n);
    for (size_t i = 0; i < n; ++i) if (name == t[i].name) return &t[i];
    return nullptr;
}
// The name `written` stands for, matched without regard to case (as a hand-written languages.yaml
// spells it); "" for none.
inline std::string wxnLangCanonicalName(const std::string& written)
{
    auto lower = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; };
    const std::string w = lower(written);
    size_t n; const WxnLang* t = wxnLangTable(n);
    for (size_t i = 0; i < n; ++i) if (lower(t[i].name) == w) return std::string(t[i].name);
    return std::string();
}
// The Lexilla lexer of the language called `name`; "" for none.
inline std::string wxnLangLexerOf(const std::string& name)
{
    const WxnLang* l = wxnLangFindByName(name);
    return l ? std::string(l->lexer) : std::string();
}
