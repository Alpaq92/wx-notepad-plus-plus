// SPDX-License-Identifier: Apache-2.0
//
// catalog_selftest - keeps every user-visible string translatable. No GUI, no wx: pure STL, so it runs
// on every CI arch. It reads the repo's own sources and catalogs as data; it links none of them.
//
// 0.14.x shipped ten _() strings that fell back to English in all eight locales, in two independent
// ways. Nothing built and nothing ran over the catalogs, so both defects were invisible until someone
// read the files. One assertion here per way, plus two that keep what SHIPS in step with the template:
//
//   A. ABSENT MSGID - a new _()/wxTRANSLATE() call site whose string was never added to the catalogs.
//      -> every marked literal in src/*.h and src/*.cpp must be a msgid in resources/locale/wxn.pot.
//   B. MALFORMED ENTRY - tools/po2mo.c reads BLANK-LINE-SEPARATED stanzas and takes only the first
//      msgid/msgstr of each, so two shapes are silently dropped rather than diagnosed:
//        * a REAL newline inside a quoted value (they must spell newlines `\n`) - it breaks the quoting
//          AND its blank line truncates the stanza. "Delete the macro \"%s\"?..." was written that way
//          in wxn.pot and in all eight wxn.po files.
//        * an entry appended with no blank line after the previous one - v0.15.0 shipped
//          "Command &Palette..." straight after the " [Sandbox]" stanza, so the Command Palette menu
//          item reached no binary catalog and was English in all eight languages.
//      -> every catalog line must parse, each quoted value opened AND closed on its own line, and each
//      msgid separated from the previous entry by a blank line.
//   C. TEMPLATE/CATALOG DRIFT - a string added to the template but skipped in the eight catalogs.
//      -> every wxn.po carries exactly the .pot's msgid set, with no msgstr left empty (an empty one is
//      worse than missing: gettext returns it, so the UI renders blank instead of falling back).
//   D. STALE BINARY CATALOG - .mo files are committed, so editing a .po without re-running po2mo ships
//      the old strings, and the regional twin dirs (pl_PL, de_DE, ...) hold a byte-identical copy that
//      is easy to forget. -> each wxn.mo is the current compile of its wxn.po (msgstrs compared, not
//      just msgids), and each twin matches its base byte for byte.
//
// The source side is a real C++ lexer, not a regex. Comments are skipped: three PROSE comments in src/
// discuss _("...") / wxTRANSLATE("...") in passing, and a regex reports those as missing strings - that
// decoy is what made `"..."` in menu_model.h look like a genuine untranslated call site. Raw string
// literals are understood (R"delim(...)delim" - main.cpp alone has 60) so a `//` or a quote inside one
// cannot desync the scan, as are char literals and the adjacent-literal concatenation that the
// multi-line _("a\n" "b") call sites are written with.
//
// Both decoders reduce to the BYTES the runtime compares - the catalog side mirrors tools/po2mo.c
// exactly - so this checks what gettext will really look up, not that two spellings look alike.
//
//   cmake --build build --target catalog_selftest && build/bin/catalog_selftest
//
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef SRC_DIR
#error "SRC_DIR must be defined (see the catalog_selftest target in CMakeLists.txt)"
#endif
#ifndef LOCALE_DIR
#error "LOCALE_DIR must be defined (see the catalog_selftest target in CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

static int g_fail = 0, g_pass = 0;

