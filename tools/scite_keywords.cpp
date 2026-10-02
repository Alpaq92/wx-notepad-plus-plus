// SPDX-License-Identifier: Apache-2.0
//
// scite_keywords - turns SciTE's language .properties files into src/keywords_scite.h: the keyword lists
// wxNote hands its Lexilla lexers, by Language-menu language and keyword slot.
//
// SciTE is Lexilla's companion editor, by the same author and under the same licence (the vendored
// third_party/lexilla/License.txt is "License for Lexilla, Scintilla, and SciTE"), and its lists are
// written for exactly these lexers: `keywords.<files>` fills keyword slot 0, `keywords2.<files>` slot 1,
// and so on. This tool only re-files them under wxNote's language names; it adds no word of its own.
//
// A maintainer tool like po2mo - not built by default, run when SciTE is updated:
//   cmake --build build --target scite_keywords
//   build/bin/scite_keywords <scite>/src src/lang_table.h src/keywords_scite.h "SciTE 5.6.7" <sha256>
// The last two arguments only label the output: the release the lists came from, and the SHA-256 of its
// source archive, so a reader can tell which copy of SciTE produced the file.
//
// It links Lexilla and asks each lexer whether it takes a list in each slot (see lexerTakesSlot), so a
// list SciTE assigns to a slot the lexer does not have is dropped - and reported - rather than shipped.
//
#include "ILexer.h"
#include "Lexilla.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// wxNote Language-menu name -> the SciTE keys whose lists it takes, merged slot by slot in this order.
// A key is what follows "keywordsN." in SciTE: a file.patterns variable name (`cpp` for
// $(file.patterns.cpp)), or a literal file pattern for the few files that use one (`*.css`).
// Languages that share a lexer get the list for THEIR files: C# takes `cs`, not `cpp`.
static const std::map<std::string, std::vector<std::string>> kMap = {
    { "ActionScript", { "flash" } }, { "Ada", { "ada" } }, { "ASN.1", { "asn1" } }, { "ASP", { "html" } },
    { "Assembly", { "asm" } }, { "AutoIt", { "au3" } }, { "AviSynth", { "avs" } }, { "BaanC", { "baan" } },
    { "Batch", { "batch" } }, { "BlitzBasic", { "blitzbasic" } },
    { "C", { "cpp", "doxygen.langs" } }, { "C#", { "cs" } }, { "C++", { "cpp", "doxygen.langs" } },
    { "Caml", { "caml" } }, { "CIL", { "cil" } }, { "CMake", { "cmake" } }, { "COBOL", { "COBOL" } },
    { "Csound", { "csound" } }, { "CSS", { "*.css" } }, { "D", { "d" } }, { "DataFlex", { "dataflex" } },
    { "Dockerfile", { "bash" } }, { "Eiffel", { "eiffel" } }, { "Erlang", { "erlang" } },
    { "ESCRIPT", { "escript" } }, { "F#", { "fsharp" } }, { "Forth", { "forth" } },
    { "Fortran (fixed form)", { "f77", "fortran" } }, { "Fortran (free form)", { "f95", "fortran" } },
    { "FreeBasic", { "freebasic" } }, { "Go", { "go" } }, { "Haskell", { "*.hs" } },
    { "Hollywood", { "hollywood" } }, { "HTML", { "html" } }, { "Inno Setup", { "inno" } },
    { "Java", { "java", "doxygen.langs" } }, { "JavaScript", { "js" } }, { "JSON", { "json" } },
    { "JSON5", { "json" } }, { "JSP", { "html" } }, { "KIXtart", { "kix" } }, { "LISP", { "lisp" } },
    { "Lua", { "lua" } }, { "Makefile", { "make" } }, { "MATLAB", { "matlab" } }, { "MetaPost", { "metapost" } },
    { "MMIXAL", { "mmixal" } },
    { "Modula-3", { "m3" } }, { "Nim", { "nim" } }, { "nnCron", { "nncron" } }, { "NSIS", { "nsis" } },
    { "Objective-C", { "cpp", "doxygen.langs" } }, { "Octave", { "octave" } }, { "OScript", { "oscript" } },
    { "Pascal", { "pascal", "pascal.package" } }, { "Perl", { "perl" } }, { "PHP", { "html" } },
    { "PostScript", { "ps" } }, { "POV-Ray", { "pov" } }, { "PowerShell", { "powershell" } },
    { "PureBasic", { "purebasic" } }, { "Python", { "py" } }, { "R", { "r" } }, { "Raku", { "raku" } },
    { "Rebol", { "rebol" } }, { "Resource file", { "rc" } }, { "Ruby", { "rb" } }, { "Rust", { "rust" } },
    { "SAS", { "sas" } }, { "Scheme", { "scheme" } }, { "Shell", { "bash" } }, { "Smalltalk", { "smalltalk" } },
    { "SPICE", { "spice" } }, { "SQL", { "sql" } }, { "Swift", { "swift" } }, { "TCL", { "tcl.like", "itcl" } },
    { "TeX", { "tex" } }, { "TypeScript", { "js" } }, { "VBScript", { "wscript" } },
    { "Verilog", { "verilog", "systemverilog" } }, { "VHDL", { "vhdl" } }, { "Visual Basic", { "vb" } },
    { "Visual Prolog", { "visualprolog", "visualprolog.like" } }, { "XML", { "xml" } }, { "YAML", { "yaml" } },
};

