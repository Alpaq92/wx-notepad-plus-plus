// SPDX-License-Identifier: Apache-2.0
//
// charset_selftest - round-trip test for the portable code-page path (the wxCSConv name-based conversion
// that interpretCharset/encodeForPage use off Windows). Links wx::base. It validates that THIS platform's
// converter (iconv on Linux/macOS, the win32 mapping on Windows) round-trips a representative set of the
// ~50 code pages, and that an unavailable charset fails the empty-on-nonempty guard rather than emitting
// garbage. Vectors were generated with Python's codecs. Run on Linux/macOS/Windows in CI so all three
// backends are exercised.
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

        wxString s(bytes.data(), conv, bytes.size());        // decode: bytes -> Unicode
        const std::string gotUtf8(s.utf8_str());
        std::snprintf(lbl, sizeof lbl, "%s: decode -> expected Unicode", c.name);
        check(!s.empty() && gotUtf8 == wantUtf8, lbl);

        wxScopedCharBuffer b = s.mb_str(conv);               // encode: Unicode -> bytes
        std::snprintf(lbl, sizeof lbl, "%s: re-encode -> original bytes", c.name);
        check(std::string(b.data(), b.length()) == bytes, lbl);
    }

    // Negative: an unknown charset must fail the empty-on-nonempty guard (the exact idiom production uses on
    // POSIX), so an unavailable code page reports "unavailable" rather than writing garbage into the buffer.
    // Only meaningful on non-Windows: that guard IS the production path there (iconv returns empty on an
    // unknown charset). On Windows production uses direct Win32, and wxCSConv falls back to the locale
    // converter for an unknown name rather than returning empty, so this assertion doesn't apply.
#ifndef __WXMSW__
    {
        wxCSConv bad(wxString::FromAscii("NO_SUCH_CHARSET_ZZZ"));
        const std::string bytes = fromHex("cff0e8");
        wxString s(bytes.data(), bad, bytes.size());
        check(s.empty(), "unknown charset -> empty output (guard fires, no garbage)");
    }
#else
    std::printf("  skip  unknown-charset guard test (Windows uses direct Win32, not this path)\n");
#endif

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
