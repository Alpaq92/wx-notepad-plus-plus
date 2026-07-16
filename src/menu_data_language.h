#pragma once
// =====================================================================
// The Language menu: SPECIAL-CASED and intentionally NOT a static
// MenuItemDef table like the other menus (see menu_model.h). Most of its
// content - the ~88 built-in languages bucketed into single-letter (A, B,
// C...) submenus - is generated at runtime from wxnLangTable. That shape
// doesn't fit a static array, so this stays a small hand-written generator
// function, buildLanguageMenu(), by deliberate design decision.
//
// wxnLangTable()/wxnLangFind() are relocated here VERBATIM from the old
// inline menu construction so existing call sites (main.cpp's language
// dispatch, the Preferences dialog) keep working by simply switching their
// include.
// =====================================================================
#include "menu_model.h"
#include "command_ids.h"
#include <wx/menu.h>
#include <wx/intl.h>

// The app's full built-in Language list, each mapped to the Lexilla lexer that highlights it (the
// CreateLexer name, which doubles as the theme/styler key for per-token colours). Shared so it both
// POPULATES the Language menu - bucketed into A/B/C... submenus - and DISPATCHES a manual
// pick (force that lexer on the active buffer). Entries are grouped contiguously by first letter so the
// submenu builder can split on letter changes. Lexers without a Lexilla module fall back to Normal Text.
struct WxnLang { int id; const char* name; const char* lexer; };
inline const WxnLang* wxnLangTable(size_t& n)
{
    static const WxnLang t[] = {
        { kCmdLangFlash,        "ActionScript",         "as"           },
        { kCmdLangAda,          "Ada",                  "ada"          },
        { kCmdLangAsn1,         "ASN.1",                "asn1"         },
        { kCmdLangAsp,          "ASP",                  "hypertext"    },
        { kCmdLangAsm,          "Assembly",             "asm"          },
        { kCmdLangAu3,          "AutoIt",               "au3"          },
        { kCmdLangAvs,          "AviSynth",             "avs"          },
        { kCmdLangBaanc,        "BaanC",                "baan"         },
        { kCmdLangBatch,        "Batch",                "batch"        },
        { kCmdLangBlitzbasic,   "BlitzBasic",           "blitzbasic"   },
        { kCmdLangC,            "C",                    "cpp"          },
        { kCmdLangCs,           "C#",                   "cpp"          },
        { kCmdLangCpp,          "C++",                  "cpp"          },
        { kCmdLangCaml,         "Caml",                 "caml"         },
        { kCmdLangCmake,        "CMake",                "cmake"        },
        { kCmdLangCobol,        "COBOL",                "COBOL"        },
        { kCmdLangCoffeeScript, "CoffeeScript",         "coffeescript" },
        { kCmdLangCsound,       "Csound",               "csound"       },
        { kCmdLangCss,          "CSS",                  "css"          },
        { kCmdLangD,            "D",                    "d"            },
        { kCmdLangDiff,         "Diff",                 "diff"         },
        { kCmdLangErlang,       "Erlang",               "erlang"       },
        { kCmdLangEscript,      "ESCRIPT",              "escript"      },
        { kCmdLangForth,        "Forth",                "forth"        },
        { kCmdLangFortran77,   "Fortran (fixed form)", "f77"          },
        { kCmdLangFortran,      "Fortran (free form)",  "fortran"      },
        { kCmdLangFreebasic,    "FreeBasic",            "freebasic"    },
        { kCmdLangGdscript,     "GDScript",             "gdscript"     },
        { kCmdLangGolang,       "Go",                   "cpp"          },
        { kCmdLangGui4cli,      "Gui4Cli",              "gui4cli"      },
        { kCmdLangHaskell,      "Haskell",              "haskell"      },
        { kCmdLangHollywood,    "Hollywood",            "hollywood"    },
        { kCmdLangHtml,         "HTML",                 "hypertext"    },
        { kCmdLangInno,         "Inno Setup",           "inno"         },
        { kCmdLangIhex,         "Intel HEX",            "ihex"         },
        { kCmdLangJava,         "Java",                 "cpp"          },
        { kCmdLangJs,           "JavaScript",           "cpp"          },
        { kCmdLangJson,         "JSON",                 "json"         },
        { kCmdLangJson5,        "JSON5",                "json"         },
        { kCmdLangJsp,          "JSP",                  "hypertext"    },
        { kCmdLangKix,          "KIXtart",              "kix"          },
        { kCmdLangLatex,        "LaTeX",                "latex"        },
        { kCmdLangLisp,         "LISP",                 "lisp"         },
        { kCmdLangLua,          "Lua",                  "lua"          },
        { kCmdLangMakefile,     "Makefile",             "makefile"     },
        { kCmdLangMatlab,       "MATLAB",               "matlab"       },
        { kCmdLangMmixal,       "MMIXAL",               "mmixal"       },
        { kCmdLangMssql,        "MS SQL",               "mssql"        },
        { kCmdLangNim,          "Nim",                  "nim"          },
        { kCmdLangNncrontab,    "nnCron",               "nncrontab"    },
        { kCmdLangNsis,         "NSIS",                 "nsis"         },
        { kCmdLangObjc,         "Objective-C",          "cpp"          },
        { kCmdLangOscript,      "OScript",              "oscript"      },
        { kCmdLangPascal,       "Pascal",               "pascal"       },
        { kCmdLangPerl,         "Perl",                 "perl"         },
        { kCmdLangPhp,          "PHP",                  "hypertext"    },
        { kCmdLangPs,           "PostScript",           "ps"           },
        { kCmdLangPowershell,   "PowerShell",           "powershell"   },
        { kCmdLangProps,        "Properties",           "props"        },
        { kCmdLangPurebasic,    "PureBasic",            "purebasic"    },
        { kCmdLangPython,       "Python",               "python"       },
        { kCmdLangR,            "R",                    "r"            },
        { kCmdLangRaku,         "Raku",                 "raku"         },
        { kCmdLangRebol,        "Rebol",                "rebol"        },
        { kCmdLangRegistry,     "Registry",             "registry"     },
        { kCmdLangRc,           "Resource file",        "cpp"          },
        { kCmdLangRuby,         "Ruby",                 "ruby"         },
        { kCmdLangRust,         "Rust",                 "rust"         },
        { kCmdLangSas,          "SAS",                  "sas"          },
        { kCmdLangScheme,       "Scheme",               "lisp"         },
        { kCmdLangBash,         "Shell",                "bash"         },
        { kCmdLangSmalltalk,    "Smalltalk",            "smalltalk"    },
        { kCmdLangSpice,        "SPICE",                "spice"        },
        { kCmdLangSql,          "SQL",                  "sql"          },
        { kCmdLangSrec,         "S-Record",             "srec"         },
        { kCmdLangSwift,        "Swift",                "cpp"          },
        { kCmdLangTcl,          "TCL",                  "tcl"          },
        { kCmdLangTehex,        "Tektronix hex",        "tehex"        },
        { kCmdLangTex,          "TeX",                  "tex"          },
        { kCmdLangToml,         "TOML",                 "toml"         },
        { kCmdLangTxt2tags,     "txt2tags",             "txt2tags"     },
        { kCmdLangTypescript,   "TypeScript",           "cpp"          },
        { kCmdLangVerilog,      "Verilog",              "verilog"      },
        { kCmdLangVhdl,         "VHDL",                 "vhdl"         },
        { kCmdLangVb,           "Visual Basic",         "vb"           },
        { kCmdLangVisualProlog, "Visual Prolog",        "visualprolog" },
        { kCmdLangXml,          "XML",                  "xml"          },
        { kCmdLangYaml,         "YAML",                 "yaml"         },
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

// The only static/fixed labels in this menu - the ~88 per-language names come straight from
// wxnLangTable's own `name` field, which is NOT translated today (language names like "Python",
// "JavaScript" are used as-is) and must stay that way; see buildLanguageMenu() below.
namespace Label
{
    inline const wxString LanguageNone() { return _("None (Normal Text)"); }
    inline const wxString MenuLanguage() { return _("&Language"); }
    inline const wxString LanguageUserDefinedSubmenu() { return _("User Defined Language"); }
    inline const wxString LanguageOpenUdlDir() { return _("Open User Defined Language folder..."); }
    inline const wxString LanguageUserDefined() { return _("User-Defined"); }
}

// ------------------------------------------------------------- Language
// The full built-in language list, bucketed alphabetically into single-letter submenus (A, B, C,
// ...). The shared wxnLangTable is grouped contiguously by first letter (see the top of this
// file), so a new submenu starts whenever the first letter changes.
inline wxMenu* buildLanguageMenu(class MenuRegistry& reg)
{
    auto* lang = new wxMenu;
    lang->Append(kCmdLangText, Label::LanguageNone());
    lang->AppendSeparator();
    {
        size_t ln; const WxnLang* lt = wxnLangTable(ln);
        wxMenu* sub = nullptr; char cur = 0;
        for (size_t i = 0; i < ln; ++i)
        {
            char first = lt[i].name[0];
            if (first >= 'a' && first <= 'z') first = (char)(first - 'a' + 'A');
            if (first != cur) { sub = new wxMenu; lang->AppendSubMenu(sub, wxString(wxUniChar(first))); cur = first; }
            sub->Append(lt[i].id, lt[i].name);
        }
    }
    lang->AppendSeparator();
    {
        auto* sub = new wxMenu;
        sub->Append(kCmdLangOpenudldir, Label::LanguageOpenUdlDir());
        lang->AppendSubMenu(sub, Label::LanguageUserDefinedSubmenu());
    }
    lang->Append(kCmdLangUser, Label::LanguageUserDefined());

    reg.registerMenu("menu.language", lang, /* barPosition filled in by the caller */ -1);
    return lang;
}