// Languages SciTE's own files have no lists for, taken from Lexilla's test files instead - same author,
// same licence, shipped in the same SciTE source archive (lexilla/test/examples/<dir>/SciTE.properties).
// Only the slots that hold a real list: the files are test fixtures, and some slots carry a single
// placeholder word (Dart 3 "Spacecraft", Nix 3 "runCommand") or another language's list (Zig 0 is Python's).
struct LexillaTestList { const char* language; const char* dir; const char* pattern; std::vector<int> slots; };
static const LexillaTestList kLexillaTests[] = {
    { "Dart", "dart", "*.dart", { 0, 1, 2 } },
    { "Nix",  "nix",  "*.nix",  { 0, 1, 2 } },
    { "TOML", "toml", "*.toml", { 0 } },
    { "Zig",  "zig",  "*.zig",  { 1 } },
};

static std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s; s << in.rdbuf();
    return s.str();
}

static std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// SciTE's property syntax, as far as its language files use it: key=value lines, '#' comments, a trailing
// backslash continuing onto the next line, and $(name) references. The `if`/`import`/`match` directive
// lines are skipped and the assignments under them read as unconditional - none of them guards a keyword
// list. Files are read in name order and a later assignment wins, as in SciTE.
static void readPropertiesFile(const fs::path& f, std::map<std::string, std::string>& props)
{
    std::istringstream in(readFile(f));
    std::string line, raw;
    while (std::getline(in, raw))
    {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        line = raw;
        while (!line.empty() && line.back() == '\\' && std::getline(in, raw))
        {
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            line.pop_back();
            line += ' ' + raw;
        }
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.rfind("if ", 0) == 0 || t.rfind("import ", 0) == 0 || t.rfind("match ", 0) == 0) continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        props[trim(t.substr(0, eq))] = t.substr(eq + 1);
    }
}
static std::map<std::string, std::string> readProperties(const fs::path& dir)
{
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".properties") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::map<std::string, std::string> props;
    for (const fs::path& f : files) readPropertiesFile(f, props);
    return props;
}

static std::string expand(const std::map<std::string, std::string>& props, const std::string& v, int depth = 0)
{
    if (depth > 32) return v;
    std::string out;
    for (size_t i = 0; i < v.size(); )
    {
        if (v.compare(i, 2, "$(") == 0)
        {
            const size_t close = v.find(')', i + 2);
            if (close == std::string::npos) { out += v.substr(i); break; }
            const auto it = props.find(v.substr(i + 2, close - i - 2));
            if (it != props.end()) out += expand(props, it->second, depth + 1);
            i = close + 1;
        }
        else out += v[i++];
    }
    return out;
}

static std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w; ) out.push_back(w);
    return out;
}

// Rows of lang_table.h:  { kCmdLangPascal, "Pascal", "pascal" },
static std::map<std::string, std::string> readMenuLexers(const fs::path& header)
{
    std::map<std::string, std::string> out;
    const std::string text = readFile(header);
    static const std::regex row(R"re(\{\s*kCmdLang\w+\s*,\s*"([^"]+)"\s*,\s*"([^"]*)")re");
    for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) out[(*it)[1]] = (*it)[2];
    return out;
}

