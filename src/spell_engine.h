// SPDX-License-Identifier: Apache-2.0
#pragma once
// =====================================================================
// spell_engine.h - a pluggable spell-check backend behind a tiny interface, so the Scintilla squiggle/UX
// layer is written once and the engine swaps per platform. The Windows backend uses the OS ISpellChecker
// (spellcheck.h, Windows 8+) - no bundled library or dictionary, and per-language OS dictionaries. Other
// platforms return no engine for now (a bundled Hunspell backend, decided in
// docs/MISSING_FUNCTIONALITY.md, is the planned follow-up behind this same interface).
//
// tokenizeForSpell() is a PURE, cross-platform helper (no OS deps): it splits a UTF-8 run into checkable
// word tokens, breaking identifiers on camelCase / PascalCase / underscore / digit boundaries the way a
// code-aware checker should, so `fooBar_baz2` yields `foo`,`Bar`,`baz` instead of one "misspelled" blob.
// =====================================================================
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <cctype>
#include <cwctype>
#include <cstdlib>

namespace wxn { namespace spell {

struct Engine {
    virtual ~Engine() = default;
    virtual bool available() const = 0;
    virtual bool check(const std::string& utf8word) = 0;                    // true = spelled correctly
    virtual std::vector<std::string> suggest(const std::string& utf8word) = 0;
    virtual bool add(const std::string& utf8word) = 0;                     // add to the user dictionary
};

// Which engine to use, a user preference on platforms that HAVE a native checker (Windows/macOS). On
// Linux there is no native checker, so makeEngine always uses Hunspell regardless of this value.
//   Native             - OS checker only; if it lacks the language, spell-check is unavailable.
//   NativeThenHunspell - OS checker, falling back to the bundled Hunspell + en_US when it lacks it (default).
//   Hunspell           - always the bundled Hunspell, ignoring any OS checker.
enum class Backend { Native, NativeThenHunspell, Hunspell };

// A checkable sub-word: byte offset + length within the input string.
struct WordSpan { int start; int len; };

// Split UTF-8 text into checkable word tokens. Letters (ASCII a-z/A-Z and any byte >= 0x80, so accented
// words stay whole) plus interior apostrophes form segments; each ASCII segment is further split on
// camelCase / PascalCase boundaries. Tokens shorter than 2 letters, or containing a digit, are dropped
// (digits already break segments; the length gate skips 1-letter fragments and most acronym noise).
inline std::vector<WordSpan> tokenizeForSpell(const std::string& s)
{
    std::vector<WordSpan> out;
    const int n = (int)s.size();
    auto isLetterByte = [](unsigned char c) { return (c >= 0x80) || std::isalpha(c); };

    int i = 0;
    while (i < n) {
        // Grow a segment of letter bytes, allowing an apostrophe only between two letters.
        if (!isLetterByte((unsigned char)s[i])) { ++i; continue; }
        int segStart = i;
        while (i < n) {
            const unsigned char c = (unsigned char)s[i];
            if (isLetterByte(c)) { ++i; }
            else if (c == '\'' && i + 1 < n && isLetterByte((unsigned char)s[i + 1])) { ++i; }
            else break;
        }
        const int segEnd = i;   // [segStart, segEnd)

        // Split the segment at camelCase / PascalCase boundaries (ASCII case only; high bytes never split).
        auto emit = [&](int a, int b) {
            // trim leading/trailing apostrophes
            while (a < b && s[a] == '\'') ++a;
            while (b > a && s[b - 1] == '\'') --b;
            if (b - a >= 2) out.push_back({a, b - a});
        };
        int start = segStart;
        for (int k = segStart + 1; k < segEnd; ++k) {
            const unsigned char p = (unsigned char)s[k - 1];
            const unsigned char c = (unsigned char)s[k];
            const bool lowerToUpper = p < 0x80 && c < 0x80 && std::islower(p) && std::isupper(c);         // aB
            const bool acronymEnd   = p < 0x80 && c < 0x80 && std::isupper(p) && std::isupper(c)
                                      && k + 1 < segEnd && (unsigned char)s[k + 1] < 0x80 && std::islower((unsigned char)s[k + 1]); // ABc
            if (lowerToUpper || acronymEnd) { emit(start, k); start = k; }
        }
        emit(start, segEnd);
    }
    return out;
}

std::unique_ptr<Engine> makeEngine(const std::string& lang, Backend backend = Backend::NativeThenHunspell); // null if unavailable

}} // namespace wxn::spell  (closed here so the platform backends below own their headers/namespaces)