static void check(bool ok, const std::string& what)
{
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

// A detail line under a failed check - already counted by its check(), so it does not bump the tally.
static void detail(const std::string& msg) { std::printf("        %s\n", msg.c_str()); }

// Printable one-line form of a catalog string, for diagnostics.
static std::string repr(const std::string& s, size_t cap = 100)
{
    std::string o = "\"";
    bool cut = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (o.size() >= cap) { cut = true; break; }
        const char c = s[i];
        if      (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else                o += c;
    }
    return o + (cut ? "...\"" : "\"");
}

static bool readFile(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// ---- C++ source lexing ------------------------------------------------------------------------------

enum SpanKind { KComment, KString, KOpaque };   // KOpaque: char literal or raw string - never a msgid
struct Span { SpanKind kind; size_t b, e; };

static bool isIdent(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static bool isDigit(char c) { return c >= '0' && c <= '9'; }

// One pass classifying every comment and literal; everything else is code. `markers` collects, for each
// `_(` / `wxTRANSLATE(` token, the offset just past its '('. Identifiers and numbers are consumed WHOLE,
// so `myR"x"` is not read as a raw string and a digit separator (1'000) is not read as a char literal.
// An unterminated comment or literal is a hard error: a desynced lexer would silently under-report call
// sites, which is exactly the failure this test exists to catch.
static bool lexSource(const std::string& s, std::vector<Span>& spans, std::vector<size_t>& markers,
                      std::string& err)
{
    const size_t n = s.size();
    for (size_t i = 0; i < n; ) {
        const char c = s[i];

        if (isIdent(c) && !isDigit(c)) {
            size_t j = i;
            while (j < n && isIdent(s[j])) ++j;
            const std::string id = s.substr(i, j - i);
            if (j < n && s[j] == '"' && (id == "R" || id == "LR" || id == "uR" || id == "UR" || id == "u8R")) {
                size_t d = j + 1;                                   // R"delim( ... )delim"
                while (d < n && s[d] != '(' && s[d] != '"') ++d;
                if (d >= n || s[d] != '(') { err = "malformed raw string literal"; return false; }
                const std::string close = ")" + s.substr(j + 1, d - (j + 1)) + "\"";
                const size_t e = s.find(close, d + 1);
                if (e == std::string::npos) { err = "unterminated raw string literal"; return false; }
                spans.push_back({ KOpaque, i, e + close.size() });
                i = e + close.size();
                continue;
            }
            if (id == "_" || id == "wxTRANSLATE") {
                size_t k = j;
                while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
                if (k < n && s[k] == '(') markers.push_back(k + 1);
            }
            i = j;
            continue;
        }
        if (isDigit(c)) {
            size_t j = i;
            while (j < n && (isIdent(s[j]) || s[j] == '.' || (s[j] == '\'' && j + 1 < n && isIdent(s[j + 1])))) ++j;
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            size_t j = i + 2;
            while (j < n && s[j] != '\n') {
                if (s[j] == '\\') {                                  // a line continuation extends it
                    size_t k = j + 1;
                    if (k < n && s[k] == '\r') ++k;
                    if (k < n && s[k] == '\n') { j = k + 1; continue; }
                }
                ++j;
            }
            spans.push_back({ KComment, i, j });
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) ++j;
            if (j + 1 >= n) { err = "unterminated /* */ comment"; return false; }
            spans.push_back({ KComment, i, j + 2 });
            i = j + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && s[j] != c) { if (s[j] == '\\') ++j; ++j; }
            if (j >= n) { err = std::string("unterminated ") + (c == '"' ? "string" : "character") + " literal"; return false; }
            spans.push_back({ c == '"' ? KString : KOpaque, i, j + 1 });
            i = j + 1;
            continue;
        }
        ++i;
    }
    return true;
}

// Decode a C++ "..." literal body to bytes. Only the escapes tools/po2mo.c can round-trip are accepted;
// anything else is reported, because the string could not be represented as a msgid even if it were
// added to the catalogs.
static bool decodeCxx(const std::string& s, size_t b, size_t e, std::string& out, std::string& err)
{
    for (size_t i = b + 1; i + 1 < e; ++i) {
        const char c = s[i];
        if (c != '\\') { out += c; continue; }
        if (i + 2 > e - 1) { err = "literal ends inside an escape"; return false; }
        const char x = s[++i];
        if      (x == 'n')  out += '\n';
        else if (x == 't')  out += '\t';
        else if (x == '\\') out += '\\';
        else if (x == '"')  out += '"';
        else {
            err = std::string("escape \\") + x + " has no .po representation (tools/po2mo.c decodes it "
                  "as a bare '" + x + "'); use one of \\n \\t \\\\ \\\"";
            return false;
        }
    }
    return true;
}

// ---- .po / .pot parsing -----------------------------------------------------------------------------

struct Catalog {
    std::map<std::string, std::string> entries;   // msgid -> msgstr (the header's empty msgid excluded)
    std::vector<std::string> errors;
};

static std::set<std::string> keysOf(const std::map<std::string, std::string>& m)
{
    std::set<std::string> k;
    for (const auto& kv : m) k.insert(kv.first);
    return k;
}

// Read one `"..."` value starting at or after `from`, decoding it the way tools/po2mo.c does (\n \t \\ \"
// recognised; any other escape drops the backslash and keeps the char). The value MUST open and close on
// this one line - that rule is check B.
static bool quotedValue(const std::string& line, size_t from, std::string& out, std::string& why)
{
    const size_t q = line.find_first_not_of(" \t", from);
    if (q == std::string::npos || line[q] != '"') { why = "expected a quoted \"...\" value"; return false; }

    std::string v;
    size_t i = q + 1;
    for (; i < line.size() && line[i] != '"'; ++i) {
        if (line[i] != '\\') { v += line[i]; continue; }
        if (i + 1 >= line.size()) { why = "the line ends inside an escape"; return false; }
        const char x = line[++i];
        if      (x == 'n')  v += '\n';
        else if (x == 't')  v += '\t';
        else if (x == '\\') v += '\\';
        else if (x == '"')  v += '"';
        else                v += x;      // tools/po2mo.c drops the backslash of an unknown escape
    }
    if (i >= line.size()) {
        why = "the quoted value is not closed on this line - a newline inside it must be written \\n, "
              "not typed literally";
        return false;
    }
    if (line.find_first_not_of(" \t", i + 1) != std::string::npos) { why = "trailing text after the closing quote"; return false; }
    out = v;
    return true;
}

static bool isKeyword(const std::string& kw)
{
    return kw == "msgid" || kw == "msgstr" || kw == "msgid_plural" || kw == "msgctxt"
        || kw.rfind("msgstr[", 0) == 0;
}

// Parse a .po/.pot into its msgid -> msgstr map, recording every line that does not conform. Mirrors
// po2mo's view of the format: '#' comments skipped, blank lines end a stanza, quoted continuation lines
// extend the preceding keyword. msgctxt/msgid_plural are accepted but ignored - po2mo has no contexts or
// plurals either, and the catalogs use none.
static Catalog parsePo(const std::string& text, const std::string& name)
{
    Catalog cat;
    std::istringstream in(text);
    std::string line, curId, curStr;
    int lineno = 0;
    bool haveId = false, inId = false, inStr = false, haveKeyword = false;

    // A stanza is complete once we have seen its msgid; store it (with whatever msgstr followed).
    auto flush = [&] {
        if (haveId && !curId.empty() && !cat.entries.emplace(curId, curStr).second)
            cat.errors.push_back(name + ": duplicate msgid " + repr(curId));
        curId.clear();
        curStr.clear();
        haveId = inId = inStr = false;
    };

    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string loc = name + ":" + std::to_string(lineno);

        const size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) { flush(); haveKeyword = false; continue; }   // blank ends the stanza
        if (line[s] == '#') continue;                                             // translator comment

        std::string kw;
        size_t from = s;
        if (line[s] != '"') {
            size_t k = s;
            while (k < line.size() && line[k] != ' ' && line[k] != '\t') ++k;
            kw = line.substr(s, k - s);
            from = k;
            if (!isKeyword(kw)) {
                cat.errors.push_back(loc + ": unrecognised line: " + line.substr(s, 60));
                flush();
                haveKeyword = false;
                continue;
            }
        } else if (!haveKeyword) {
            cat.errors.push_back(loc + ": a \"...\" continuation with no preceding msgid/msgstr");
            continue;
        }

        std::string value, why;
        if (!quotedValue(line, from, value, why)) {
            cat.errors.push_back(loc + ": " + why);
            flush();
            haveKeyword = false;
            continue;
        }

        if (kw.empty()) {                       // continuation of whatever keyword came before
            if (inId)       curId += value;
            else if (inStr) curStr += value;
        } else if (kw == "msgid") {
            // A stanza runs to the next BLANK line, and tools/po2mo.c takes only the first msgid/msgstr
            // in one - so a second msgid with no blank line before it is silently dropped from the .mo.
            // v0.15.0 shipped exactly that: "Command &Palette..." was appended straight after the
            // " [Sandbox]" stanza and never reached any of the eight binary catalogs.
            if (haveId)
                cat.errors.push_back(loc + ": msgid not separated from the previous entry by a blank "
                                           "line - po2mo keeps only the first entry of a stanza, so "
                                           "this one is dropped: " + repr(value));
            flush();
            haveId = inId = true;
            curId = value;
        } else {
            inId = false;
            inStr = (kw == "msgstr");
            if (inStr) curStr = value;
        }
        haveKeyword = true;
    }
    flush();
    return cat;
}

