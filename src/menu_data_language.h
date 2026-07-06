#pragma once
// =====================================================================
// The Language menu: SPECIAL-CASED and intentionally NOT a static
// MenuItemDef table like the other menus (see menu_model.h). Most of its
// content - the ~88 built-in languages bucketed into single-letter (A, B,
// C...) submenus - is generated at runtime from nppLangTable, exactly like
// upstream Notepad++ does. That shape doesn't fit a static array, so this
// stays a small hand-written generator function, buildLanguageMenu(), by
// deliberate design decision.
//
// nppLangTable()/nppLangFind() are relocated here VERBATIM from the old
// src/npp_menu.h so existing call sites (main.cpp's language dispatch,
// the Preferences dialog) keep working by simply switching their include.
// =====================================================================
#include "menu_model.h"
#include "menuCmdID.h"
#include <wx/menu.h>
#include <wx/intl.h>

// Notepad++'s full built-in Language list, each mapped to the Lexilla lexer that highlights it (the
// CreateLexer name, which doubles as the theme/styler key for per-token colours). Shared so it both
// POPULATES the Language menu - bucketed into A/B/C... submenus like Notepad++ - and DISPATCHES a manual
// pick (force that lexer on the active buffer). Entries are grouped contiguously by first letter so the
// submenu builder can split on letter changes. Lexers without a Lexilla module fall back to Normal Text.
struct NppLang { int id; const char* name; const char* lexer; };
inline const NppLang* nppLangTable(size_t& n)
{
    static const NppLang t[] = {
        { IDM_LANG_FLASH,        "ActionScript",         "as"           },
        { IDM_LANG_ADA,          "Ada",                  "ada"          },
        { IDM_LANG_ASN1,         "ASN.1",                "asn1"         },
        { IDM_LANG_ASP,          "ASP",                  "hypertext"    },
        { IDM_LANG_ASM,          "Assembly",             "asm"          },
        { IDM_LANG_AU3,          "AutoIt",               "au3"          },
        { IDM_LANG_AVS,          "AviSynth",             "avs"          },
        { IDM_LANG_BAANC,        "BaanC",                "baan"         },
        { IDM_LANG_BATCH,        "Batch",                "batch"        },
        { IDM_LANG_BLITZBASIC,   "BlitzBasic",           "blitzbasic"   },
        { IDM_LANG_C,            "C",                    "cpp"          },
        { IDM_LANG_CS,           "C#",                   "cpp"          },
        { IDM_LANG_CPP,          "C++",                  "cpp"          },
        { IDM_LANG_CAML,         "Caml",                 "caml"         },
        { IDM_LANG_CMAKE,        "CMake",                "cmake"        },
        { IDM_LANG_COBOL,        "COBOL",                "COBOL"        },
        { IDM_LANG_COFFEESCRIPT, "CoffeeScript",         "coffeescript" },
        { IDM_LANG_CSOUND,       "Csound",               "csound"       },
        { IDM_LANG_CSS,          "CSS",                  "css"          },
        { IDM_LANG_D,            "D",                    "d"            },
        { IDM_LANG_DIFF,         "Diff",                 "diff"         },
        { IDM_LANG_ERLANG,       "Erlang",               "erlang"       },
        { IDM_LANG_ESCRIPT,      "ESCRIPT",              "escript"      },
        { IDM_LANG_FORTH,        "Forth",                "forth"        },
        { IDM_LANG_FORTRAN_77,   "Fortran (fixed form)", "f77"          },
        { IDM_LANG_FORTRAN,      "Fortran (free form)",  "fortran"      },
        { IDM_LANG_FREEBASIC,    "FreeBasic",            "freebasic"    },
        { IDM_LANG_GDSCRIPT,     "GDScript",             "gdscript"     },
        { IDM_LANG_GOLANG,       "Go",                   "cpp"          },
        { IDM_LANG_GUI4CLI,      "Gui4Cli",              "gui4cli"      },
        { IDM_LANG_HASKELL,      "Haskell",              "haskell"      },
        { IDM_LANG_HOLLYWOOD,    "Hollywood",            "hollywood"    },
        { IDM_LANG_HTML,         "HTML",                 "hypertext"    },
        { IDM_LANG_INNO,         "Inno Setup",           "inno"         },
        { IDM_LANG_IHEX,         "Intel HEX",            "ihex"         },
        { IDM_LANG_JAVA,         "Java",                 "cpp"          },
        { IDM_LANG_JS,           "JavaScript",           "cpp"          },
        { IDM_LANG_JSON,         "JSON",                 "json"         },
        { IDM_LANG_JSON5,        "JSON5",                "json"         },
        { IDM_LANG_JSP,          "JSP",                  "hypertext"    },
        { IDM_LANG_KIX,          "KIXtart",              "kix"          },
        { IDM_LANG_LATEX,        "LaTeX",                "latex"        },
        { IDM_LANG_LISP,         "LISP",                 "lisp"         },
        { IDM_LANG_LUA,          "Lua",                  "lua"          },
        { IDM_LANG_MAKEFILE,     "Makefile",             "makefile"     },
        { IDM_LANG_MATLAB,       "MATLAB",               "matlab"       },
        { IDM_LANG_MMIXAL,       "MMIXAL",               "mmixal"       },
        { IDM_LANG_MSSQL,        "MS SQL",               "mssql"        },
        { IDM_LANG_NIM,          "Nim",                  "nim"          },
        { IDM_LANG_NNCRONTAB,    "nnCron",               "nncrontab"    },
        { IDM_LANG_NSIS,         "NSIS",                 "nsis"         },
        { IDM_LANG_OBJC,         "Objective-C",          "cpp"          },
        { IDM_LANG_OSCRIPT,      "OScript",              "oscript"      },
        { IDM_LANG_PASCAL,       "Pascal",               "pascal"       },
        { IDM_LANG_PERL,         "Perl",                 "perl"         },
        { IDM_LANG_PHP,          "PHP",                  "hypertext"    },
        { IDM_LANG_PS,           "PostScript",           "ps"           },
        { IDM_LANG_POWERSHELL,   "PowerShell",           "powershell"   },
        { IDM_LANG_PROPS,        "Properties",           "props"        },
        { IDM_LANG_PUREBASIC,    "PureBasic",            "purebasic"    },
        { IDM_LANG_PYTHON,       "Python",               "python"       },
        { IDM_LANG_R,            "R",                    "r"            },
        { IDM_LANG_RAKU,         "Raku",                 "raku"         },
        { IDM_LANG_REBOL,        "Rebol",                "rebol"        },
        { IDM_LANG_REGISTRY,     "Registry",             "registry"     },
        { IDM_LANG_RC,           "Resource file",        "cpp"          },
        { IDM_LANG_RUBY,         "Ruby",                 "ruby"         },
        { IDM_LANG_RUST,         "Rust",                 "rust"         },
        { IDM_LANG_SAS,          "SAS",                  "sas"          },
        { IDM_LANG_SCHEME,       "Scheme",               "lisp"         },
        { IDM_LANG_BASH,         "Shell",                "bash"         },
        { IDM_LANG_SMALLTALK,    "Smalltalk",            "smalltalk"    },
        { IDM_LANG_SPICE,        "SPICE",                "spice"        },
        { IDM_LANG_SQL,          "SQL",                  "sql"          },
        { IDM_LANG_SREC,         "S-Record",             "srec"         },
        { IDM_LANG_SWIFT,        "Swift",                "cpp"          },
        { IDM_LANG_TCL,          "TCL",                  "tcl"          },
        { IDM_LANG_TEHEX,        "Tektronix hex",        "tehex"        },
        { IDM_LANG_TEX,          "TeX",                  "tex"          },
        { IDM_LANG_TOML,         "TOML",                 "toml"         },
        { IDM_LANG_TXT2TAGS,     "txt2tags",             "txt2tags"     },
        { IDM_LANG_TYPESCRIPT,   "TypeScript",           "cpp"          },
        { IDM_LANG_VERILOG,      "Verilog",              "verilog"      },
        { IDM_LANG_VHDL,         "VHDL",                 "vhdl"         },
        { IDM_LANG_VB,           "Visual Basic",         "vb"           },
        { IDM_LANG_VISUALPROLOG, "Visual Prolog",        "visualprolog" },
        { IDM_LANG_XML,          "XML",                  "xml"          },
        { IDM_LANG_YAML,         "YAML",                 "yaml"         },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}
inline const NppLang* nppLangFind(int id)
{
    size_t n; const NppLang* t = nppLangTable(n);
    for (size_t i = 0; i < n; ++i) if (t[i].id == id) return &t[i];
    return nullptr;
}

// The only static/fixed labels in this menu - the ~88 per-language names come straight from
// nppLangTable's own `name` field, which is NOT translated today (language names like "Python",
// "JavaScript" are used as-is) and must stay that way; see buildLanguageMenu() below.
namespace Label
{
    inline const wxString LanguageNone() { return _("None (Normal Text)"); }
    inline const wxString MenuLanguage() { return _("&Language"); }
    inline const wxString LanguageUserDefinedSubmenu() { return _("User Defined Language"); }
    inline const wxString LanguageDefineYourLanguage() { return _("Define your language..."); }
    inline const wxString LanguageOpenUdlDir() { return _("Open User Defined Language folder..."); }
    inline const wxString LanguageUdlCollectionSite() { return _("Notepad++ User Defined Languages Collection"); }
    inline const wxString LanguageUserDefined() { return _("User-Defined"); }
}

// ------------------------------------------------------------- Language
// The full Notepad++ language list, bucketed into single-letter submenus (A, B, C, ...) exactly like
// Notepad++. The shared nppLangTable is grouped contiguously by first letter (see the top of this
// file), so a new submenu starts whenever the first letter changes.
inline wxMenu* buildLanguageMenu(class MenuRegistry& reg)
{
    auto* lang = new wxMenu;
    lang->Append(IDM_LANG_TEXT, Label::LanguageNone());
    lang->AppendSeparator();
    {
        size_t ln; const NppLang* lt = nppLangTable(ln);
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
        sub->Append(IDM_LANG_USER_DLG, Label::LanguageDefineYourLanguage());
        sub->Append(IDM_LANG_OPENUDLDIR, Label::LanguageOpenUdlDir());
        sub->Append(IDM_LANG_UDLCOLLECTION_PROJECT_SITE, Label::LanguageUdlCollectionSite());
        lang->AppendSubMenu(sub, Label::LanguageUserDefinedSubmenu());
    }
    lang->Append(IDM_LANG_USER, Label::LanguageUserDefined());

    reg.registerMenu("menu.language", lang, /* barPosition filled in by the caller */ -1);
    return lang;
}