// ---- Hunspell backend: Linux native engine + always-available cross-platform fallback ---------------
// libhunspell (used under its MPL-1.1 arm) loaded with the bundled SCOWL en_US dictionary. Defined for
// ALL platforms - not only the #else below - so the Windows/macOS makeEngine() can fall through to
// makeHunspellEngine() when the OS spell checker is unavailable or lacks the requested language. That is
// exactly how a non-English Windows with no en-US pack gets real English spell-checking (bundled Hunspell)
// instead of silently checking against whatever OS language happens to be installed.
#include <hunspell/hunspell.hxx>
#include <wx/string.h>
#include <wx/strconv.h>
#include <wx/filefn.h>
#include <wx/file.h>
#include <wx/textfile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>

namespace wxn { namespace spell {

// Path to hand to Hunspell's Hunspell(const char*,...) ctor. On MSVC, Hunspell's file opener (csutil.cxx
// myopen) only decodes the path as UTF-8 (via MultiByteToWideChar) when it carries the \\?\ long-path
// prefix; without it, it does a narrow open that mangles non-ASCII paths (e.g. an accented user name). So
// prefix + UTF-8 there, and plain UTF-8 bytes everywhere else (POSIX opens the bytes directly).
inline std::string hunspellPath(const wxString& p) {
#if defined(_WIN32) && defined(_MSC_VER)
    wxString abs = p; abs.Replace("/", "\\");
    return std::string(("\\\\?\\" + abs).utf8_str());
#else
    return std::string(p.utf8_str());
#endif
}

struct HunspellEngine : Engine {
    std::unique_ptr<Hunspell> h;
    std::unique_ptr<wxCSConv> conv;      // non-null only if the dict is NOT UTF-8 (our bundled en_US IS)
    wxString userDicPath;                // <userDataDir>/dictionaries/user.dic - persists add()ed words
    std::set<std::string> m_userWords;   // UTF-8 words already in user.dic - dedups repeated add()s
    bool ready = false;

    HunspellEngine(const std::string& aff, const std::string& dic, const wxString& userDic)
        : userDicPath(userDic)
    {
        h.reset(new Hunspell(aff.c_str(), dic.c_str()));
        const std::string enc = h->get_dict_encoding();          // "" when the .aff has no SET line
        const wxString e = wxString::FromUTF8(enc.c_str()).Upper();
        if (!enc.empty() && e != "UTF-8" && e != "UTF8")         // only explicit UTF-8 skips conversion
            conv.reset(new wxCSConv(wxString::FromUTF8(enc.c_str())));
        // The ctor never throws over a missing/corrupt dict, but then "spells" every word wrong; only
        // report available if a known-good word passes, so makeHunspellEngine can try the next path.
        // NOTE: the probe word "the" is English-specific - correct for the only language requested today
        // (en-US). If per-language selection is ever wired to request other languages, a non-English dict
        // would fail this probe: switch to a language-agnostic load check (a per-language probe word) then.
        std::string probe;
        ready = toDict("the", probe) && h->spell(probe);
        if (ready) loadUserDic();
    }

    // UTF-8 (our API charset) -> dictionary charset; a no-op when the dict is UTF-8. Returns false when a
    // character can't be represented in the dict charset (caller then treats the word as uncheckable).
    bool toDict(const std::string& u8, std::string& out) const {
        if (!conv) { out = u8; return true; }
        const wxString ws = wxString::FromUTF8(u8.c_str(), u8.size());
        if (ws.empty() && !u8.empty()) return false;
        const wxCharBuffer b = ws.mb_str(*conv);
        if (!b || (b.length() == 0 && !u8.empty())) return false;
        out.assign(b.data(), b.length());
        return true;
    }
    std::string fromDict(const std::string& s) const {           // dict charset -> UTF-8
        if (!conv) return s;
        return std::string(wxString(s.c_str(), *conv).utf8_str());
    }

