#pragma once
// SPDX-License-Identifier: Apache-2.0
//
// keyword_sets - what each keyword list a language's lexer takes is called, so that languages.yaml and
// the Style Configurator can name it ("types", "taskMarkers") instead of by its SCI_SETKEYWORDS number.
//
// The names follow what Lexilla's lexers say each list is for (ILexer5::DescribeWordListSets, printed as
// `label` where it reads well), shortened to one word; a few lexers describe fewer lists than they read,
// or describe two the wrong way round (Clarion), and there the lexer's code decides. Where a theme names
// the style a list colours (Lua's FUNC1-3), the name follows the theme. Lists a lexer reads but says are
// unused (CoffeeScript 2, R 3 and 4) have no name.
//
// Also here: the user keyword groups - Lexilla substyles of identifiers (SCI_ALLOCATESUBSTYLES), which
// the themes colour as USER KEYWORDS 1-8, USER TAGS 1-4 and so on. wxNote allocates them as Notepad++
// does, in the same order and numbers, so a theme's styles for them line up.
//
// Standalone - std only, no wx - so tests/keywords_test.cpp checks every name against the real lexers.

#include <cstddef>
#include <string>
#include <vector>

struct WxnKeywordSetName
{
    const char* lexer;   // Lexilla lexer name (lang_table.h's third column)
    int         slot;    // SCI_SETKEYWORDS wParam
    const char* name;    // what languages.yaml calls it
    const char* label;   // what the list holds, for the Style Configurator
};

