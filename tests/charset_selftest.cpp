// SPDX-License-Identifier: Apache-2.0
//
// charset_selftest - round-trip test for the portable code-page path (the wxCSConv name-based conversion
// that interpretCharset/encodeForPage use off Windows). Links wx::base. It validates that THIS platform's
// converter (iconv on Linux/macOS, the win32 mapping on Windows) round-trips a representative set of the
// ~50 code pages, and that an unavailable charset is DETECTED (wxCSConv::IsOk) rather than silently
// decoded as garbage. Vectors were generated with Python's codecs. Run on Linux/macOS/Windows in CI so
// all three backends are exercised.
//
//   cmake --build build --target charset_selftest && build/bin/charset_selftest
//
#include <wx/init.h>
#include <wx/string.h>
#include <wx/strconv.h>
#include <wx/buffer.h>
#include <cstdio>
#include <string>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what); if (ok) ++g_pass; else ++g_fail; }

static std::string fromHex(const char* h)
{
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    std::string s; for (; h[0] && h[1]; h += 2) s.push_back((char)((v(h[0]) << 4) | v(h[1]))); return s;
}

struct Case { const char* name; const char* bytesHex; const char* utf8Hex; };

int main()
{
    wxInitializer init;
    if (!init.IsOk()) { std::printf("wx init failed\n"); return 2; }
    std::printf("charset_selftest\n");

    const Case cases[] = {
        { "CP1251",     "cff0e8e2e5f2", "d09fd180d0b8d0b2d0b5d182" },   // Привет (Cyrillic, single-byte)
        { "CP932",      "93fa967b",     "e697a5e69cac" },               // 日本 (Shift-JIS, multi-byte)
        { "ISO-8859-2", "b1",           "c485" },                        // ą (Central European)
        { "KOI8-R",     "d0d2c9d7c5d4", "d0bfd180d0b8d0b2d0b5d182" },   // привет (Cyrillic)
        { "CP437",      "c9cdbb",       "e29594e29590e29597" },          // ╔═╗ (box-drawing, enum-family canary)
        { "CP437",      "db",           "e29688" },                      // █
    };

    for (const Case& c : cases) {
        const std::string bytes    = fromHex(c.bytesHex);
        const std::string wantUtf8 = fromHex(c.utf8Hex);
        wxCSConv conv(wxString::FromAscii(c.name));
        char lbl[96];

        std::snprintf(lbl, sizeof lbl, "%s: converter resolved (IsOk)", c.name);
        check(conv.IsOk(), lbl);

        wxString s(bytes.data(), conv, bytes.size());        // decode: bytes -> Unicode
        const std::string gotUtf8(s.utf8_str());
        std::snprintf(lbl, sizeof lbl, "%s: decode -> expected Unicode", c.name);
        check(!s.empty() && gotUtf8 == wantUtf8, lbl);

        wxScopedCharBuffer b = s.mb_str(conv);               // encode: Unicode -> bytes
        std::snprintf(lbl, sizeof lbl, "%s: re-encode -> original bytes", c.name);
        check(std::string(b.data(), b.length()) == bytes, lbl);
    }

    // Negative: an unavailable charset must be DETECTABLE, so the portable path can report
    // "not available on this platform" instead of writing mojibake into the buffer.
    //
    // This suite originally asserted the weaker idiom "unavailable -> empty output". CI disproved it on
    // Linux/macOS, and the wx sources say why: wxCSConv::DoCreate() tries iconv, the font mapper, the
    // Win32/CoreFoundation converters and wxEncodingConverter, and when they ALL fail it returns null -
    // whereupon wxCSConv::ToWChar() falls back to a **Latin-1 direct decode** (strconv.cpp, "latin-1
    // (direct)"), which is non-empty garbage. So an empty-output test can never fire, and production
    // relying on it would have silently reinterpreted the file. IsOk() is the real availability query
    // (m_convReal is built eagerly in the ctor, so it is valid immediately after construction), and it
    // is what the interpret-as-charset path in main.cpp now checks - hence the IsOk assertions above.
    {
        wxCSConv bad(wxString::FromAscii("NO_SUCH_CHARSET_ZZZ"));
        check(!bad.IsOk(), "unavailable charset -> IsOk() is false (the guard production checks)");

        const std::string bytes = fromHex("cff0e8");
        wxString s(bytes.data(), bad, bytes.size());
        std::printf("  ..    (for the record, the old empty-output idiom would %s here: %d chars)\n",
                    s.empty() ? "have fired" : "NOT have fired", static_cast<int>(s.length()));
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