    // Re-add the words the user previously learned (our own plain UTF-8, one word per line), and seed the
    // dedup set so add() won't re-append them. wxLogNull: an existing-but-unreadable user.dic (locked /
    // bad ACL) must not pop a wxLog dialog - matching the userDataDir I/O convention used elsewhere.
    void loadUserDic() {
        if (userDicPath.empty() || !wxFileExists(userDicPath)) return;
        wxLogNull noLog;
        wxTextFile f(userDicPath);
        if (!f.Open()) return;
        for (wxString line = f.GetFirstLine(); ; line = f.GetNextLine()) {
            const wxString w = line.Trim(true).Trim(false);
            if (!w.empty()) {
                const std::string u8(w.utf8_str());
                std::string d;
                if (toDict(u8, d)) { h->add(d); m_userWords.insert(u8); }
            }
            if (f.Eof()) break;
        }
        f.Close();
    }

    bool available() const override { return ready; }

    bool check(const std::string& w) override {
        if (!ready || w.empty()) return true;
        std::string d;
        if (!toDict(w, d)) return true;          // uncheckable -> don't flag as an error
        return h->spell(d);                      // bool: true = spelled correctly (Hunspell >= 1.5.0)
    }
    std::vector<std::string> suggest(const std::string& w) override {
        std::vector<std::string> out;
        if (!ready || w.empty()) return out;
        std::string d;
        if (!toDict(w, d)) return out;
        for (const std::string& s : h->suggest(d)) out.push_back(fromDict(s));
        return out;
    }
    bool add(const std::string& w) override {
        if (!ready || w.empty()) return false;
        std::string d;
        if (!toDict(w, d)) return false;
        if (h->add(d) != 0) return false;        // session add: Hunspell never writes the .dic itself
        // Persist for next launch, but only a word the engine can ACTUALLY learn (spell() now accepts it)
        // and that isn't already stored. Otherwise a word add() accepts yet spell() still rejects - e.g. a
        // curly-apostrophe token the .aff's ICONV normalizes on lookup but not on add - would be re-appended
        // on every "Add to Dictionary" click, growing user.dic without bound (and re-added each startup).
        if (!userDicPath.empty() && h->spell(d) && m_userWords.insert(w).second)
            persistUserWord(w);
        return true;
    }

    // Append the UTF-8 word to <userDataDir>/dictionaries/user.dic (created on first use). The OS backends
    // persist learned words via the OS; this path has no store of its own otherwise. Best-effort - the
    // wxLogNull spans the Mkdir too, so neither a failed dir-create nor a failed write pops a wxLog dialog.
    void persistUserWord(const std::string& w) {
        wxLogNull noLog;
        wxFileName fn(userDicPath);
        if (!fn.DirExists()) fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        wxFile f;
        if (f.Open(userDicPath, wxFile::write_append)) {
            const std::string line = w + "\n";
            f.Write(line.data(), line.size());
        }
    }
};

// Load the bundled/user dictionary for `lang` (e.g. "en-US" -> en_US.{aff,dic}); null if none is usable.
// Searches <userDataDir>/dictionaries first (user drop-ins + user.dic) then <exeDir>/dictionaries (shipped),
// so a user can override or add languages without touching the install.
inline std::unique_ptr<Engine> makeHunspellEngine(const std::string& lang) {
    wxString base = lang.empty() ? wxString("en_US") : wxString::FromUTF8(lang.c_str());
    base.Replace("-", "_");                                       // "en-US" -> "en_US" on-disk basename

    const wxString exeDir  = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
    const wxString userDir = wxStandardPaths::Get().GetUserDataDir() + wxFILE_SEP_PATH + "dictionaries";
    const wxString shipDir = exeDir + wxFILE_SEP_PATH + "dictionaries";
    const wxString userDic = userDir + wxFILE_SEP_PATH + "user.dic";

    const wxString dirs[] = { userDir, shipDir };                // user drop-ins win over the shipped copy
    for (const wxString& dir : dirs) {
        const wxString aff = dir + wxFILE_SEP_PATH + base + ".aff";
        const wxString dic = dir + wxFILE_SEP_PATH + base + ".dic";
        if (wxFileExists(aff) && wxFileExists(dic)) {
            try {
                auto e = std::make_unique<HunspellEngine>(hunspellPath(aff), hunspellPath(dic), userDic);
                if (e->available()) return std::unique_ptr<Engine>(std::move(e));
            } catch (const std::exception&) {
                // OOM / ctor failure -> report unavailable (fall through to the next dir, then nullptr)
                // rather than letting the exception escape makeEngine into the wx event loop (terminate).
            }
        }
    }
    return nullptr;
}

// Installed dictionary basenames (e.g. "en_US", "de_DE") that have BOTH an .aff and a .dic, scanned from
// <userDataDir>/dictionaries and <exeDir>/dictionaries. Deduped (user copy wins by set-insert) and sorted.
// Used by the View ▸ Spell Check ▸ Dictionary picker and the Preferences dictionary list.
inline std::vector<std::string> listDictionaries() {
    const wxString exeDir  = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
    const wxString userDir = wxStandardPaths::Get().GetUserDataDir() + wxFILE_SEP_PATH + "dictionaries";
    const wxString shipDir = exeDir + wxFILE_SEP_PATH + "dictionaries";
    std::set<std::string> bases;
    const wxString dirs[] = { userDir, shipDir };
    for (const wxString& dir : dirs) {
        if (!wxDirExists(dir)) continue;
        wxDir d(dir);
        if (!d.IsOpened()) continue;
        wxString name;
        for (bool ok = d.GetFirst(&name, "*.aff", wxDIR_FILES); ok; ok = d.GetNext(&name)) {
            const wxString base = name.BeforeLast('.');          // strip ".aff"
            if (!base.empty() && wxFileExists(dir + wxFILE_SEP_PATH + base + ".dic"))
                bases.insert(std::string(base.utf8_str()));
        }
    }
    return std::vector<std::string>(bases.begin(), bases.end());
}

}} // namespace wxn::spell