// ---- .mo reading ------------------------------------------------------------------------------------

// Minimal reader for the GNU .mo format tools/po2mo.c writes: a header, then a key table and a value
// table of (length, offset) pairs into the two string blobs. Recovers the whole msgid -> msgstr map, so
// a translation edited without re-running po2mo is caught, not just an added string. Both byte orders
// are accepted, so the check is meaningful on the big-endian CI legs too.
static bool readMo(const std::string& b, std::map<std::string, std::string>& out, std::string& why)
{
    auto u32 = [&b](size_t off, bool swap) {
        uint32_t v = (uint32_t)(uint8_t)b[off]            | ((uint32_t)(uint8_t)b[off + 1] << 8)
                   | ((uint32_t)(uint8_t)b[off + 2] << 16) | ((uint32_t)(uint8_t)b[off + 3] << 24);
        if (swap) v = ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24);
        return v;
    };
    if (b.size() < 28) { why = "shorter than a .mo header"; return false; }
    const uint32_t magic = u32(0, false);
    bool swap;
    if      (magic == 0x950412deu) swap = false;
    else if (magic == 0xde120495u) swap = true;
    else { why = "not a .mo file (bad magic)"; return false; }

    const uint32_t count = u32(8, swap), keyTab = u32(12, swap), valTab = u32(16, swap);
    if (count > b.size() / 8) { why = "implausible entry count"; return false; }
    for (uint32_t i = 0; i < count; ++i) {
        std::string pair[2];
        for (int t = 0; t < 2; ++t) {
            const size_t rec = (size_t)(t == 0 ? keyTab : valTab) + 8u * (size_t)i;
            if (rec + 8 > b.size()) { why = "a string table runs past the end of the file"; return false; }
            const uint32_t len = u32(rec, swap), off = u32(rec + 4, swap);
            if ((size_t)off + (size_t)len > b.size()) { why = "a string runs past the end of the file"; return false; }
            pair[t] = b.substr(off, len);
        }
        if (!pair[0].empty()) out.emplace(pair[0], pair[1]);   // the header's empty msgid is not an entry
    }
    return true;
}