inline const WxnKeywordSetName* wxnKeywordSetNames(std::size_t& n)
{
    static const WxnKeywordSetName t[] = {
        { "abl", 0, "keywords", "Keywords (abbreviations as DEF(INE)" },
        { "abl", 1, "blockStarters", "Keywords that open a block at the start of a statement" },
        { "abl", 2, "blockStartersAnywhere", "Keywords that open a block anywhere in a statement" },
        { "abl", 3, "taskMarkers", "Task markers" },
        { "ada", 0, "keywords", "Keywords" },
        { "asm", 0, "cpuInstructions", "CPU instructions" },
        { "asm", 1, "fpuInstructions", "FPU instructions" },
        { "asm", 2, "registers", "Registers" },
        { "asm", 3, "directives", "Directives" },
        { "asm", 4, "directiveOperands", "Directive operands" },
        { "asm", 5, "extendedInstructions", "Extended instructions" },
        { "asm", 6, "foldStartDirectives", "Directives that open a fold" },
        { "asm", 7, "foldEndDirectives", "Directives that close a fold" },
        { "asn1", 0, "keywords", "Keywords" },
        { "asn1", 1, "attributes", "Attributes" },
        { "asn1", 2, "descriptors", "Descriptors" },
        { "asn1", 3, "types", "Types" },
        { "au3", 0, "keywords", "Keywords" },
        { "au3", 1, "functions", "Functions" },
        { "au3", 2, "macros", "Macros" },
        { "au3", 3, "sendKeys", "Send keys" },
        { "au3", 4, "preprocessor", "Preprocessor directives" },
        { "au3", 5, "special", "Special" },
        { "au3", 6, "expand", "Expand" },
        { "au3", 7, "udf", "User defined functions" },
        { "avs", 0, "keywords", "Keywords" },
        { "avs", 1, "filters", "Filters" },
        { "avs", 2, "plugins", "Plugins" },
        { "avs", 3, "functions", "Functions" },
        { "avs", 4, "clipProperties", "Clip properties" },
        { "avs", 5, "userFunctions", "User defined functions" },
        { "baan", 0, "keywords", "Baan and BaanSQL reserved keywords" },
        { "baan", 1, "functions", "Standard functions" },
        { "baan", 2, "functionsAbridged", "Functions abridged" },
        { "baan", 3, "mainSections", "Main sections" },
        { "baan", 4, "subSections", "Sub sections" },
        { "baan", 5, "predefinedVariables", "Predefined variables" },
        { "baan", 6, "predefinedAttributes", "Predefined attributes" },
        { "baan", 7, "enumerates", "Enumerates" },
        { "baan", 8, "keywords9", "Ninth keyword list" },
        { "bash", 0, "keywords", "Keywords" },
        { "batch", 0, "internalCommands", "Internal commands" },
        { "batch", 1, "externalCommands", "External commands" },
        { "bib", 0, "entryTypes", "Entry types" },
        { "blitzbasic", 0, "keywords", "Keywords" },
        { "blitzbasic", 1, "user1", "User list 1" },
        { "blitzbasic", 2, "user2", "User list 2" },
        { "blitzbasic", 3, "user3", "User list 3" },
        { "caml", 0, "keywords", "Keywords" },
        { "caml", 1, "keywords2", "Keywords 2" },
        { "caml", 2, "keywords3", "Keywords 3" },
        { "cil", 0, "keywords", "Keywords" },
        { "cil", 1, "metadata", "Metadata" },
        { "cil", 2, "opcodes", "Opcode instructions" },
        // The lexer's descriptions swap 2 and 3; its code reads runtime expressions from 2.
        { "clarionnocase", 0, "keywords", "Keywords" },
        { "clarionnocase", 1, "compilerDirectives", "Compiler directives" },
        { "clarionnocase", 2, "runtimeExpressions", "Runtime expressions" },
        { "clarionnocase", 3, "builtins", "Built-in procedures and functions" },
        { "clarionnocase", 4, "dataTypes", "Structures and data types" },
        { "clarionnocase", 5, "attributes", "Attributes" },
        { "clarionnocase", 6, "standardEquates", "Standard equates" },
        { "clarionnocase", 7, "reservedLabels", "Reserved words (labels)" },
        { "clarionnocase", 8, "reservedProcedureLabels", "Reserved words (procedure labels)" },
        { "cmake", 0, "commands", "Commands" },
        { "cmake", 1, "parameters", "Parameters" },
        { "cmake", 2, "userDefined", "User defined" },
        { "COBOL", 0, "areaA", "Area A keywords" },
        { "COBOL", 1, "areaB", "Area B keywords" },
        { "COBOL", 2, "extended", "Extended keywords" },
        { "coffeescript", 0, "keywords", "Keywords" },
        { "coffeescript", 1, "secondary", "Secondary keywords" },
        { "coffeescript", 3, "globalClasses", "Global classes" },
        // C, C++, C#, Java, JavaScript, TypeScript, Go, Swift, Kotlin, Objective-C... Notepad++ keeps the type
        // names in list 1, and its theme calls the style TYPE WORD.
        { "cpp", 0, "keywords", "Keywords" },
        { "cpp", 1, "types", "Types and other secondary keywords" },
        { "cpp", 2, "docKeywords", "Documentation comment keywords" },
        { "cpp", 3, "globalClasses", "Global classes and typedefs" },
        { "cpp", 4, "preprocessor", "Preprocessor definitions" },
        { "cpp", 5, "taskMarkers", "Task and error markers" },
        { "csound", 0, "opcodes", "Opcodes" },
        { "csound", 1, "headerStatements", "Header statements" },
        { "csound", 2, "user", "User keywords" },
        { "css", 0, "css1Properties", "CSS1 properties" },
        { "css", 1, "pseudoClasses", "Pseudo-classes" },
        { "css", 2, "css2Properties", "CSS2 properties" },
        { "css", 3, "css3Properties", "CSS3 properties" },
        { "css", 4, "pseudoElements", "Pseudo-elements" },
        { "css", 5, "vendorProperties", "Browser-specific properties" },
        { "css", 6, "vendorPseudoClasses", "Browser-specific pseudo-classes" },
        { "css", 7, "vendorPseudoElements", "Browser-specific pseudo-elements" },
        { "d", 0, "keywords", "Keywords" },
        { "d", 1, "secondary", "Secondary keywords" },
        { "d", 2, "docKeywords", "Documentation comment keywords" },
        { "d", 3, "types", "Type definitions and aliases" },
        { "d", 4, "keywords5", "Keywords 5" },
        { "d", 5, "keywords6", "Keywords 6" },
        { "d", 6, "keywords7", "Keywords 7" },
        { "dart", 0, "keywords", "Keywords" },
        { "dart", 1, "secondary", "Secondary keywords" },
        { "dart", 2, "tertiary", "Tertiary keywords" },
        { "dart", 3, "globalTypes", "Global type definitions" },
        { "dataflex", 0, "keywords", "Keywords" },
        { "dataflex", 1, "scopeOpen", "Scope open" },
        { "dataflex", 2, "scopeClose", "Scope close" },
        { "dataflex", 3, "operators", "Operators" },
        { "eiffel", 0, "keywords", "Keywords" },
        { "erlang", 0, "keywords", "Reserved words" },
        { "erlang", 1, "bifs", "Built-in functions" },
        { "erlang", 2, "preprocessor", "Preprocessor" },
        { "erlang", 3, "moduleAttributes", "Module attributes" },
        { "erlang", 4, "docKeywords", "Documentation" },
        { "erlang", 5, "docMacros", "Documentation macros" },
        { "escript", 0, "keywords", "Keywords" },
        { "escript", 1, "functions", "Intrinsic functions" },
        { "escript", 2, "extendedFunctions", "Extended and user defined functions" },
        { "f77", 0, "keywords", "Keywords" },
        { "f77", 1, "functions", "Intrinsic functions" },
        { "f77", 2, "extendedFunctions", "Extended and user defined functions" },
        { "forth", 0, "control", "Control keywords" },
        { "forth", 1, "keywords", "Keywords" },
        { "forth", 2, "definingWords", "Defining words" },
        { "forth", 3, "prewords1", "Prewords with one argument" },
        { "forth", 4, "prewords2", "Prewords with two arguments" },
        { "forth", 5, "stringDefinitions", "String definition keywords" },
        { "fortran", 0, "keywords", "Keywords" },
        { "fortran", 1, "functions", "Intrinsic functions" },
        { "fortran", 2, "extendedFunctions", "Extended and user defined functions" },
        { "freebasic", 0, "keywords", "Keywords" },
        { "freebasic", 1, "preprocessor", "Preprocessor keywords" },
        { "freebasic", 2, "user1", "User list 1" },
        { "freebasic", 3, "user2", "User list 2" },
        { "fsharp", 0, "keywords", "Keywords" },
        { "fsharp", 1, "functions", "Core functions" },
        { "fsharp", 2, "types", "Built-in types, core namespaces and modules" },
        { "fsharp", 3, "optional1", "Optional list 1" },
        { "fsharp", 4, "optional2", "Optional list 2" },
        { "gdscript", 0, "keywords", "Keywords" },
        { "gdscript", 1, "identifiers", "Highlighted identifiers" },
        { "gui4cli", 0, "globals", "Globals" },
        { "gui4cli", 1, "events", "Events" },
        { "gui4cli", 2, "attributes", "Attributes" },
        { "gui4cli", 3, "control", "Control statements" },
        { "gui4cli", 4, "commands", "Commands" },
        { "haskell", 0, "keywords", "Keywords" },
        { "haskell", 1, "ffi", "Foreign function interface" },
        { "haskell", 2, "reservedOperators", "Reserved operators" },
        { "hollywood", 0, "keywords", "Keywords" },
        { "hollywood", 1, "functions", "Standard API functions" },
        { "hollywood", 2, "pluginFunctions", "Plugin API functions" },
        { "hollywood", 3, "pluginMethods", "Plugin methods" },
        // HTML, PHP, ASP and JSP: the markup and every script language embedded in it.
        { "hypertext", 0, "html", "HTML elements and attributes" },
        { "hypertext", 1, "javascript", "JavaScript keywords" },
        { "hypertext", 2, "vbscript", "VBScript keywords" },
        { "hypertext", 3, "python", "Python keywords" },
        { "hypertext", 4, "php", "PHP keywords" },
        { "hypertext", 5, "sgml", "SGML and DTD keywords" },
        { "inno", 0, "sections", "Sections" },
        { "inno", 1, "keywords", "Keywords" },
        { "inno", 2, "parameters", "Parameters" },
        { "inno", 3, "preprocessor", "Preprocessor directives" },
        { "inno", 4, "pascal", "Pascal keywords" },
        { "inno", 5, "user", "User defined keywords" },
        { "json", 0, "keywords", "Keywords" },
        { "json", 1, "jsonLd", "JSON-LD keywords" },
        { "julia", 0, "keywords", "Keywords" },
        { "julia", 1, "types", "Built-in types" },
        { "julia", 2, "other", "Other keywords" },
        { "julia", 3, "functions", "Built-in functions" },
        { "kix", 0, "commands", "Commands" },
        { "kix", 1, "functions", "Functions" },
        { "kix", 2, "macros", "Macros" },
        { "lisp", 0, "functions", "Functions and special operators" },
        { "lisp", 1, "keywords", "Keywords" },
        // Lists 1-3 are coloured by the themes' FUNC1-3 styles, 4-7 by USER KEYWORD 1-4.
        { "lua", 0, "keywords", "Keywords" },
        { "lua", 1, "functions1", "Basic functions" },
        { "lua", 2, "functions2", "String, table and math functions" },
        { "lua", 3, "functions3", "Coroutine, I/O and system facilities" },
        { "lua", 4, "user1", "User list 1" },
        { "lua", 5, "user2", "User list 2" },
        { "lua", 6, "user3", "User list 3" },
        { "lua", 7, "user4", "User list 4" },
        { "makefile", 0, "directives", "Directives" },
        { "matlab", 0, "keywords", "Keywords" },
        { "metapost", 0, "metapost", "MetaPost" },
        { "metapost", 1, "metafun", "MetaFun" },
        { "metapost", 3, "foldStart", "Keywords that open a fold" },
        { "metapost", 4, "foldEnd", "Keywords that close a fold" },
        { "mmixal", 0, "opcodes", "Operation codes" },
        { "mmixal", 1, "specialRegisters", "Special registers" },
        { "mmixal", 2, "predefinedSymbols", "Predefined symbols" },
        { "modula", 0, "keywords", "Keywords" },
        { "modula", 1, "reservedKeywords", "Reserved keywords" },
        { "modula", 2, "operators", "Operators" },
        { "modula", 3, "pragmaKeywords", "Pragma keywords" },
        { "modula", 4, "escapeCodes", "Escape codes" },
        { "modula", 5, "docKeywords", "Documentation keywords" },
        { "mssql", 0, "statements", "Statements" },
        { "mssql", 1, "types", "Data types" },
        { "mssql", 2, "systemTables", "System tables" },
        { "mssql", 3, "globalVariables", "Global variables (@@ left off)" },
        { "mssql", 4, "functions", "Functions" },
        { "mssql", 5, "systemProcedures", "System stored procedures" },
        { "mssql", 6, "operators", "Operators" },
        { "mysql", 0, "major", "Major keywords" },
        { "mysql", 1, "keywords", "Keywords" },
        { "mysql", 2, "databaseObjects", "Database objects" },
        { "mysql", 3, "functions", "Functions" },
        { "mysql", 4, "systemVariables", "System variables" },
        { "mysql", 5, "procedureKeywords", "Procedure keywords" },
        { "mysql", 6, "user1", "User list 1" },
        { "mysql", 7, "user2", "User list 2" },
        { "mysql", 8, "user3", "User list 3" },
        { "nim", 0, "keywords", "Keywords" },
        { "nix", 0, "keywords", "Keywords" },
        { "nix", 1, "keywords2", "Keywords 2" },
        { "nix", 2, "keywords3", "Keywords 3" },
        { "nix", 3, "keywords4", "Keywords 4" },
        { "nncrontab", 0, "sections", "Section keywords and Forth words" },
        { "nncrontab", 1, "keywords", "Keywords" },
        { "nncrontab", 2, "modifiers", "Modifiers" },
        { "nsis", 0, "functions", "Functions" },
        { "nsis", 1, "variables", "Variables" },
        { "nsis", 2, "labels", "Labels" },
        { "nsis", 3, "userDefined", "User defined" },
        { "octave", 0, "keywords", "Keywords" },
        { "oscript", 0, "keywords", "Keywords and reserved words" },
        { "oscript", 1, "constants", "Literal constants" },
        { "oscript", 2, "operators", "Literal operators" },
        { "oscript", 3, "types", "Built-in value and reference types" },
        { "oscript", 4, "functions", "Built-in global functions" },
        { "oscript", 5, "objects", "Built-in static objects" },
        { "pascal", 0, "keywords", "Keywords" },
        { "perl", 0, "keywords", "Keywords" },
        { "pov", 0, "directives", "Language directives" },
        { "pov", 1, "objects", "Objects, CSG and appearance" },
        { "pov", 2, "types", "Types, modifiers and items" },
        { "pov", 3, "identifiers", "Predefined identifiers" },
        { "pov", 4, "functions", "Predefined functions" },
        { "pov", 5, "user1", "User list 1" },
        { "pov", 6, "user2", "User list 2" },
        { "pov", 7, "user3", "User list 3" },
        { "powershell", 0, "commands", "Commands" },
        { "powershell", 1, "cmdlets", "Cmdlets" },
        { "powershell", 2, "aliases", "Aliases" },
        { "powershell", 3, "functions", "Functions" },
        { "powershell", 4, "user", "User keywords" },
        { "powershell", 5, "docComment", "Documentation comment keywords" },
        { "ps", 0, "operators1", "PostScript level 1 operators" },
        { "ps", 1, "operators2", "PostScript level 2 operators" },
        { "ps", 2, "operators3", "PostScript level 3 operators" },
        { "ps", 3, "ripOperators", "RIP-specific operators" },
        { "ps", 4, "userOperators", "User-defined operators" },
        { "purebasic", 0, "keywords", "Keywords" },
        { "purebasic", 1, "preprocessor", "Preprocessor keywords" },
        { "purebasic", 2, "user1", "User list 1" },
        { "purebasic", 3, "user2", "User list 2" },
        { "python", 0, "keywords", "Keywords" },
        { "python", 1, "identifiers", "Highlighted identifiers" },
        { "r", 0, "keywords", "Keywords" },
        { "r", 1, "baseFunctions", "Base and default package functions" },
        { "r", 2, "otherFunctions", "Other package functions" },
        { "raku", 0, "keywords", "Keywords" },
        { "raku", 1, "functions", "Functions" },
        { "raku", 2, "basicTypes", "Basic types" },
        { "raku", 3, "compositeTypes", "Composite types" },
        { "raku", 4, "domainTypes", "Domain-specific types" },
        { "raku", 5, "exceptionTypes", "Exception types" },
        { "raku", 6, "adverbs", "Adverbs" },
        { "rebol", 0, "keywords", "Keywords" },
        { "rebol", 1, "keywords2", "Keywords 2" },
        { "rebol", 2, "keywords3", "Keywords 3" },
        { "ruby", 0, "keywords", "Keywords" },
        { "rust", 0, "keywords", "Keywords" },
        { "rust", 1, "types", "Built-in types" },
        { "rust", 2, "other", "Other keywords" },
        { "rust", 3, "keywords4", "Keywords 4" },
        { "rust", 4, "keywords5", "Keywords 5" },
        { "rust", 5, "keywords6", "Keywords 6" },
        { "rust", 6, "keywords7", "Keywords 7" },
        { "sas", 0, "keywords", "Keywords" },
        { "sas", 1, "blockKeywords", "Block keywords" },
        { "sas", 2, "functions", "Function keywords" },
        { "sas", 3, "statements", "Statements" },
        { "smalltalk", 0, "specialSelectors", "Special selectors" },
        { "spice", 0, "keywords", "Keywords" },
        { "spice", 1, "keywords2", "Keywords 2" },
        { "spice", 2, "keywords3", "Keywords 3" },
        { "sql", 0, "keywords", "Keywords" },
        { "sql", 1, "databaseObjects", "Database objects" },
        { "sql", 2, "pldoc", "PLDoc keywords" },
        { "sql", 3, "sqlplus", "SQL*Plus keywords" },
        { "sql", 4, "user1", "User list 1" },
        { "sql", 5, "user2", "User list 2" },
        { "sql", 6, "user3", "User list 3" },
        { "sql", 7, "user4", "User list 4" },
        { "stata", 0, "keywords", "Commands and keywords" },
        { "stata", 1, "types", "Types" },
        { "tcl", 0, "keywords", "TCL keywords" },
        { "tcl", 1, "tk", "Tk keywords" },
        { "tcl", 2, "itcl", "iTCL keywords" },
        { "tcl", 3, "tkCommands", "Tk commands" },
        { "tcl", 4, "expand", "Expand" },
        { "tcl", 5, "user1", "User list 1" },
        { "tcl", 6, "user2", "User list 2" },
        { "tcl", 7, "user3", "User list 3" },
        { "tcl", 8, "user4", "User list 4" },
        { "tex", 0, "tex", "TeX, eTeX, pdfTeX and Omega" },
        { "tex", 1, "contextDutch", "ConTeXt Dutch" },
        { "tex", 2, "contextEnglish", "ConTeXt English" },
        { "tex", 3, "contextGerman", "ConTeXt German" },
        { "tex", 4, "contextCzech", "ConTeXt Czech" },
        { "tex", 5, "contextItalian", "ConTeXt Italian" },
        { "tex", 6, "contextRomanian", "ConTeXt Romanian" },
        { "toml", 0, "keywords", "Keywords" },
        { "vb", 0, "keywords", "Keywords" },
        { "vb", 1, "user1", "User list 1" },
        { "vb", 2, "user2", "User list 2" },
        { "vb", 3, "user3", "User list 3" },
        { "vbscript", 0, "keywords", "Keywords" },
        { "vbscript", 1, "user1", "User list 1" },
        { "vbscript", 2, "user2", "User list 2" },
        { "vbscript", 3, "user3", "User list 3" },
        { "verilog", 0, "keywords", "Keywords" },
        { "verilog", 1, "secondary", "Secondary keywords" },
        { "verilog", 2, "systemTasks", "System tasks" },
        { "verilog", 3, "userTasks", "User defined tasks and identifiers" },
        { "verilog", 4, "docKeywords", "Documentation comment keywords" },
        { "verilog", 5, "preprocessor", "Preprocessor definitions" },
        { "vhdl", 0, "keywords", "Keywords" },
        { "vhdl", 1, "operators", "Operators" },
        { "vhdl", 2, "attributes", "Attributes" },
        { "vhdl", 3, "functions", "Standard functions" },
        { "vhdl", 4, "packages", "Standard packages" },
        { "vhdl", 5, "types", "Standard types" },
        { "vhdl", 6, "user", "User words" },
        { "visualprolog", 0, "major", "Major keywords (class, predicates...)" },
        { "visualprolog", 1, "minor", "Minor keywords (if, then, try...)" },
        { "visualprolog", 2, "directives", "Directive keywords without the # (include, requires...)" },
        { "visualprolog", 3, "docKeywords", "Documentation keywords without the @ (short, detail...)" },
        // XML runs on the HTML lexer, so it has the same lists.
        { "xml", 0, "html", "Elements and attributes" },
        { "xml", 1, "javascript", "JavaScript keywords" },
        { "xml", 2, "vbscript", "VBScript keywords" },
        { "xml", 3, "python", "Python keywords" },
        { "xml", 4, "php", "PHP keywords" },
        { "xml", 5, "sgml", "SGML and DTD keywords" },
        { "yaml", 0, "keywords", "Keywords" },
        { "zig", 0, "keywords", "Keywords" },
        { "zig", 1, "secondary", "Secondary keywords" },
        { "zig", 2, "tertiary", "Tertiary keywords" },
        { "zig", 3, "globalTypes", "Global type definitions" },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// The names a lexer's lists have, slot by slot.
inline std::vector<const WxnKeywordSetName*> wxnKeywordSetsOf(const std::string& lexer)
{
    std::vector<const WxnKeywordSetName*> out;
    std::size_t n;
    const WxnKeywordSetName* t = wxnKeywordSetNames(n);
    for (std::size_t i = 0; i < n; ++i) if (lexer == t[i].lexer) out.push_back(&t[i]);
    return out;
}

// ---- user keyword groups: identifier substyles -----------------------------------------------------
//
// One run of substyles of a base style: `count` groups, named name+firstNumber... ("userKeywords1"). The
// rows of a lexer are allocated in table order, which fixes the style numbers a theme colours them with
// (128 up for most lexers, 192 up for the HTML lexer). `language` limits a run's NAMES to one language -
// the HTML lexer's script runs belong to the language whose script they are - while every run of the
// lexer is still allocated, so the numbers stay the same whichever language the document is.
struct WxnSubstyleRun
{
    const char* lexer;
    const char* language;     // "" for every language on the lexer
    int         base;         // the style the groups are substyles of
    int         count;
    const char* name;         // group name stem
    const char* themeStyle;   // what the themes call the groups' styles, before the number
    int         firstNumber;  // number of the run's first group
};

inline const WxnSubstyleRun* wxnSubstyleRuns(std::size_t& n)
{
    // Style numbers from SciLexer.h: SCE_C_IDENTIFIER 11, SCE_P_IDENTIFIER 11, SCE_GD_IDENTIFIER 11,
    // SCE_LUA_IDENTIFIER 11, SCE_SH_IDENTIFIER 8, SCE_SH_SCALAR 9, SCE_H_TAG 1, SCE_H_ATTRIBUTE 3,
    // SCE_HJ_WORD 46, SCE_HPHP_WORD 121, SCE_HB_WORD 74.
    static const WxnSubstyleRun t[] = {
        { "cpp",       "",           11,  8, "userKeywords",   "USER KEYWORDS",   1 },   // 128-135
        { "python",    "",           11,  8, "userKeywords",   "USER KEYWORDS",   1 },
        { "gdscript",  "",           11,  8, "userKeywords",   "USER KEYWORDS",   1 },
        { "lua",       "",           11,  4, "userKeywords",   "USER KEYWORDS",   5 },   // 5-8: 1-4 are lists 4-7
        { "bash",      "",            8,  4, "userKeywords",   "USER KEYWORDS",   1 },   // 128-131
        { "bash",      "",            9,  4, "userScalars",    "USER SCALAR",     1 },   // 132-135
        { "hypertext", "",            1,  4, "userTags",       "USER TAGS",       1 },   // 192-195
        { "hypertext", "",            3,  4, "userAttributes", "USER ATTRIBUTES", 1 },   // 196-199
        { "hypertext", "HTML",       46,  8, "userKeywords",   "USER KEYWORDS",   1 },   // JavaScript words: 200-207
        { "hypertext", "JSP",        46,  8, "userKeywords",   "USER KEYWORDS",   1 },
        { "hypertext", "PHP",       121,  8, "userKeywords",   "USER KEYWORDS",   1 },   // PHP words: 208-215
        { "hypertext", "ASP",        74,  8, "userKeywords",   "USER KEYWORDS",   1 },   // VBScript words: 216-223
        { "xml",       "",            3,  8, "userAttributes", "USER ATTRIBUTES", 1 },   // 192-199
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// The runs to allocate for `lexer`, in order. A base listed by several rows (the HTML lexer's script
// runs, one per language) is allocated once.
inline std::vector<const WxnSubstyleRun*> wxnSubstyleAllocation(const std::string& lexer)
{
    std::vector<const WxnSubstyleRun*> out;
    std::size_t n;
    const WxnSubstyleRun* t = wxnSubstyleRuns(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (lexer != t[i].lexer) continue;
        bool dup = false;
        for (const WxnSubstyleRun* r : out) if (r->base == t[i].base) dup = true;
        if (!dup) out.push_back(&t[i]);
    }
    return out;
}

// One user keyword group a language has: its name ("userKeywords3"), the run it belongs to and its index
// in that run.
struct WxnSubstyleGroup { std::string name; const WxnSubstyleRun* run; int index; };
inline std::vector<WxnSubstyleGroup> wxnSubstyleGroupsOf(const std::string& lexer, const std::string& language)
{
    std::vector<WxnSubstyleGroup> out;
    std::size_t n;
    const WxnSubstyleRun* t = wxnSubstyleRuns(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (lexer != t[i].lexer || (*t[i].language && language != t[i].language)) continue;
        for (int k = 0; k < t[i].count; ++k)
            out.push_back({ std::string(t[i].name) + std::to_string(t[i].firstNumber + k), &t[i], k });
    }
    return out;
}