// ---- Windows backend: OS ISpellChecker --------------------------------------------------------------
#ifdef __WXMSW__
#include <windows.h>
#include <spellcheck.h>

namespace wxn { namespace spell { namespace detail {

inline std::wstring toW(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
inline std::string fromW(const wchar_t* w) {
    if (!w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s((size_t)(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

struct WinEngine : Engine {
    ISpellCheckerFactory* factory = nullptr;
    ISpellChecker*        checker = nullptr;
    bool ready = false;

    explicit WinEngine(const std::string& lang) {
        // COM is already initialised on the main thread by wx (OleInitialize). If not, this simply fails
        // and the engine reports unavailable - never a crash.
        if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(ISpellCheckerFactory), reinterpret_cast<void**>(&factory))) || !factory)
            return;
        std::wstring wlang = toW(lang.empty() ? std::string("en-US") : lang);
        BOOL supported = FALSE;
        if (FAILED(factory->IsSupported(wlang.c_str(), &supported)) || !supported) {
            // The requested language pack isn't installed (common: en-US on a non-English Windows). Prefer
            // another variant of the SAME language (e.g. en-GB when en-US is absent). Do NOT fall back to an
            // unrelated OS language: an English-first request must never end up silently checking against,
            // say, Polish. If no same-language variant exists we report unavailable, and makeEngine() falls
            // through to the bundled Hunspell + en_US dictionary - real English checking on any OS language.
            std::wstring primary = wlang.substr(0, wlang.find(L'-'));   // "en-US" -> "en"
            for (wchar_t& ch : primary) ch = (wchar_t)towlower(ch);
            std::wstring sameLanguage;
            IEnumString* langs = nullptr;
            if (!primary.empty() && SUCCEEDED(factory->get_SupportedLanguages(&langs)) && langs) {
                LPOLESTR item = nullptr; ULONG got = 0;
                while (sameLanguage.empty() && langs->Next(1, &item, &got) == S_OK && got == 1 && item) {
                    std::wstring tag(item); CoTaskMemFree(item); item = nullptr;
                    std::wstring low = tag; for (wchar_t& ch : low) ch = (wchar_t)towlower(ch);
                    if (low == primary || (low.size() > primary.size() &&
                        low.compare(0, primary.size(), primary) == 0 && low[primary.size()] == L'-'))
                        sameLanguage = tag;                // preserve original case for CreateSpellChecker
                }
                langs->Release();
            }
            if (sameLanguage.empty()) return;   // no OS pack for this language -> unavailable (use Hunspell)
            wlang = sameLanguage;
        }
        if (FAILED(factory->CreateSpellChecker(wlang.c_str(), &checker)) || !checker) return;
        ready = true;
    }
    ~WinEngine() override { if (checker) checker->Release(); if (factory) factory->Release(); }

    bool available() const override { return ready; }

    bool check(const std::string& word) override {
        if (!ready || word.empty()) return true;
        IEnumSpellingError* errs = nullptr;
        if (FAILED(checker->Check(toW(word).c_str(), &errs)) || !errs) return true;
        ISpellingError* e = nullptr;
        const bool bad = (errs->Next(&e) == S_OK);   // any error over the word => misspelled
        if (e) e->Release();
        errs->Release();
        return !bad;
    }
    std::vector<std::string> suggest(const std::string& word) override {
        std::vector<std::string> out;
        if (!ready || word.empty()) return out;
        IEnumString* sug = nullptr;
        if (FAILED(checker->Suggest(toW(word).c_str(), &sug)) || !sug) return out;
        LPOLESTR item = nullptr; ULONG got = 0;
        while (out.size() < 8 && sug->Next(1, &item, &got) == S_OK && got == 1 && item) {
            out.push_back(fromW(item));
            CoTaskMemFree(item); item = nullptr;
        }
        sug->Release();
        return out;
    }
    bool add(const std::string& word) override {
        return ready && !word.empty() && SUCCEEDED(checker->Add(toW(word).c_str()));
    }
};

}   // namespace detail

inline std::unique_ptr<Engine> makeEngine(const std::string& lang, Backend backend) {
    if (backend != Backend::Hunspell) {                          // try the OS checker (Native / fallback)
        auto e = std::make_unique<detail::WinEngine>(lang);
        if (e->available()) return std::unique_ptr<Engine>(std::move(e));
        if (backend == Backend::Native) return nullptr;          // native-only: no OS checker -> unavailable
    }
    return makeHunspellEngine(lang);   // fallback for this language, or Hunspell-only -> bundled + en_US
}

}} // namespace wxn::spell

// ---- macOS backend: NSSpellChecker (shims implemented in src/macos_native.mm) -----------------------
#elif defined(__WXMAC__)
extern "C" {
    bool  wxn_spell_available(void);
    bool  wxn_spell_language_available(const char* bcp47);          // OS has a checker for this language?
    bool  wxn_spell_check(const char* utf8word, const char* bcp47lang);   // true = spelled correctly
    char* wxn_spell_suggest(const char* utf8word, const char* bcp47lang); // '\n'-joined UTF-8, malloc'd (caller free()s) or NULL
    bool  wxn_spell_add(const char* utf8word);
}
namespace wxn { namespace spell {
struct MacEngine : Engine {
    std::string lang;                                               // checked EXPLICITLY, not the OS global
    explicit MacEngine(std::string l) : lang(std::move(l)) {}
    bool available() const override { return wxn_spell_available(); }
    bool check(const std::string& w) override { return w.empty() ? true : wxn_spell_check(w.c_str(), lang.c_str()); }
    std::vector<std::string> suggest(const std::string& w) override {
        std::vector<std::string> out;
        char* joined = w.empty() ? nullptr : wxn_spell_suggest(w.c_str(), lang.c_str());
        if (joined) {
            const std::string all(joined); std::free(joined);
            size_t p = 0;
            while (p <= all.size()) {
                const size_t nl = all.find('\n', p);
                const std::string one = all.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                if (!one.empty()) out.push_back(one);
                if (nl == std::string::npos) break;
                p = nl + 1;
            }
        }
        return out;
    }
    bool add(const std::string& w) override { return !w.empty() && wxn_spell_add(w.c_str()); }
};
inline std::unique_ptr<Engine> makeEngine(const std::string& lang, Backend backend) {
    const std::string want = lang.empty() ? std::string("en-US") : lang;
    if (backend != Backend::Hunspell) {                          // try the OS checker (Native / fallback)
        // Only use the OS checker when it actually has the requested language; otherwise (e.g. the user
        // fixed a non-English spelling language) treat it as unavailable.
        if (wxn_spell_available() && wxn_spell_language_available(want.c_str()))
            return std::unique_ptr<Engine>(new MacEngine(want));
        if (backend == Backend::Native) return nullptr;          // native-only: OS lacks it -> unavailable
    }
    return makeHunspellEngine(lang);   // fallback for this language, or Hunspell-only -> bundled + en_US
}
}} // namespace wxn::spell

// ---- other platforms (Linux etc.): the bundled Hunspell above is the engine -------------------------
#else
namespace wxn { namespace spell {
// No native checker on this platform, so the Backend preference is moot - always the bundled Hunspell.
inline std::unique_ptr<Engine> makeEngine(const std::string& lang, Backend) { return makeHunspellEngine(lang); }
}}
#endif
