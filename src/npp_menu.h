#pragma once
// =====================================================================
// Notepad++ main menu - faithful 1:1 reproduction of IDR_M30_MENU from
// PowerEditor/src/Notepad_plus.rc (every top-level popup, item, submenu,
// separator and mnemonic). Built data-style into a wxMenuBar.
//
// The .rc menu text carries no shortcuts (Notepad++ injects them at runtime
// from its accelerator table); we add the standard defaults to the common
// commands here, so the keyboard shortcuts both WORK and SHOW in the menu,
// mirroring the real app. Items needing the full app are still listed (they
// report themselves via the dispatcher's default case when invoked).
//
// "&&" in a label is a literal ampersand (wx escaping), matching the .rc "&&".
// =====================================================================
#include <wx/menu.h>
#include <cstddef>
#include "menuCmdID.h"

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

// darkModeId: our extra restart-to-apply "Dark Mode" toggle (myID_DARKMODE),
// added under Settings (real Notepad++ keeps dark mode under Settings too).
inline void buildNppMainMenu(wxMenuBar* mb, int darkModeId)
{
    // ----------------------------------------------------------------- File
    {
        auto* file = new wxMenu;
        file->Append(IDM_FILE_NEW, _("&New\tCtrl+N"));
        file->Append(IDM_FILE_OPEN, _("&Open...\tCtrl+O"));
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FILE_OPEN_FOLDER, _("Explorer"));
            sub->Append(IDM_FILE_OPEN_CMD, _("cmd"));
            sub->Append(IDM_FILE_OPEN_POWERSHELL, _("PowerShell"));
            sub->AppendSeparator();
            sub->Append(IDM_FILE_CONTAININGFOLDERASWORKSPACE, _("Folder as Workspace"));
            file->AppendSubMenu(sub, _("Open Containing &Folder"));
        }
        file->Append(IDM_FILE_OPEN_DEFAULT_VIEWER, _("Open in &Default Viewer"));
        file->Append(IDM_FILE_OPENFOLDERASWORKSPACE, _("Open Folder as &Workspace..."));
        file->Append(IDM_FILE_RELOAD, _("Re&load from Disk"));
        file->Append(IDM_FILE_SAVE, _("&Save\tCtrl+S"));
        file->Append(IDM_FILE_SAVEAS, _("Save &As...\tCtrl+Alt+S"));
        file->Append(IDM_FILE_SAVECOPYAS, _("Save a Cop&y As..."));
        file->Append(IDM_FILE_SAVEALL, _("Sa&ve All\tCtrl+Shift+S"));
        file->Append(IDM_FILE_RENAME, _("&Rename..."));
        file->Append(IDM_FILE_CLOSE, _("&Close\tCtrl+W"));
        file->Append(IDM_FILE_CLOSEALL, _("Clos&e All\tCtrl+Shift+W"));
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FILE_CLOSEALL_BUT_CURRENT, _("Close All but Active Document"));
            sub->Append(IDM_FILE_CLOSEALL_BUT_PINNED, _("Close All but Pinned Documents"));
            sub->Append(IDM_FILE_CLOSEALL_TOLEFT, _("Close All to the Left"));
            sub->Append(IDM_FILE_CLOSEALL_TORIGHT, _("Close All to the Right"));
            sub->Append(IDM_FILE_CLOSEALL_UNCHANGED, _("Close All Unchanged"));
            file->AppendSubMenu(sub, _("Close &Multiple Documents"));
        }
        file->Append(IDM_FILE_RESTORELASTCLOSEDFILE, _("Restore Recent Closed File\tCtrl+Shift+T"));
        file->Append(IDM_FILE_DELETE, _("Move to Recycle &Bin"));
        file->AppendSeparator();
        file->Append(IDM_FILE_LOADSESSION, _("Load Sess&ion..."));
        file->Append(IDM_FILE_SAVESESSION, _("Save Sess&ion..."));
        file->AppendSeparator();
        file->Append(IDM_FILE_PRINT, _("&Print...\tCtrl+P"));
        file->Append(IDM_FILE_PRINTNOW, _("Print No&w"));
        file->AppendSeparator();
        // The "Recent Files" submenu (wxFileHistory) is inserted here at runtime by buildMenuBar().
        file->Append(IDM_FILE_EXIT, _("E&xit\tAlt+F4"));
        mb->Append(file, _("&File"));
    }

    // ----------------------------------------------------------------- Edit
    {
        auto* edit = new wxMenu;
        edit->Append(IDM_EDIT_UNDO, _("&Undo\tCtrl+Z"));
        edit->Append(IDM_EDIT_REDO, _("&Redo\tCtrl+Y"));
        edit->AppendSeparator();
        edit->Append(IDM_EDIT_CUT, _("Cu&t\tCtrl+X"));
        edit->Append(IDM_EDIT_COPY, _("&Copy\tCtrl+C"));
        edit->Append(IDM_EDIT_PASTE, _("&Paste\tCtrl+V"));
        edit->Append(IDM_EDIT_DELETE, _("&Delete\tDel"));
        edit->Append(IDM_EDIT_SELECTALL, _("&Select All\tCtrl+A"));
        edit->Append(IDM_EDIT_BEGINENDSELECT, _("Begin/End &Select"));
        edit->Append(IDM_EDIT_BEGINENDSELECT_COLUMNMODE, _("Begin/End Select in Column Mode"));
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_INSERT_DATETIME_SHORT, _("Date Time (short)"));
            sub->Append(IDM_EDIT_INSERT_DATETIME_LONG, _("Date Time (long)"));
            sub->Append(IDM_EDIT_INSERT_DATETIME_CUSTOMIZED, _("Date Time (customized)"));
            edit->AppendSubMenu(sub, _("Insert"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_FULLPATHTOCLIP, _("Copy Current Full File path"));
            sub->Append(IDM_EDIT_FILENAMETOCLIP, _("Copy Current Filename"));
            sub->Append(IDM_EDIT_CURRENTDIRTOCLIP, _("Copy Current Dir. Path"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_COPY_ALL_NAMES, _("Copy All Filenames"));
            sub->Append(IDM_EDIT_COPY_ALL_PATHS, _("Copy All File Paths"));
            edit->AppendSubMenu(sub, _("Cop&y to Clipboard"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_INS_TAB, _("Increase Line Indent"));
            sub->Append(IDM_EDIT_RMV_TAB, _("Decrease Line Indent"));
            edit->AppendSubMenu(sub, _("&Indent"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_UPPERCASE, _("&UPPERCASE\tCtrl+Shift+U"));
            sub->Append(IDM_EDIT_LOWERCASE, _("&lowercase\tCtrl+U"));
            sub->Append(IDM_EDIT_PROPERCASE_FORCE, _("&Proper Case"));
            sub->Append(IDM_EDIT_PROPERCASE_BLEND, _("Proper Case (blend)"));
            sub->Append(IDM_EDIT_SENTENCECASE_FORCE, _("&Sentence case"));
            sub->Append(IDM_EDIT_SENTENCECASE_BLEND, _("Sentence case (blend)"));
            sub->Append(IDM_EDIT_INVERTCASE, _("&iNVERT cASE"));
            sub->Append(IDM_EDIT_RANDOMCASE, _("&ranDOm CasE"));
            edit->AppendSubMenu(sub, _("Con&vert Case to"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_DUP_LINE, _("Duplicate Current Line\tCtrl+D"));
            sub->Append(IDM_EDIT_REMOVE_ANY_DUP_LINES, _("Remove Duplicate Lines"));
            sub->Append(IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES, _("Remove Consecutive Duplicate Lines"));
            sub->Append(IDM_EDIT_SPLIT_LINES, _("Split Lines\tCtrl+I"));
            sub->Append(IDM_EDIT_JOIN_LINES, _("Join Lines\tCtrl+J"));
            sub->Append(IDM_EDIT_LINE_UP, _("Move Up Current Line\tCtrl+Shift+Up"));
            sub->Append(IDM_EDIT_LINE_DOWN, _("Move Down Current Line\tCtrl+Shift+Down"));
            sub->Append(IDM_EDIT_REMOVEEMPTYLINES, _("Remove Empty Lines"));
            sub->Append(IDM_EDIT_REMOVEEMPTYLINESWITHBLANK, _("Remove Empty Lines (Containing Blank characters)"));
            sub->Append(IDM_EDIT_BLANKLINEABOVECURRENT, _("Insert Blank Line Above Current"));
            sub->Append(IDM_EDIT_BLANKLINEBELOWCURRENT, _("Insert Blank Line Below Current"));
            sub->Append(IDM_EDIT_SORTLINES_REVERSE_ORDER, _("Reverse Line Order"));
            sub->Append(IDM_EDIT_SORTLINES_RANDOMLY, _("Randomize Line Order"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING, _("Sort Lines Lexicographically Ascending"));
            sub->Append(IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING, _("Sort Lines Lex. Ascending Ignoring Case"));
            sub->Append(IDM_EDIT_SORTLINES_LOCALE_ASCENDING, _("Sort Lines In Locale Order Ascending"));
            sub->Append(IDM_EDIT_SORTLINES_INTEGER_ASCENDING, _("Sort Lines As Integers Ascending"));
            sub->Append(IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING, _("Sort Lines As Decimals (Comma) Ascending"));
            sub->Append(IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING, _("Sort Lines As Decimals (Dot) Ascending"));
            sub->Append(IDM_EDIT_SORTLINES_LENGTH_ASCENDING, _("Sort Lines By Length Ascending"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING, _("Sort Lines Lexicographically Descending"));
            sub->Append(IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING, _("Sort Lines Lex. Descending Ignoring Case"));
            sub->Append(IDM_EDIT_SORTLINES_LOCALE_DESCENDING, _("Sort Lines In Locale Order Descending"));
            sub->Append(IDM_EDIT_SORTLINES_INTEGER_DESCENDING, _("Sort Lines As Integers Descending"));
            sub->Append(IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING, _("Sort Lines As Decimals (Comma) Descending"));
            sub->Append(IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING, _("Sort Lines As Decimals (Dot) Descending"));
            sub->Append(IDM_EDIT_SORTLINES_LENGTH_DESCENDING, _("Sort Lines By Length Descending"));
            edit->AppendSubMenu(sub, _("&Line Operations"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_BLOCK_COMMENT, _("Toggle Single Line Comment\tCtrl+Q"));
            sub->Append(IDM_EDIT_BLOCK_COMMENT_SET, _("Single Line Comment"));
            sub->Append(IDM_EDIT_BLOCK_UNCOMMENT, _("Single Line Uncomment"));
            sub->Append(IDM_EDIT_STREAM_COMMENT, _("Block Comment\tCtrl+Shift+Q"));
            sub->Append(IDM_EDIT_STREAM_UNCOMMENT, _("Block Uncomment"));
            edit->AppendSubMenu(sub, _("Co&mment/Uncomment"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_AUTOCOMPLETE, _("Function Completion\tCtrl+Space"));
            sub->Append(IDM_EDIT_AUTOCOMPLETE_CURRENTFILE, _("Word Completion"));
            sub->Append(IDM_EDIT_FUNCCALLTIP, _("Function Parameters Hint"));
            sub->Append(IDM_EDIT_FUNCCALLTIP_PREVIOUS, _("Function Parameters Previous Hint"));
            sub->Append(IDM_EDIT_FUNCCALLTIP_NEXT, _("Function Parameters Next Hint"));
            sub->Append(IDM_EDIT_AUTOCOMPLETE_PATH, _("Path Completion"));
            edit->AppendSubMenu(sub, _("&Auto-Completion"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FORMAT_TODOS, _("Windows (CR LF)"));
            sub->Append(IDM_FORMAT_TOUNIX, _("Unix (LF)"));
            sub->Append(IDM_FORMAT_TOMAC, _("Macintosh (CR)"));
            edit->AppendSubMenu(sub, _("&EOL Conversion"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_TRIMTRAILING, _("Trim Trailing Space"));
            sub->Append(IDM_EDIT_TRIMLINEHEAD, _("Trim Leading Space"));
            sub->Append(IDM_EDIT_TRIM_BOTH, _("Trim Leading and Trailing Space"));
            sub->Append(IDM_EDIT_EOL2WS, _("EOL to Space"));
            sub->Append(IDM_EDIT_TRIMALL, _("Trim both and EOL to Space"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_TAB2SW, _("TAB to Space"));
            sub->Append(IDM_EDIT_SW2TAB_ALL, _("Space to TAB (All)"));
            sub->Append(IDM_EDIT_SW2TAB_LEADING, _("Space to TAB (Leading)"));
            edit->AppendSubMenu(sub, _("&Blank Operations"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_PASTE_AS_HTML, _("Paste HTML Content"));
            sub->Append(IDM_EDIT_PASTE_AS_RTF, _("Paste RTF Content"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_COPY_BINARY, _("Copy Binary Content"));
            sub->Append(IDM_EDIT_CUT_BINARY, _("Cut Binary Content"));
            sub->Append(IDM_EDIT_PASTE_BINARY, _("Paste Binary Content"));
            edit->AppendSubMenu(sub, _("&Paste Special"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_OPENSELECTEDFILETOEDIT, _("Open File"));
            sub->Append(IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER, _("Open Containing Folder in Explorer"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_REDACT_SELECTION, _("&Redact Selection █ (Shift: ●)"));
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SEARCHONINTERNET, _("Search on Internet"));
            sub->Append(IDM_EDIT_CHANGESEARCHENGINE, _("Change Search Engine..."));
            edit->AppendSubMenu(sub, _("&On Selection"));
        }
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_MULTISELECTALL, _("Ignore Case && Whole Word"));
            sub->Append(IDM_EDIT_MULTISELECTALLMATCHCASE, _("Match Case Only"));
            sub->Append(IDM_EDIT_MULTISELECTALLWHOLEWORD, _("Match Whole Word Only"));
            sub->Append(IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD, _("Match Case && Whole Word"));
            edit->AppendSubMenu(sub, _("Multi-select All"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_MULTISELECTNEXT, _("Ignore Case && Whole Word"));
            sub->Append(IDM_EDIT_MULTISELECTNEXTMATCHCASE, _("Match Case Only"));
            sub->Append(IDM_EDIT_MULTISELECTNEXTWHOLEWORD, _("Match Whole Word Only"));
            sub->Append(IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD, _("Match Case && Whole Word"));
            edit->AppendSubMenu(sub, _("Multi-select Next"));
        }
        edit->Append(IDM_EDIT_MULTISELECTUNDO, _("Undo the Latest Added Multi-Select"));
        edit->Append(IDM_EDIT_MULTISELECTSSKIP, _("Skip Current && Go to Next Multi-select"));
        edit->AppendSeparator();
        edit->Append(IDM_EDIT_COLUMNMODETIP, _("Column Mode..."));
        edit->Append(IDM_EDIT_COLUMNMODE, _("Colum&n Editor...\tAlt+C"));
        edit->Append(IDM_EDIT_CHAR_PANEL, _("Character &Panel"));
        edit->Append(IDM_EDIT_CLIPBOARDHISTORY_PANEL, _("Clipboard &History"));
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_TOGGLEREADONLY, _("Read-Only on Current Document"));
            sub->Append(IDM_EDIT_SETREADONLYFORALLDOCS, _("Read-Only for All Documents"));
            sub->Append(IDM_EDIT_CLEARREADONLYFORALLDOCS, _("Clear Read-Only for All Documents"));
            edit->AppendSubMenu(sub, _("Read-&Only"));
        }
        edit->Append(IDM_EDIT_TOGGLESYSTEMREADONLY, _("Read-Only Attribute in Windows"));
        mb->Append(edit, _("&Edit"));
    }

    // --------------------------------------------------------------- Search
    {
        auto* search = new wxMenu;
        search->Append(IDM_SEARCH_FIND, _("&Find...\tCtrl+F"));
        search->Append(IDM_SEARCH_FINDINFILES, _("Find in Fi&les...\tCtrl+Shift+F"));
        search->Append(IDM_SEARCH_FINDNEXT, _("Find &Next\tF3"));
        search->Append(IDM_SEARCH_FINDPREV, _("Find &Previous\tShift+F3"));
        search->Append(IDM_SEARCH_SETANDFINDNEXT, _("&Select and Find Next\tCtrl+F3"));
        search->Append(IDM_SEARCH_SETANDFINDPREV, _("&Select and Find Previous\tCtrl+Shift+F3"));
        search->Append(IDM_SEARCH_VOLATILE_FINDNEXT, _("Find (&Volatile) Next"));
        search->Append(IDM_SEARCH_VOLATILE_FINDPREV, _("Find (&Volatile) Previous"));
        search->Append(IDM_SEARCH_REPLACE, _("&Replace...\tCtrl+H"));
        search->Append(IDM_SEARCH_FINDINCREMENT, _("&Incremental Search\tCtrl+Alt+I"));
        search->Append(IDM_FOCUS_ON_FOUND_RESULTS, _("Search Results &Window\tF7"));
        search->Append(IDM_SEARCH_GOTONEXTFOUND, _("Next Search Resul&t\tF4"));
        search->Append(IDM_SEARCH_GOTOPREVFOUND, _("Previous Search Resul&t\tShift+F4"));
        search->Append(IDM_SEARCH_GOTOLINE, _("&Go to...\tCtrl+G"));
        search->Append(IDM_SEARCH_GOTOMATCHINGBRACE, _("Go to &Matching Brace\tCtrl+B"));
        search->Append(IDM_SEARCH_SELECTMATCHINGBRACES, _("Select All In-betw&een {} [] or ()"));
        search->Append(IDM_SEARCH_MARK, _("Mar&k..."));
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_CHANGED_NEXT, _("Go to Next Change"));
            sub->Append(IDM_SEARCH_CHANGED_PREV, _("Go to Previous Change"));
            sub->Append(IDM_SEARCH_CLEAR_CHANGE_HISTORY, _("Clear Change History"));
            search->AppendSubMenu(sub, _("Change History"));
        }
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_MARKALLEXT1, _("Using 1st Style"));
            sub->Append(IDM_SEARCH_MARKALLEXT2, _("Using 2nd Style"));
            sub->Append(IDM_SEARCH_MARKALLEXT3, _("Using 3rd Style"));
            sub->Append(IDM_SEARCH_MARKALLEXT4, _("Using 4th Style"));
            sub->Append(IDM_SEARCH_MARKALLEXT5, _("Using 5th Style"));
            search->AppendSubMenu(sub, _("Style &All Occurrences of Token"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_MARKONEEXT1, _("Using 1st Style"));
            sub->Append(IDM_SEARCH_MARKONEEXT2, _("Using 2nd Style"));
            sub->Append(IDM_SEARCH_MARKONEEXT3, _("Using 3rd Style"));
            sub->Append(IDM_SEARCH_MARKONEEXT4, _("Using 4th Style"));
            sub->Append(IDM_SEARCH_MARKONEEXT5, _("Using 5th Style"));
            search->AppendSubMenu(sub, _("Style &One Token"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_UNMARKALLEXT1, _("Clear 1st Style"));
            sub->Append(IDM_SEARCH_UNMARKALLEXT2, _("Clear 2nd Style"));
            sub->Append(IDM_SEARCH_UNMARKALLEXT3, _("Clear 3rd Style"));
            sub->Append(IDM_SEARCH_UNMARKALLEXT4, _("Clear 4th Style"));
            sub->Append(IDM_SEARCH_UNMARKALLEXT5, _("Clear 5th Style"));
            sub->Append(IDM_SEARCH_CLEARALLMARKS, _("Clear all Styles"));
            search->AppendSubMenu(sub, _("Clear Style"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_GOPREVMARKER1, _("1st Style"));
            sub->Append(IDM_SEARCH_GOPREVMARKER2, _("2nd Style"));
            sub->Append(IDM_SEARCH_GOPREVMARKER3, _("3rd Style"));
            sub->Append(IDM_SEARCH_GOPREVMARKER4, _("4th Style"));
            sub->Append(IDM_SEARCH_GOPREVMARKER5, _("5th Style"));
            sub->Append(IDM_SEARCH_GOPREVMARKER_DEF, _("Find Mark Style"));
            search->AppendSubMenu(sub, _("&Jump Up"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_GONEXTMARKER1, _("1st Style"));
            sub->Append(IDM_SEARCH_GONEXTMARKER2, _("2nd Style"));
            sub->Append(IDM_SEARCH_GONEXTMARKER3, _("3rd Style"));
            sub->Append(IDM_SEARCH_GONEXTMARKER4, _("4th Style"));
            sub->Append(IDM_SEARCH_GONEXTMARKER5, _("5th Style"));
            sub->Append(IDM_SEARCH_GONEXTMARKER_DEF, _("Find Mark Style"));
            search->AppendSubMenu(sub, _("Jump &Down"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_STYLE1TOCLIP, _("1st Style"));
            sub->Append(IDM_SEARCH_STYLE2TOCLIP, _("2nd Style"));
            sub->Append(IDM_SEARCH_STYLE3TOCLIP, _("3rd Style"));
            sub->Append(IDM_SEARCH_STYLE4TOCLIP, _("4th Style"));
            sub->Append(IDM_SEARCH_STYLE5TOCLIP, _("5th Style"));
            sub->Append(IDM_SEARCH_ALLSTYLESTOCLIP, _("All Styles"));
            sub->Append(IDM_SEARCH_MARKEDTOCLIP, _("Find Mark Style"));
            search->AppendSubMenu(sub, _("&Copy Styled Text"));
        }
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_TOGGLE_BOOKMARK, _("Toggle Bookmark\tCtrl+F2"));
            sub->Append(IDM_SEARCH_NEXT_BOOKMARK, _("Next Bookmark\tF2"));
            sub->Append(IDM_SEARCH_PREV_BOOKMARK, _("Previous Bookmark\tShift+F2"));
            sub->Append(IDM_SEARCH_CLEAR_BOOKMARKS, _("Clear All Bookmarks"));
            sub->Append(IDM_SEARCH_CUTMARKEDLINES, _("Cut Bookmarked Lines"));
            sub->Append(IDM_SEARCH_COPYMARKEDLINES, _("Copy Bookmarked Lines"));
            sub->Append(IDM_SEARCH_PASTEMARKEDLINES, _("Paste to (Replace) Bookmarked Lines"));
            sub->Append(IDM_SEARCH_DELETEMARKEDLINES, _("Remove Bookmarked Lines"));
            sub->Append(IDM_SEARCH_DELETEUNMARKEDLINES, _("Remove Non-Bookmarked Lines"));
            sub->Append(IDM_SEARCH_INVERSEMARKS, _("Inverse Bookmarks"));
            search->AppendSubMenu(sub, _("&Bookmark"));
        }
        search->AppendSeparator();
        search->Append(IDM_SEARCH_FINDCHARINRANGE, _("Find characters in rang&e..."));
        mb->Append(search, _("&Search"));
    }

    // ----------------------------------------------------------------- View
    // (the restart-to-apply Dark Mode toggle is added under Settings, below)
    {
        auto* view = new wxMenu;
        view->AppendCheckItem(IDM_VIEW_ALWAYSONTOP, _("Always on &Top"));
        view->AppendCheckItem(IDM_VIEW_FULLSCREENTOGGLE, _("To&ggle Full Screen Mode\tF11"));
        view->AppendCheckItem(IDM_VIEW_POSTIT, _("&Post-It\tF12"));
        view->AppendCheckItem(IDM_VIEW_DISTRACTIONFREE, _("D&istraction Free Mode"));
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_IN_FIREFOX, _("&Firefox"));
            sub->Append(IDM_VIEW_IN_CHROME, _("&Chrome"));
            sub->Append(IDM_VIEW_IN_EDGE, _("&Edge"));
            sub->Append(IDM_VIEW_IN_IE, _("&IE"));
            view->AppendSubMenu(sub, _("&View Current File in"));
        }
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->AppendCheckItem(IDM_VIEW_TAB_SPACE, _("Show Space and Tab"));
            sub->AppendCheckItem(IDM_VIEW_EOL, _("Show End of Line"));
            sub->AppendCheckItem(IDM_VIEW_NPC, _("Show Non-Printing Characters"));
            sub->AppendCheckItem(IDM_VIEW_NPC_CCUNIEOL, _("Show Control Characters && Unicode EOL"));
            sub->AppendCheckItem(IDM_VIEW_ALL_CHARACTERS, _("Show All Characters"));
            sub->AppendSeparator();
            sub->AppendCheckItem(IDM_VIEW_INDENT_GUIDE, _("Show Indent Guide"));
            sub->AppendCheckItem(IDM_VIEW_WRAP_SYMBOL, _("Show Wrap Symbol"));
            view->AppendSubMenu(sub, _("Show S&ymbol"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_ZOOMIN, _("Zoom &In (Ctrl+Mouse Wheel Up)"));
            sub->Append(IDM_VIEW_ZOOMOUT, _("Zoom &Out (Ctrl+Mouse Wheel Down)"));
            sub->Append(IDM_VIEW_ZOOMRESTORE, _("&Restore Default Zoom"));
            view->AppendSubMenu(sub, _("&Zoom"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_GOTO_ANOTHER_VIEW,     _("Move to Other &View"));
            sub->Append(IDM_VIEW_CLONE_TO_ANOTHER_VIEW, _("Clone to Other Vie&w"));
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_GOTO_NEW_INSTANCE, _("Mo&ve to New Instance"));
            sub->Append(IDM_VIEW_LOAD_IN_NEW_INSTANCE, _("&Open in New Instance"));
            view->AppendSubMenu(sub, _("&Move/Clone Current Document"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_TAB1, _("1st Tab\tCtrl+1"));
            sub->Append(IDM_VIEW_TAB2, _("2nd Tab\tCtrl+2"));
            sub->Append(IDM_VIEW_TAB3, _("3rd Tab\tCtrl+3"));
            sub->Append(IDM_VIEW_TAB4, _("4th Tab\tCtrl+4"));
            sub->Append(IDM_VIEW_TAB5, _("5th Tab\tCtrl+5"));
            sub->Append(IDM_VIEW_TAB6, _("6th Tab\tCtrl+6"));
            sub->Append(IDM_VIEW_TAB7, _("7th Tab\tCtrl+7"));
            sub->Append(IDM_VIEW_TAB8, _("8th Tab\tCtrl+8"));
            sub->Append(IDM_VIEW_TAB9, _("9th Tab\tCtrl+9"));
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_TAB_START, _("First Tab"));
            sub->Append(IDM_VIEW_TAB_END, _("Last Tab"));
            sub->Append(IDM_VIEW_TAB_NEXT, _("Next Tab\tCtrl+Tab"));
            sub->Append(IDM_VIEW_TAB_PREV, _("Previous Tab\tCtrl+Shift+Tab"));
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_GOTO_START, _("Move to Start"));
            sub->Append(IDM_VIEW_GOTO_END, _("Move to End"));
            sub->Append(IDM_VIEW_TAB_MOVEFORWARD, _("Move Tab Forward"));
            sub->Append(IDM_VIEW_TAB_MOVEBACKWARD, _("Move Tab Backward"));
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_TAB_COLOUR_1, _("Apply Color 1"));
            sub->Append(IDM_VIEW_TAB_COLOUR_2, _("Apply Color 2"));
            sub->Append(IDM_VIEW_TAB_COLOUR_3, _("Apply Color 3"));
            sub->Append(IDM_VIEW_TAB_COLOUR_4, _("Apply Color 4"));
            sub->Append(IDM_VIEW_TAB_COLOUR_5, _("Apply Color 5"));
            sub->Append(IDM_VIEW_TAB_COLOUR_NONE, _("Remove Color"));
            view->AppendSubMenu(sub, _("Ta&b"));
        }
        view->AppendCheckItem(IDM_VIEW_WRAP, _("&Word wrap"));
        view->Append(IDM_VIEW_HIDELINES, _("&Hide Lines"));
        view->AppendSeparator();
        view->Append(IDM_VIEW_FOLDALL, _("Fold All\tAlt+0"));
        view->Append(IDM_VIEW_UNFOLDALL, _("Unfold All\tAlt+Shift+0"));
        view->Append(IDM_VIEW_FOLD_CURRENT, _("Fold Current Level\tCtrl+Alt+F"));
        view->Append(IDM_VIEW_UNFOLD_CURRENT, _("Unfold Current Level\tCtrl+Alt+Shift+F"));
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_FOLD_1, _("1\tAlt+1"));
            sub->Append(IDM_VIEW_FOLD_2, _("2\tAlt+2"));
            sub->Append(IDM_VIEW_FOLD_3, _("3\tAlt+3"));
            sub->Append(IDM_VIEW_FOLD_4, _("4\tAlt+4"));
            sub->Append(IDM_VIEW_FOLD_5, _("5\tAlt+5"));
            sub->Append(IDM_VIEW_FOLD_6, _("6\tAlt+6"));
            sub->Append(IDM_VIEW_FOLD_7, _("7\tAlt+7"));
            sub->Append(IDM_VIEW_FOLD_8, _("8\tAlt+8"));
            view->AppendSubMenu(sub, _("Fold Level"));
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_UNFOLD_1, _("1\tAlt+Shift+1"));
            sub->Append(IDM_VIEW_UNFOLD_2, _("2\tAlt+Shift+2"));
            sub->Append(IDM_VIEW_UNFOLD_3, _("3\tAlt+Shift+3"));
            sub->Append(IDM_VIEW_UNFOLD_4, _("4\tAlt+Shift+4"));
            sub->Append(IDM_VIEW_UNFOLD_5, _("5\tAlt+Shift+5"));
            sub->Append(IDM_VIEW_UNFOLD_6, _("6\tAlt+Shift+6"));
            sub->Append(IDM_VIEW_UNFOLD_7, _("7\tAlt+Shift+7"));
            sub->Append(IDM_VIEW_UNFOLD_8, _("8\tAlt+Shift+8"));
            view->AppendSubMenu(sub, _("Unfold Level"));
        }
        view->AppendSeparator();
        view->Append(IDM_VIEW_SUMMARY, _("&Summary..."));
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_PROJECT_PANEL_1, _("Project Panel &1"));
            sub->Append(IDM_VIEW_PROJECT_PANEL_2, _("Project Panel &2"));
            sub->Append(IDM_VIEW_PROJECT_PANEL_3, _("Project Panel &3"));
            view->AppendSubMenu(sub, _("Pro&ject Panels"));
        }
        view->Append(IDM_VIEW_FILEBROWSER, _("Folder as Wor&kspace"));
        view->Append(IDM_VIEW_DOC_MAP, _("&Document Map"));
        view->Append(IDM_VIEW_DOCLIST, _("D&ocument List"));
        view->Append(IDM_VIEW_FUNC_LIST, _("Function &List"));
        view->AppendSeparator();
        view->Append(IDM_EDIT_RTL, _("T&ext Direction RTL"));
        view->Append(IDM_EDIT_LTR, _("Te&xt Direction LTR"));
        view->AppendSeparator();
        view->AppendCheckItem(IDM_VIEW_MONITORING, _("Monito&ring (tail -f)"));
        mb->Append(view, _("&View"));
    }

    // ------------------------------------------------------------- Encoding
    {
        auto* enc = new wxMenu;
        enc->AppendCheckItem(IDM_FORMAT_ANSI, _("ANSI"));
        enc->AppendCheckItem(IDM_FORMAT_AS_UTF_8, _("UTF-8"));
        enc->AppendCheckItem(IDM_FORMAT_UTF_8, _("UTF-8-BOM"));
        enc->AppendCheckItem(IDM_FORMAT_UTF_16BE, _("UTF-16 BE BOM"));
        enc->AppendCheckItem(IDM_FORMAT_UTF_16LE, _("UTF-16 LE BOM"));
        {
            auto* charsets = new wxMenu;
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_6, _("ISO 8859-6"));
              s->Append(IDM_FORMAT_DOS_720, _("OEM 720"));
              s->Append(IDM_FORMAT_WIN_1256, _("Windows-1256"));
              charsets->AppendSubMenu(s, _("Arabic")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_4, _("ISO 8859-4"));
              s->Append(IDM_FORMAT_ISO_8859_13, _("ISO 8859-13"));
              s->Append(IDM_FORMAT_DOS_775, _("OEM 775"));
              s->Append(IDM_FORMAT_WIN_1257, _("Windows-1257"));
              charsets->AppendSubMenu(s, _("Baltic")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_14, _("ISO 8859-14"));
              charsets->AppendSubMenu(s, _("Celtic")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_5, _("ISO 8859-5"));
              s->Append(IDM_FORMAT_KOI8R_CYRILLIC, _("KOI8-R"));
              s->Append(IDM_FORMAT_KOI8U_CYRILLIC, _("KOI8-U"));
              s->Append(IDM_FORMAT_MAC_CYRILLIC, _("Macintosh"));
              s->Append(IDM_FORMAT_DOS_855, _("OEM 855"));
              s->Append(IDM_FORMAT_DOS_866, _("OEM 866"));
              s->Append(IDM_FORMAT_WIN_1251, _("Windows-1251"));
              charsets->AppendSubMenu(s, _("Cyrillic")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_DOS_852, _("OEM 852"));
              s->Append(IDM_FORMAT_WIN_1250, _("Windows-1250"));
              charsets->AppendSubMenu(s, _("Central European")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_BIG5, _("Big5 (Traditional)"));
              s->Append(IDM_FORMAT_GB2312, _("GB2312 (Simplified)"));
              charsets->AppendSubMenu(s, _("Chinese")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_2, _("ISO 8859-2"));
              charsets->AppendSubMenu(s, _("Eastern European")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_7, _("ISO 8859-7"));
              s->Append(IDM_FORMAT_DOS_737, _("OEM 737"));
              s->Append(IDM_FORMAT_DOS_869, _("OEM 869"));
              s->Append(IDM_FORMAT_WIN_1253, _("Windows-1253"));
              charsets->AppendSubMenu(s, _("Greek")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_8, _("ISO 8859-8"));
              s->Append(IDM_FORMAT_DOS_862, _("OEM 862"));
              s->Append(IDM_FORMAT_WIN_1255, _("Windows-1255"));
              charsets->AppendSubMenu(s, _("Hebrew")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_SHIFT_JIS, _("Shift-JIS"));
              charsets->AppendSubMenu(s, _("Japanese")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_KOREAN_WIN, _("Windows 949"));
              s->Append(IDM_FORMAT_EUC_KR, _("EUC-KR"));
              charsets->AppendSubMenu(s, _("Korean")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_DOS_861, _("OEM 861 : Icelandic"));
              s->Append(IDM_FORMAT_DOS_865, _("OEM 865 : Nordic"));
              charsets->AppendSubMenu(s, _("North European")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_TIS_620, _("TIS-620"));
              charsets->AppendSubMenu(s, _("Thai")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_3, _("ISO 8859-3"));
              s->Append(IDM_FORMAT_ISO_8859_9, _("ISO 8859-9"));
              s->Append(IDM_FORMAT_DOS_857, _("OEM 857"));
              s->Append(IDM_FORMAT_WIN_1254, _("Windows-1254"));
              charsets->AppendSubMenu(s, _("Turkish")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_1, _("ISO 8859-1"));
              s->Append(IDM_FORMAT_ISO_8859_15, _("ISO 8859-15"));
              s->Append(IDM_FORMAT_DOS_850, _("OEM 850"));
              s->Append(IDM_FORMAT_DOS_858, _("OEM 858"));
              s->Append(IDM_FORMAT_DOS_860, _("OEM 860 : Portuguese"));
              s->Append(IDM_FORMAT_DOS_863, _("OEM 863 : French"));
              s->Append(IDM_FORMAT_DOS_437, _("OEM-US"));
              s->Append(IDM_FORMAT_WIN_1252, _("Windows-1252"));
              charsets->AppendSubMenu(s, _("Western European")); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_WIN_1258, _("Windows-1258"));
              charsets->AppendSubMenu(s, _("Vietnamese")); }
            enc->AppendSubMenu(charsets, _("Character sets"));
        }
        enc->AppendSeparator();
        enc->Append(IDM_FORMAT_CONV2_ANSI, _("Convert to ANSI"));
        enc->Append(IDM_FORMAT_CONV2_AS_UTF_8, _("Convert to UTF-8"));
        enc->Append(IDM_FORMAT_CONV2_UTF_8, _("Convert to UTF-8-BOM"));
        enc->Append(IDM_FORMAT_CONV2_UTF_16BE, _("Convert to UTF-16 BE BOM"));
        enc->Append(IDM_FORMAT_CONV2_UTF_16LE, _("Convert to UTF-16 LE BOM"));
        mb->Append(enc, _("E&ncoding"));
    }

    // ------------------------------------------------------------- Language
    // The full Notepad++ language list, bucketed into single-letter submenus (A, B, C, ...) exactly like
    // Notepad++. The shared nppLangTable is grouped contiguously by first letter (see npp_menu.h top), so
    // a new submenu starts whenever the first letter changes.
    {
        auto* lang = new wxMenu;
        lang->Append(IDM_LANG_TEXT, _("None (Normal Text)"));
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
            sub->Append(IDM_LANG_USER_DLG, _("Define your language..."));
            sub->Append(IDM_LANG_OPENUDLDIR, _("Open User Defined Language folder..."));
            sub->Append(IDM_LANG_UDLCOLLECTION_PROJECT_SITE, _("Notepad++ User Defined Languages Collection"));
            lang->AppendSubMenu(sub, _("User Defined Language"));
        }
        lang->Append(IDM_LANG_USER, _("User-Defined"));
        mb->Append(lang, _("&Language"));
    }

    // ------------------------------------------------------------- Settings
    {
        auto* settings = new wxMenu;
        settings->Append(IDM_SETTING_PREFERENCE, _("Preferences..."));
        settings->Append(IDM_LANGSTYLE_CONFIG_DLG, _("Style Configurator..."));
        settings->Append(IDM_SETTING_SHORTCUT_MAPPER, _("Shortcut Mapper..."));
        settings->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SETTING_IMPORTPLUGIN, _("Import plugin(s)..."));
            sub->Append(IDM_SETTING_IMPORTSTYLETHEMES, _("Import style theme(s)..."));
            settings->AppendSubMenu(sub, _("Import"));
        }
        settings->AppendSeparator();
        settings->Append(IDM_SETTING_EDITCONTEXTMENU, _("Edit Popup ContextMenu"));
        settings->AppendSeparator();
        settings->AppendCheckItem(darkModeId, _("&Dark Mode"));   // our restart-to-apply theme toggle
        mb->Append(settings, _("Se&ttings"));
    }

    // ---------------------------------------------------------------- Tools
    {
        auto* tools = new wxMenu;
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_MD5_GENERATE, _("Generate..."));
          sub->Append(IDM_TOOL_MD5_GENERATEFROMFILE, _("Generate from files..."));
          sub->Append(IDM_TOOL_MD5_GENERATEINTOCLIPBOARD, _("Generate from selection into clipboard"));
          tools->AppendSubMenu(sub, _("MD5")); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA1_GENERATE, _("Generate..."));
          sub->Append(IDM_TOOL_SHA1_GENERATEFROMFILE, _("Generate from files..."));
          sub->Append(IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD, _("Generate from selection into clipboard"));
          tools->AppendSubMenu(sub, _("SHA-1")); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA256_GENERATE, _("Generate..."));
          sub->Append(IDM_TOOL_SHA256_GENERATEFROMFILE, _("Generate from files..."));
          sub->Append(IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD, _("Generate from selection into clipboard"));
          tools->AppendSubMenu(sub, _("SHA-256")); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA512_GENERATE, _("Generate..."));
          sub->Append(IDM_TOOL_SHA512_GENERATEFROMFILE, _("Generate from files..."));
          sub->Append(IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD, _("Generate from selection into clipboard"));
          tools->AppendSubMenu(sub, _("SHA-512")); }
        mb->Append(tools, _("T&ools"));
    }

    // ---------------------------------------------------------------- Macro
    {
        auto* macro = new wxMenu;
        macro->Append(IDM_MACRO_STARTRECORDINGMACRO, _("Start Re&cording"));
        macro->Append(IDM_MACRO_STOPRECORDINGMACRO, _("S&top Recording"));
        macro->Append(IDM_MACRO_PLAYBACKRECORDEDMACRO, _("&Playback"));
        macro->Append(IDM_MACRO_SAVECURRENTMACRO, _("&Save Current Recorded Macro..."));
        macro->Append(IDM_MACRO_RUNMULTIMACRODLG, _("&Run a Macro Multiple Times..."));
        mb->Append(macro, _("&Macro"));
    }

    // ------------------------------------------------------------------ Run
    {
        auto* run = new wxMenu;
        run->Append(IDM_EXECUTE, _("&Run...\tF5"));
        run->AppendSeparator();
        run->Append(IDM_EXECUTE_VALIDATE_SHORTCUTSXML, _("Validate shortcuts.xml"));
        mb->Append(run, _("&Run"));
    }

    // -------------------------------------------------------------- Plugins
    {
        auto* plugins = new wxMenu;
        plugins->Append(IDM_SETTING_OPENPLUGINSDIR, _("Open Plugins Folder..."));
        mb->Append(plugins, _("&Plugins"));
    }

    // --------------------------------------------------------------- Window
    {
        auto* window = new wxMenu;
        { auto* sub = new wxMenu;
          sub->Append(IDM_WINDOW_SORT_FN_ASC, _("Name A to Z"));
          sub->Append(IDM_WINDOW_SORT_FN_DSC, _("Name Z to A"));
          sub->Append(IDM_WINDOW_SORT_FP_ASC, _("Path A to Z"));
          sub->Append(IDM_WINDOW_SORT_FP_DSC, _("Path Z to A"));
          sub->Append(IDM_WINDOW_SORT_FT_ASC, _("Type A to Z"));
          sub->Append(IDM_WINDOW_SORT_FT_DSC, _("Type Z to A"));
          sub->Append(IDM_WINDOW_SORT_FS_ASC, _("Content Length Ascending"));
          sub->Append(IDM_WINDOW_SORT_FS_DSC, _("Content Length Descending"));
          sub->Append(IDM_WINDOW_SORT_FD_ASC, _("Modified Time Ascending"));
          sub->Append(IDM_WINDOW_SORT_FD_DSC, _("Modified Time Descending"));
          window->AppendSubMenu(sub, _("Sort By")); }
        window->Append(IDM_WINDOW_WINDOWS, _("&Windows..."));
        window->AppendSeparator();
        window->Append(IDM_WINDOW_MRU_FIRST, _("Recent Window"))->Enable(false);
        mb->Append(window, _("&Window"));
    }

    // ----------------------------------------------------------- ? (Help)
    {
        auto* help = new wxMenu;
        help->Append(IDM_CMDLINEARGUMENTS, _("Command Line Arguments..."));
        help->AppendSeparator();
        help->Append(IDM_HOMESWEETHOME, _("Notepad++ Home"));
        help->Append(IDM_PROJECTPAGE, _("Notepad++ Project Page"));
        help->Append(IDM_ONLINEDOCUMENT, _("Notepad++ Online User Manual"));
        help->Append(IDM_FORUM, _("Notepad++ Community (Forum)"));
        help->AppendSeparator();
        help->Append(IDM_UPDATE_NPP, _("Check for Updates"));
        help->Append(IDM_CONFUPDATERPROXY, _("Set Updater Proxy..."));
        help->AppendSeparator();
        help->Append(IDM_DEBUGINFO, _("Debug Info..."));
        help->Append(IDM_ABOUT, _("About wxNotepad++\tF1"));
        mb->Append(help, _("&About"));
    }
}