// Does the lexer take a keyword list in `slot`? Asked by offering it one: a lexer returns -1 from
// WordListSet for a slot it has no list for. Not DescribeWordListSets(), which several lexers leave
// empty or short although they read the lists (KIXtart describes none, Verilog none of its six). The
// older function-style lexers accept every slot this way, which is harmless: they read the ones they use.
// Returns -1 if Lexilla has no such lexer.
static int lexerTakesSlot(const std::string& lexerName, int slot)
{
    Scintilla::ILexer5* lx = CreateLexer(lexerName.c_str());
    if (!lx) return -1;
    const bool takes = lx->WordListSet(slot, "wxnote_probe") != -1;
    lx->Release();
    return takes ? 1 : 0;
}

// A C++ string literal for `s`, cut at spaces into pieces MSVC accepts (its limit is ~16 KB per piece).
static std::string literal(const std::string& s, const char* indent)
{
    std::string out = "\"";
    size_t col = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"' || c == '\\') out += '\\';
        out += c; ++col;
        if (c == ' ' && col > 100 && i + 1 < s.size()) { out += "\"\n"; out += indent; out += '"'; col = 0; }
    }
    return out + "\"";
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: scite_keywords <scite/src> <lang_table.h> <out.h> [release] [archive-sha256]\n");
        return 2;
    }
    const fs::path sciteSrc = argv[1], menuHeader = argv[2], outPath = argv[3];
    const std::string release = argc > 4 ? argv[4] : "SciTE", archiveSha = argc > 5 ? argv[5] : "";

    const auto props = readProperties(sciteSrc);
    const auto lexers = readMenuLexers(menuHeader);
    const std::string licence = readFile(sciteSrc.parent_path() / "License.txt");
    if (props.empty() || lexers.empty() || licence.empty())
    {
        std::fprintf(stderr, "scite_keywords: nothing read - check the paths (need %s, %s and ../License.txt)\n",
                     sciteSrc.string().c_str(), menuHeader.string().c_str());
        return 1;
    }

    struct Row { std::string language; int slot; std::string words; std::string keys; };
    std::vector<Row> rows;
    int problems = 0;
    for (const auto& [language, keys] : kMap)
    {
        const auto lx = lexers.find(language);
        if (lx == lexers.end()) { std::fprintf(stderr, "  no Language-menu entry named \"%s\"\n", language.c_str()); ++problems; continue; }
        if (lexerTakesSlot(lx->second, 0) < 0) { std::fprintf(stderr, "  Lexilla has no lexer \"%s\" (%s)\n", lx->second.c_str(), language.c_str()); ++problems; continue; }
        std::map<int, std::vector<std::string>> merged;
        std::map<int, std::string> from;
        for (const std::string& key : keys)
        {
            bool any = false;
            for (int n = 0; n < 9; ++n)
            {
                const std::string prop = "keywords" + (n ? std::to_string(n + 1) : std::string()) + "." +
                                         (key[0] == '*' ? key : "$(file.patterns." + key + ")");
                const auto it = props.find(prop);
                if (it == props.end()) continue;
                any = true;
                std::vector<std::string>& dst = merged[n];
                for (const std::string& w : words(expand(props, it->second)))
                    if (std::find(dst.begin(), dst.end(), w) == dst.end()) dst.push_back(w);
                from[n] += (from[n].empty() ? "" : " + ") + prop;
            }
            if (!any) { std::fprintf(stderr, "  SciTE defines no keywords for \"%s\" (%s)\n", key.c_str(), language.c_str()); ++problems; }
        }
        for (const auto& [slot, list] : merged)
        {
            if (list.empty()) continue;
            if (lexerTakesSlot(lx->second, slot) != 1)
            {
                std::fprintf(stderr, "  dropped %s slot %d (%zu words): lexer \"%s\" has no list there\n",
                             language.c_str(), slot, list.size(), lx->second.c_str());
                continue;
            }
            std::string joined;
            for (const std::string& w : list) { if (!joined.empty()) joined += ' '; joined += w; }
            rows.push_back({ language, slot, joined, from[slot] });
        }
    }

    // Lexilla's own lists, for languages SciTE's files leave out (kLexillaTests). The SciTE source archive
    // ships Lexilla beside SciTE, so its test files sit at <archive>/lexilla/test/examples.
    const fs::path tests = sciteSrc.parent_path().parent_path() / "lexilla" / "test" / "examples";
    for (const LexillaTestList& lt : kLexillaTests)
    {
        const fs::path file = tests / lt.dir / "SciTE.properties";
        const auto lx = lexers.find(lt.language);
        if (!fs::exists(file) || lx == lexers.end())
        {
            std::fprintf(stderr, "  no %s (or no Language-menu entry \"%s\")\n", file.string().c_str(), lt.language);
            ++problems; continue;
        }
        std::map<std::string, std::string> p;
        readPropertiesFile(file, p);
        for (int slot : lt.slots)
        {
            const std::string prop = "keywords" + (slot ? std::to_string(slot + 1) : std::string()) + "." + lt.pattern;
            std::vector<std::string> list;
            if (p.count(prop))
                for (const std::string& w : words(expand(p, p[prop])))
                    if (std::find(list.begin(), list.end(), w) == list.end()) list.push_back(w);
            if (list.empty() || lexerTakesSlot(lx->second, slot) != 1)
            {
                std::fprintf(stderr, "  %s slot %d: nothing usable in %s\n", lt.language, slot, file.string().c_str());
                ++problems; continue;
            }
            std::string joined;
            for (const std::string& w : list) { if (!joined.empty()) joined += ' '; joined += w; }
            rows.push_back({ lt.language, slot, joined, std::string("lexilla/test/examples/") + lt.dir + "/SciTE.properties " + prop });
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.language != b.language ? a.language < b.language : a.slot < b.slot;
    });

    // One constant per distinct list: HTML, PHP, ASP and JSP share all six, C/C++/Objective-C theirs.
    std::map<std::string, std::string> nameOf;
    std::vector<std::pair<std::string, std::string>> constants;   // name, words
    for (const Row& r : rows)
        if (!nameOf.count(r.words))
        {
            const std::string name = "k" + std::to_string(constants.size());
            nameOf[r.words] = name;
            constants.push_back({ name, r.words });
        }

    std::ostringstream o;
    o << "// keywords_scite.h - GENERATED by tools/scite_keywords.cpp from " << release << " src/*.properties.\n"
         "// Do not edit by hand: re-run the tool on a newer SciTE instead (see its header for how).\n"
      << (archiveSha.empty() ? "" : "// Source archive SHA-256: " + archiveSha + "\n") <<
         "//\n"
         "// The keyword lists wxNote hands its Lexilla lexers, by Language-menu language and keyword slot.\n"
         "// Included by keywords.h only, which defines WxnKeywordList. The words are SciTE's - and, for Dart,\n"
         "// Nix, TOML and Zig, which SciTE's files leave out, Lexilla's own lists from the same archive -\n"
         "// reproduced under their shared licence, whose notice follows as it requires:\n"
         "//\n";
    std::istringstream lic(licence);
    for (std::string l; std::getline(lic, l); )
    {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();   // the file pads some lines
        o << (l.empty() ? "//" : "// " + l) << "\n";
    }
    o << "\n#pragma once\n\nnamespace wxn_scite_keywords {\n";
    for (const auto& [name, w] : constants)
        o << "inline constexpr char " << name << "[] =\n    " << literal(w, "    ") << ";\n";
    o << "}  // namespace wxn_scite_keywords\n\n"
         "// language, slot, words - sorted by language, then slot. The comment names the SciTE properties\n"
         "// each list was assembled from.\n"
         "inline const WxnKeywordList* wxnSciteKeywordLists(std::size_t& n)\n{\n"
         "    using namespace wxn_scite_keywords;\n"
         "    static const WxnKeywordList t[] = {\n";
    for (const Row& r : rows)
        o << "        { \"" << r.language << "\", " << r.slot << ", " << nameOf[r.words] << " },   // " << r.keys << "\n";
    o << "    };\n    n = sizeof(t) / sizeof(t[0]);\n    return t;\n}\n";

    std::ofstream out(outPath, std::ios::binary);
    out << o.str();
    if (!out) { std::fprintf(stderr, "scite_keywords: cannot write %s\n", outPath.string().c_str()); return 1; }
    std::set<std::string> languages;
    for (const Row& r : rows) languages.insert(r.language);
    std::printf("wrote %s: %zu lists for %zu languages, %zu distinct; %d problem(s) reported above\n",
                outPath.string().c_str(), rows.size(), languages.size(), constants.size(), problems);
    return 0;
}