// ---- the checks -------------------------------------------------------------------------------------

static size_t lineOf(const std::string& s, size_t off)
{
    return 1 + (size_t)std::count(s.begin(), s.begin() + (ptrdiff_t)std::min(off, s.size()), '\n');
}

// Advance past whitespace and any comment beginning there, so `_("a" /*x*/ "b")` still reads as one call.
static size_t skipGaps(const std::string& s, const std::vector<Span>& spans, size_t q)
{
    for (;;) {
        while (q < s.size() && (s[q] == ' ' || s[q] == '\t' || s[q] == '\r' || s[q] == '\n')) ++q;
        const auto it = std::lower_bound(spans.begin(), spans.end(), q,
                                         [](const Span& sp, size_t v) { return sp.b < v; });
        if (it != spans.end() && it->b == q && it->kind == KComment) { q = it->e; continue; }
        return q;
    }
}

int main()
{
    std::printf("catalog_selftest\n");

    const fs::path srcDir = SRC_DIR;
    const fs::path locDir = LOCALE_DIR;

    // ---- the template ---------------------------------------------------------------------------
    std::string potText;
    if (!readFile(locDir / "wxn.pot", potText)) {
        check(false, "read " + (locDir / "wxn.pot").string());
        return 1;
    }
    const Catalog pot = parsePo(potText, "wxn.pot");
    const std::set<std::string> potIds = keysOf(pot.entries);
    check(pot.errors.empty(), "B: wxn.pot is well formed (" + std::to_string(potIds.size()) + " msgids)");
    for (const auto& e : pot.errors) detail(e);

    // ---- A: every marked literal in src/ is a msgid in the template ------------------------------
    std::vector<fs::path> sources;
    for (const auto& de : fs::directory_iterator(srcDir)) {
        if (!de.is_regular_file()) continue;
        const std::string ext = de.path().extension().string();
        if (ext == ".h" || ext == ".cpp") sources.push_back(de.path());
    }
    std::sort(sources.begin(), sources.end());

    size_t scanned = 0, missing = 0, broken = 0;
    for (const auto& p : sources) {
        const std::string name = p.filename().string();
        std::string text;
        if (!readFile(p, text)) { check(false, "read " + p.string()); continue; }

        std::vector<Span> spans;
        std::vector<size_t> markers;
        std::string err;
        if (!lexSource(text, spans, markers, err)) { check(false, name + ": " + err); ++broken; continue; }

        for (size_t at : markers) {
            // Collect the run of adjacent string literals that forms this call's argument. A call whose
            // argument is not a plain literal (a variable, or a raw string) simply has none - skip it.
            std::string lit;
            size_t q = skipGaps(text, spans, at), taken = 0;
            for (;;) {
                const auto it = std::lower_bound(spans.begin(), spans.end(), q,
                                                 [](const Span& sp, size_t v) { return sp.b < v; });
                if (it == spans.end() || it->b != q || it->kind != KString) break;
                if (!decodeCxx(text, it->b, it->e, lit, err)) {
                    check(false, name + ":" + std::to_string(lineOf(text, it->b)) + ": " + err);
                    ++broken;
                    lit.clear();
                    taken = 0;
                    break;
                }
                ++taken;
                q = skipGaps(text, spans, it->e);
            }
            if (!taken || lit.empty()) continue;
            ++scanned;
            if (!potIds.count(lit)) {
                ++missing;
                detail(name + ":" + std::to_string(lineOf(text, at)) + " not in wxn.pot: " + repr(lit));
            }
        }
    }
    check(missing == 0 && broken == 0,
          "A: all " + std::to_string(scanned) + " _()/wxTRANSLATE() literals in " +
          std::to_string(sources.size()) + " src/ files are msgids in wxn.pot");

    // ---- B/C/D: the per-language catalogs --------------------------------------------------------
    std::vector<fs::path> pos;
    for (const auto& de : fs::directory_iterator(locDir)) {
        const fs::path po = de.path() / "LC_MESSAGES" / "wxn.po";
        if (de.is_directory() && fs::exists(po)) pos.push_back(po);
    }
    std::sort(pos.begin(), pos.end());
    check(!pos.empty(), "found " + std::to_string(pos.size()) + " wxn.po catalogs under resources/locale");

    for (const auto& p : pos) {
        const std::string lang = p.parent_path().parent_path().filename().string();
        std::string text;
        if (!readFile(p, text)) { check(false, "read " + p.string()); continue; }

        const Catalog cat = parsePo(text, lang + "/wxn.po");
        const std::set<std::string> catIds = keysOf(cat.entries);
        check(cat.errors.empty(), "B: " + lang + "/wxn.po is well formed (" + std::to_string(catIds.size()) + " msgids)");
        for (const auto& e : cat.errors) detail(e);

        std::vector<std::string> onlyPot, onlyPo;
        std::set_difference(potIds.begin(), potIds.end(), catIds.begin(), catIds.end(), std::back_inserter(onlyPot));
        std::set_difference(catIds.begin(), catIds.end(), potIds.begin(), potIds.end(), std::back_inserter(onlyPo));
        check(onlyPot.empty() && onlyPo.empty(), "C: " + lang + "/wxn.po covers exactly wxn.pot's msgids");
        for (size_t i = 0; i < onlyPot.size() && i < 10; ++i) detail("only in wxn.pot: " + repr(onlyPot[i]));
        for (size_t i = 0; i < onlyPo.size()  && i < 10; ++i) detail("only in " + lang + ": " + repr(onlyPo[i]));

        // C: an empty msgstr is NOT "untranslated" at runtime - gettext returns the empty string, so the
        // UI renders a blank label instead of falling back to English. A .po must translate everything.
        std::vector<std::string> blank;
        for (const auto& kv : cat.entries)
            if (kv.second.empty()) blank.push_back(kv.first);
        check(blank.empty(), "C: " + lang + "/wxn.po has no empty translations");
        for (size_t i = 0; i < blank.size() && i < 10; ++i) detail("empty msgstr for: " + repr(blank[i]));

        // D: the committed binary catalog must be the compile of THIS .po - msgstrs included, so editing
        // a translation without re-running po2mo is caught too, not just adding a string.
        const fs::path moPath = p.parent_path() / "wxn.mo";
        std::string mo;
        if (!readFile(moPath, mo)) { check(false, "read " + moPath.string()); continue; }
        std::map<std::string, std::string> moEntries;
        std::string why;
        if (!readMo(mo, moEntries, why)) { check(false, "D: " + lang + "/wxn.mo: " + why); continue; }
        check(moEntries == cat.entries,
              "D: " + lang + "/wxn.mo is up to date with its wxn.po (run po2mo after editing)");
        if (moEntries != cat.entries) {
            size_t shown = 0;
            for (const auto& kv : cat.entries) {
                if (shown >= 10) break;
                const auto it = moEntries.find(kv.first);
                if (it == moEntries.end()) { detail("in the .po but not the .mo: " + repr(kv.first)); ++shown; }
                else if (it->second != kv.second) { detail("translation differs for: " + repr(kv.first)); ++shown; }
            }
        }

        // D: <lang>_<REGION>/ holds a byte-identical copy - easy to update one and forget the other.
        for (const auto& de : fs::directory_iterator(locDir)) {
            if (!de.is_directory()) continue;
            const std::string twin = de.path().filename().string();
            if (twin.rfind(lang + "_", 0) != 0) continue;
            std::string twinMo;
            const fs::path twinPath = de.path() / "LC_MESSAGES" / "wxn.mo";
            if (!readFile(twinPath, twinMo)) { check(false, "read " + twinPath.string()); continue; }
            check(twinMo == mo, "D: " + twin + "/wxn.mo is a byte-identical copy of " + lang + "/wxn.mo");
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
