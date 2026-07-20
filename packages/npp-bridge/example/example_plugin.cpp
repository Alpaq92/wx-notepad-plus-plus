// SPDX-License-Identifier: GPL-3.0-or-later
//
// wxNote - cross-platform recompiled-plugin TEMPLATE + bridge PROBE.
// Copyright 2026 The wxNote Authors.
//
// This is a minimal Notepad++ plugin written the way a real N++ plugin is written: it includes the
// N++ ABI header (PluginInterface.h), exports the six entry points the host resolves by name, and
// talks to the host and the editor using ONLY NPPM_* / SCI_* messages sent through ::SendMessage().
// It uses no Win32 UI, no .rc resources, no HWND subclassing - i.e. the "recompile nearly unchanged"
// tier of docs/ARCHITECTURE.md.
//
// It plays two roles:
//
//  * TEMPLATE (every OS, unchanged from Phase 1): built off-Windows and linked against libnpp_shim,
//    the plugin's ::SendMessage calls resolve to the shim's exported forwarders, demonstrating that
//    the six-symbol contract + the SendMessage seam close with no unresolved symbols.
//
//  * PROBE (the Phase-1 coverage plan's headless-test instrument; see docs/PLAN and
//    tests/bridge_selftest.cpp): every notification the bridge delivers to beNotified is appended -
//    code + idFrom - to a JSON-lines log in the host's plugins-config dir
//    (NPPM_GETPLUGINSCONFIGDIR/wxn_probe.jsonl, truncated per host run, pid-stamped so a test run
//    can tell its own log from a stale one). On NPPN_TBMODIFICATION it exercises the Phase-1
//    surface once: registers a toolbar icon for its first FuncItem command, allocates 2 command ids
//    + 1 marker + 1 indicator through the NPPM_ALLOCATE* family, and queries
//    NPPM_ISDARKMODEENABLED - logging every result. WM_COMMAND relays that reach messageProc (how
//    the bridge dispatches a dynamically-allocated command id) are logged too, which is what lets
//    the selftest prove an allocated id round-trips host -> wx dispatcher -> bridge -> plugin.
//
// On Windows it is built as a real loadable plugin DLL (build/bin/plugins/WxnProbe/WxnProbe.dll -
// the layout the bridge's loader scans); off-Windows it stays the compile+link CI proof it always
// was (and would run identically if staged under <user-data>/plugins/, which the selftest does).

#include "PluginInterface.h"   // NppData, FuncItem, the six extern "C" exports; pulls in npp_plugin_port.h + Scintilla.h + Notepad_plus_msgs.h

#include <cstdio>   // std::fopen/_wfopen + std::vsnprintf - the probe's JSON-lines log
#include <cstdarg>  // va_list (probeLogLine)
#include <cwchar>   // std::wcscpy (menu-label copy); on libstdc++/libc++ it is NOT pulled in by <cstring>

#ifdef _WIN32
// windows.h arrives via npp_plugin_port.h: CreateDirectoryW / CreateDIBSection / GetCurrentProcessId.
#else
  #include <sys/stat.h>   // mkdir - create the plugins Config dir for the probe log
  #include <sys/types.h>
  #include <unistd.h>     // getpid - the log's per-run pid stamp
// On Windows the author's ::SendMessage comes from <windows.h> (pulled in by npp_plugin_port.h).
// Off-Windows there is no <windows.h>, so the prototype of the forwarder that libnpp_shim exports
// must be brought into scope for the unchanged ::SendMessage(hwnd, NPPM_*/SCI_*, w, l) calls below
// to type-check and to resolve to the shim at link time (signature matches shim/npp_shim.cpp).
extern "C" LRESULT SendMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// ...and the message number the bridge relays dynamic allocated command ids under.
  #ifndef WM_COMMAND
    #define WM_COMMAND 0x0111
  #endif
#endif

namespace {

// The three host handles, delivered once by the host via setInfo(). Off-Windows these are opaque
// tokens the bridge hands out; the plugin only ever passes them back into ::SendMessage().
NppData g_npp{};

// The plugin's menu commands, returned verbatim from getFuncsArray(). After load the bridge writes a
// host-granted dynamic command id into each _cmdID (what N++'s plugin manager does with
// ID_PLUGINS_CMD+n) - the probe reads g_funcs[0]._cmdID back for its toolbar registration.
FuncItem g_funcs[3] = {};

// Maintained from the host's file lifecycle notifications, as in the original template.
int g_openFileCount = 0;

// ---- probe state ---------------------------------------------------------------------------------
bool g_probed    = false;   // TBMODIFICATION one-shot guard (the registrations must run exactly once)
bool g_p2done    = false;   // Phase-2 scripted-table one-shot guard (runs once, on the selftest's trigger)
bool g_p3done    = false;   // Phase-3 scripted-table one-shot guard (fired via the 2nd allocated cmd id)
bool g_p4done    = false;   // Phase-4 scripted-table one-shot guard (fired via the 3rd allocated cmd id)
bool g_p5done    = false;   // Phase-5 scripted-table one-shot guard (fired via the 4th allocated cmd id)
int  g_cmdFirst  = -1;      // first of the 4 dynamically-allocated command ids (NPPM_ALLOCATECMDID)
int  g_markFirst = -1;      // the allocated marker number (NPPM_ALLOCATEMARKER)
int  g_indFirst  = -1;      // the allocated indicator number (NPPM_ALLOCATEINDICATOR)

// The probe log: <plugins-config-dir>/wxn_probe.jsonl, one JSON object per line. Wide path on
// Windows (_wfopen - the config dir may be non-ASCII), UTF-8 narrow elsewhere.
#ifdef _WIN32
wchar_t g_logPath[MAX_PATH * 2] = L"";
#else
char    g_logPath[2048] = "";
#endif
// A wide scratch session-file path in the plugins-config dir (both OSes: the NPPM_*SESSION* ABI takes a
// wide path). The Phase-5 table saves/enumerates/loads a session here; the selftest reads + parses it.
wchar_t g_sessPath[MAX_PATH * 2] = L"";
int g_logLines = 0;
constexpr int kLogLineCap = 5000;   // a dev session must not grow the log unboundedly (SCN_* per keystroke)

#ifndef _WIN32
// wchar_t is 4-byte UTF-32 off-Windows -> tiny UTF-32 -> UTF-8 encoder for the config-dir path.
void appendUtf8(char* dst, size_t cap, const wchar_t* w)
{
    size_t n = 0;
    for (; w && *w && n + 5 < cap; ++w) {
        unsigned c = static_cast<unsigned>(*w);
        if      (c < 0x80)    { dst[n++] = (char)c; }
        else if (c < 0x800)   { dst[n++] = (char)(0xC0 | (c >> 6));
                                dst[n++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { dst[n++] = (char)(0xE0 | (c >> 12));
                                dst[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
                                dst[n++] = (char)(0x80 | (c & 0x3F)); }
        else                  { dst[n++] = (char)(0xF0 | (c >> 18));
                                dst[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
                                dst[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
                                dst[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    dst[n] = '\0';
}
#endif

// Append one printf-formatted line to the log (open-append-close per line: durable, and safely
// readable by the in-process selftest between events). Silently inert until initProbeLog() ran.
void probeLogLine(const char* fmt, ...)
{
    if (!g_logPath[0] || g_logLines >= kLogLineCap) return;
    ++g_logLines;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
#ifdef _WIN32
    FILE* f = _wfopen(g_logPath, L"ab");
#else
    FILE* f = std::fopen(g_logPath, "ab");
#endif
    if (!f) return;
    std::fputs(line, f);
    std::fputc('\n', f);
    std::fclose(f);
}

// Resolve the plugins-config dir from the host, create it, truncate the log, and stamp this run's
// pid (the selftest verifies the stamp so a stale log from an earlier run can never fake a result).
void initProbeLog()
{
    wchar_t cfg[MAX_PATH] = L"";
    ::SendMessage(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(cfg));
    if (!cfg[0]) return;
    // Build the scratch session path <cfg>/p5sess.xml (wide, both OSes) with the platform separator.
    {
        int n = 0;
        while (cfg[n] && n < MAX_PATH) { g_sessPath[n] = cfg[n]; ++n; }
#ifdef _WIN32
        const wchar_t* suffix = L"\\p5sess.xml";
#else
        const wchar_t* suffix = L"/p5sess.xml";
#endif
        for (int k = 0; suffix[k]; ++k) g_sessPath[n++] = suffix[k];
        g_sessPath[n] = 0;
    }
    unsigned long long pid = 0;
#ifdef _WIN32
    ::CreateDirectoryW(cfg, nullptr);   // <plugins>\Config; the plugins root exists (we were loaded from it)
    std::swprintf(g_logPath, MAX_PATH * 2, L"%ls\\wxn_probe.jsonl", cfg);
    if (FILE* f = _wfopen(g_logPath, L"wb")) std::fclose(f);   // truncate: one log per host run
    pid = static_cast<unsigned long long>(::GetCurrentProcessId());
#else
    char cfg8[1600];
    appendUtf8(cfg8, sizeof(cfg8), cfg);
    ::mkdir(cfg8, 0755);
    std::snprintf(g_logPath, sizeof(g_logPath), "%s/wxn_probe.jsonl", cfg8);
    if (FILE* f = std::fopen(g_logPath, "wb")) std::fclose(f);
    pid = static_cast<unsigned long long>(getpid());
#endif
    probeLogLine("{\"k\":\"start\",\"pid\":%llu}", pid);
}

#ifdef _WIN32
// A 16x16 32bpp DIB section for the toolbar registration. A DIB section reports bmBitsPixel == 32 with
// a live alpha channel, so it takes the bridge's premultiplied-32bpp conversion path deterministically.
//
// Design: an OPAQUE teal badge (rounded corners) with a darker "[ ]" wrap-selection glyph on it - two
// square brackets enclosing three short text lines, matching the button's "Wrap selection" name - in the
// project's own Open Color teal, the accent the bundled Streamline icon set retints to, so a probe-plugin
// button doesn't look like it wandered in from a different app. It is deliberately opaque, not a thin glyph on
// a transparent field: an earlier transparent-background version registered fine and rasterized fine in
// isolation, but painted BLANK on the live Windows toolbar - wxMSW composites every button into one DIB
// and hands it to the native ToolbarWindow32, and a mostly-empty bitmap did not survive that path. A
// near-opaque badge (only the four corner pixels are transparent, for the rounded look) always paints,
// exactly as the original solid square did - just no longer a featureless block. bridge_selftest.cpp
// asserts the registered button's stored bitmap is non-blank AND green-dominant to lock this in.
HBITMAP makeProbeBitmap()
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = 16;
    bi.bmiHeader.biHeight      = -16;   // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm || !bits) return hbm;
    auto* px = static_cast<unsigned char*>(bits);
    auto setPx = [&](int x, int y, unsigned char b, unsigned char g, unsigned char r, unsigned char a) {
        if (x < 0 || x > 15 || y < 0 || y > 15) return;
        unsigned char* p = px + (y * 16 + x) * 4;   // BGRA, straight alpha
        p[0] = b; p[1] = g; p[2] = r; p[3] = a;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, unsigned char b, unsigned char g, unsigned char r) {
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) setPx(x, y, b, g, r, 0xFF);
    };
    // fill the whole badge opaque, then lay the border and glyph on top. The fill is a VIVID teal (not a
    // pale mint): the toolbar chrome is light grey, so a washed-out fill is nearly invisible on it - this
    // is the same saturation as the built-in bright-green buttons, which read clearly there.
    const unsigned char fB = 0xA9, fG = 0xD9, fR = 0x38;   // Open Color teal-4  #38d9a9 (vivid fill)
    const unsigned char bB = 0x78, bG = 0xA6, bR = 0x0C;   // Open Color teal-7  #0ca678 (border)
    const unsigned char gB = 0x5B, gG = 0x7F, gR = 0x08;   // Open Color teal-9  #087f5b (dark glyph)
    rect(0, 0, 15, 15, fB, fG, fR);                        // opaque mint field
    rect(0, 0, 15, 0, bB, bG, bR);                         // border: top / bottom / left / right
    rect(0, 15, 15, 15, bB, bG, bR);
    rect(0, 0, 0, 15, bB, bG, bR);
    rect(15, 0, 15, 15, bB, bG, bR);
    // knock the four corners transparent for a rounded-badge silhouette (12px of 256 - still ~95% opaque)
    setPx(0, 0, 0, 0, 0, 0x00); setPx(15, 0, 0, 0, 0, 0x00);
    setPx(0, 15, 0, 0, 0, 0x00); setPx(15, 15, 0, 0, 0, 0x00);
    // the "[ ]" wrap-selection glyph, dark teal on the vivid field: two brackets around three lines
    rect(3, 4, 3, 11, gB, gG, gR); rect(3, 4, 4, 4, gB, gG, gR); rect(3, 11, 4, 11, gB, gG, gR);   // left bracket [
    rect(12, 4, 12, 11, gB, gG, gR); rect(11, 4, 12, 4, gB, gG, gR); rect(11, 11, 12, 11, gB, gG, gR); // right bracket ]
    rect(6, 6, 9, 6, gB, gG, gR);                          // text line 1
    rect(6, 8, 9, 8, gB, gG, gR);                          // text line 2
    rect(6, 10, 8, 10, gB, gG, gR);                        // text line 3 (short)
    return hbm;
}
#endif

// The one-shot probe pass, run from NPPN_TBMODIFICATION (the notification real plugins hang their
// toolbar/alloc init off): toolbar icon + the three allocations + the dark-mode query, all logged.
void runProbeRegistrations()
{
    if (g_probed) return;
    g_probed = true;

    // Toolbar button for our first FuncItem's bridge-assigned command id. Off-Windows a recompiled
    // plugin cannot own an HBITMAP/HICON; the bridge documents the call as a no-op TRUE there, and
    // logging the answer on every OS is exactly what the selftest wants to see.
    const int tbCmd = g_funcs[0]._cmdID;
    static toolbarIconsWithDarkMode icons = {};   // static: the host may keep the handle; N++ plugins do the same
#ifdef _WIN32
    icons.hToolbarBmp = makeProbeBitmap();
#endif
    const long long tbOk = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE,
                      static_cast<WPARAM>(tbCmd), reinterpret_cast<LPARAM>(&icons)));
    probeLogLine("{\"k\":\"tb\",\"ok\":%lld,\"cmd\":%d}", tbOk, tbCmd);

    // The allocator family: 4 command ids + 1 marker + 1 indicator, results logged verbatim. The four
    // cmd ids are the triggers the selftest fires to drive the Phase-2/3/4/5 scripted tables (see messageProc).
    int first = -1;
    long long ok = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ALLOCATECMDID, 4, reinterpret_cast<LPARAM>(&first)));
    g_cmdFirst = ok ? first : -1;
    probeLogLine("{\"k\":\"allocCmd\",\"ok\":%lld,\"first\":%d,\"count\":4}", ok, first);

    first = -1;
    ok = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ALLOCATEMARKER, 1, reinterpret_cast<LPARAM>(&first)));
    g_markFirst = ok ? first : -1;
    probeLogLine("{\"k\":\"allocMark\",\"ok\":%lld,\"first\":%d,\"count\":1}", ok, first);

    first = -1;
    ok = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ALLOCATEINDICATOR, 1, reinterpret_cast<LPARAM>(&first)));
    g_indFirst = ok ? first : -1;
    probeLogLine("{\"k\":\"allocInd\",\"ok\":%lld,\"first\":%d,\"count\":1}", ok, first);

    // Dark-mode probe: what a themed plugin branches its palette on.
    const long long dark = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0));
    probeLogLine("{\"k\":\"dark\",\"v\":%lld}", dark);
}

// ---- Phase-2 scripted NPPM call table (the selftest's ground truth) -------------------------------
// Log one Phase-2 probe result: the message number, its return value, and any string it wrote back
// (ASCII-flattened - the selftest's fixture payloads are ASCII; non-ASCII bytes become '?'). The
// selftest greps these {"k":"p2",...} lines by message number and asserts the exact return/string.
void p2log(unsigned msg, long long ret, const wchar_t* s)
{
    char asc[300];
    size_t n = 0;
    if (s) for (; s[n] && n < sizeof(asc) - 1; ++n)
    { const wchar_t c = s[n]; asc[n] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?'; }
    asc[n] = '\0';
    probeLogLine("{\"k\":\"p2\",\"m\":%u,\"r\":%lld,\"s\":\"%s\"}", msg, ret, asc);
}

// Run the Phase-2 message table once, against whatever buffer the selftest has made active (it sets up
// a fixture with a known caret before triggering this). Uses ONLY ::SendMessage - the same NPPM path a
// real plugin uses - so it exercises the shared router on every OS. NPPM_LAUNCHFINDINFILESDLG is
// deliberately NOT called here: it opens a MODAL host dialog that would hang this headless test.
void runPhase2Table()
{
    if (g_p2done) return;
    g_p2done = true;

    auto num = [&](unsigned m, WPARAM w, LPARAM l) {
        p2log(m, static_cast<long long>(::SendMessage(g_npp._nppHandle, m, w, l)), L"");
    };
    auto str = [&](unsigned m, WPARAM w) {
        wchar_t buf[512]; buf[0] = 0;
        const long long r = static_cast<long long>(
            ::SendMessage(g_npp._nppHandle, m, w, reinterpret_cast<LPARAM>(buf)));
        p2log(m, r, buf);
    };

    // --- the 15 implemented getters (caret-dependent ones first, before RELOADBUFFERID resets it) ---
    str(NPPM_GETCURRENTWORD, 512);
    str(NPPM_GETCURRENTLINESTR, 512);
    str(NPPM_GETFILENAMEATCURSOR, 512);
    str(NPPM_GETNPPFULLFILEPATH, 512);
    str(NPPM_GETNPPSETTINGSDIRPATH, 512);
    str(NPPM_GETCURRENTCMDLINE, 512);
    str(NPPM_GETLANGUAGENAME, L_CPP);
    str(NPPM_GETLANGUAGEDESC, L_CPP);
    num(NPPM_GETCURRENTNATIVELANGENCODING, 0, 0);
    num(NPPM_GETWINDOWSVERSION, 0, 0);
    num(NPPM_GETBOOKMARKID, 0, 0);
    num(NPPM_GETAPPDATAPLUGINSALLOWED, 0, 0);
    num(NPPM_SETSMOOTHFONT, 0, TRUE);

    // --- a representative slice of the documented no-op / interim stubs (proves each is answered) ----
    num(NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED, 0, 0);
    num(NPPM_ENCODESCI, 0, 0);
    num(NPPM_DECODESCI, 0, 0);
    num(NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED, 0, 0);
    num(NPPM_DOCLISTDISABLEEXTCOLUMN, 0, 0);
    num(NPPM_DOCLISTDISABLEPATHCOLUMN, 0, 0);
    num(NPPM_DISABLEAUTOUPDATE, 0, 0);
    str(NPPM_GETSETTINGSONCLOUDPATH, 512);
    num(NPPM_GETEXTERNALLEXERAUTOINDENTMODE, 0, 0);
    num(NPPM_SETEXTERNALLEXERAUTOINDENTMODE, 0, 0);
    num(NPPM_MODELESSDIALOG, MODELESSDIALOGADD, 0x5EED);   // Annex W no-op echoes the passed handle back (0x5EED sentinel)
    num(NPPM_DMMVIEWOTHERTAB, 0, 0);                       // Annex W portable no-op -> NULL
    num(NPPM_SETEDITORBORDEREDGE, 0, 0);                   // Annex W portable no-op -> FALSE
    num(NPPM_DMMGETPLUGINHWNDBYNAME, 0, 0);
    num(NPPM_CREATESCINTILLAHANDLE, 0, 0);
    num(NPPM_HIDETABBAR, 0, 0);
    num(NPPM_ISTABBARHIDDEN, 0, 0);
    num(NPPM_DARKMODESUBCLASSANDTHEME, 0, 0);
    num(NPPM_REMOVESHORTCUTBYCMDID, 0, 0);        // Phase-5 documented no-op -> FALSE
    num(NPPM_TRIGGERTABBARCONTEXTMENU, 0, 0);     // Phase-5 documented no-op -> FALSE

    // RELOADBUFFERID last: it re-reads the fixture (resetting the caret), so it must run after every
    // caret-dependent getter above. Resolve the active buffer id, then reload it.
    const long long bid = static_cast<long long>(::SendMessage(g_npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
    num(NPPM_RELOADBUFFERID, static_cast<WPARAM>(bid), 0);
}

// ---- Phase-3 scripted table: the per-view buffer model (the selftest's ground truth) --------------
// One discrete {"k":"p3","t":"<tag>","v":<val>} line per probe, greppable by tag.
void p3log(const char* tag, long long v)
{
    probeLogLine("{\"k\":\"p3\",\"t\":\"%s\",\"v\":%lld}", tag, v);
}
// Run the Phase-3 message table once. The selftest arranges the scenario first (three files open, one
// migrated to the SUB view so the split is real and that sub buffer is active), then fires the SECOND
// allocated command id to trigger this. Uses ONLY ::SendMessage - the shared router on every OS.
void runPhase3Table()
{
    if (g_p3done) return;
    g_p3done = true;
    HWND h = g_npp._nppHandle;

    // per-view open-file counts (NPPM_GETNBOPENFILES filter fidelity)
    p3log("nb.all",     static_cast<long long>(::SendMessage(h, NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES)));
    p3log("nb.primary", static_cast<long long>(::SendMessage(h, NPPM_GETNBOPENFILES, 0, PRIMARY_VIEW)));
    p3log("nb.second",  static_cast<long long>(::SendMessage(h, NPPM_GETNBOPENFILES, 0, SECOND_VIEW)));

    // current doc index per view
    p3log("idx.main", static_cast<long long>(::SendMessage(h, NPPM_GETCURRENTDOCINDEX, 0, MAIN_VIEW)));
    p3log("idx.sub",  static_cast<long long>(::SendMessage(h, NPPM_GETCURRENTDOCINDEX, 0, SUB_VIEW)));

    // MAIN view index 0: id -> packed pos (its view bit must be 0)
    const long long idM = static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERIDFROMPOS, 0, MAIN_VIEW));
    p3log("id.m0",  idM);
    p3log("pos.m0", static_cast<long long>(::SendMessage(h, NPPM_GETPOSFROMBUFFERID, static_cast<WPARAM>(idM), MAIN_VIEW)));

    // SUB view index 0: id -> packed pos (its view bit must be 1)
    const long long idS = static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERIDFROMPOS, 0, SUB_VIEW));
    p3log("id.s0",  idS);
    p3log("pos.s0", static_cast<long long>(::SendMessage(h, NPPM_GETPOSFROMBUFFERID, static_cast<WPARAM>(idS), MAIN_VIEW)));

    // the active buffer: a full pos<->id round-trip through the packed value
    const long long cur = static_cast<long long>(::SendMessage(h, NPPM_GETCURRENTBUFFERID, 0, 0));
    p3log("cur", cur);
    const long long pos = static_cast<long long>(::SendMessage(h, NPPM_GETPOSFROMBUFFERID, static_cast<WPARAM>(cur), MAIN_VIEW));
    p3log("cur.pos", pos);
    const int cv = static_cast<int>((pos >> 30) & 0x3);
    const int ci = static_cast<int>(pos & 0x3FFFFFFF);
    p3log("cur.back", static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERIDFROMPOS, static_cast<WPARAM>(ci), cv)));

    // encoding round-trip on the active buffer (UniMode mapping): read, set uniUTF8 (UTF-8 BOM), re-read
    p3log("enc.before", static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERENCODING, static_cast<WPARAM>(cur), 0)));
    ::SendMessage(h, NPPM_SETBUFFERENCODING, static_cast<WPARAM>(cur), 1 /*uniUTF8*/);
    p3log("enc.after",  static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERENCODING, static_cast<WPARAM>(cur), 0)));

    // EOL round-trip on the active buffer: read, set LF, re-read
    p3log("eol.before", static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERFORMAT, static_cast<WPARAM>(cur), 0)));
    ::SendMessage(h, NPPM_SETBUFFERFORMAT, static_cast<WPARAM>(cur), SC_EOL_LF);
    p3log("eol.after",  static_cast<long long>(::SendMessage(h, NPPM_GETBUFFERFORMAT, static_cast<WPARAM>(cur), 0)));

    // tab colour of the active tab (nothing set -> -1); then force the buffer dirty
    p3log("tabcolor",  static_cast<long long>(::SendMessage(h, NPPM_GETTABCOLORID, static_cast<WPARAM>(-1), -1)));
    p3log("makedirty", static_cast<long long>(::SendMessage(h, NPPM_MAKECURRENTBUFFERDIRTY, 0, 0)));
}

// ---- Phase-4 scripted table: event fidelity opt-in (the selftest's ground truth) ------------------
// One {"k":"p4","t":"<tag>","v":<val>} line per probe, greppable by tag. The selftest fires the THIRD
// allocated command id to run this (after Phase 3), then drives insert/undo/language-switch/close and
// reads the notification stream (beNotified logs NPPN_GLOBALMODIFIED's modificationType as "gm" lines).
void p4log(const char* tag, long long v)
{
    probeLogLine("{\"k\":\"p4\",\"t\":\"%s\",\"v\":%lld}", tag, v);
}
void runPhase4Table()
{
    if (g_p4done) return;
    g_p4done = true;
    HWND h = g_npp._nppHandle;

    // Opt into the SCN modified stream for UNDO + BEFORE-DELETE only (deliberately NOT insert): this is
    // what arms NPPN_GLOBALMODIFIED, and the selftest checks that a plain insert stays silent while an
    // undo fires. Real N++ plugins (ComparePlus) do exactly this to gate their diff engine.
    p4log("add", static_cast<long long>(
        ::SendMessage(h, NPPM_ADDSCNMODIFIEDFLAGS, 0, SC_PERFORMED_UNDO | SC_MOD_BEFOREDELETE)));

    // NPPM_GETSHORTCUTBYCMDID for this plugin's own (unbound) command id: the negative path must return
    // FALSE and leave the caller's ShortcutKey untouched (sentinel below survives).
    ShortcutKey sk = {};
    sk._key = 0xEE;   // sentinel: a FALSE return must not overwrite it
    p4log("sc.unbound", static_cast<long long>(
        ::SendMessage(h, NPPM_GETSHORTCUTBYCMDID, static_cast<WPARAM>(g_funcs[0]._cmdID), reinterpret_cast<LPARAM>(&sk))));
    p4log("sc.unbound.key", static_cast<long long>(sk._key));   // must still be the 0xEE sentinel

    // NPPM_GETSHORTCUTBYCMDID for a PUNCTUATION-bound host command: View > Zoom In, default "Ctrl+=".
    // 44023 is the host's frozen kCmdViewZoomin (src/command_ids.h; every literal there is pinned to the
    // plugin ABI's command ids by that header's own contract) - this GPL example has no dependency on the
    // core, so the id travels as the same frozen number a shipped Win32 plugin binary would hardcode.
    // The bridge's wxKeyToVk must map the wx '=' key code to the real Win32 VK_OEM_PLUS (0xBB), not raw
    // ASCII '=' (0x3D) - a plugin comparing ShortcutKey._key against a VK_* constant needs the real one.
    ShortcutKey skZoom = {};
    p4log("sc.zoomin", static_cast<long long>(
        ::SendMessage(h, NPPM_GETSHORTCUTBYCMDID, static_cast<WPARAM>(44023), reinterpret_cast<LPARAM>(&skZoom))));
    p4log("sc.zoomin.ctrl", static_cast<long long>(skZoom._isCtrl ? 1 : 0));
    p4log("sc.zoomin.key",  static_cast<long long>(skZoom._key));   // must be 0xBB (VK_OEM_PLUS), not 0x3D ('=')
}

// ---- Phase-5 scripted table: sessions + UI-chrome + lexer registry (the selftest's ground truth) --
// One {"k":"p5","t":"<tag>","v":<val>} line per probe, greppable by tag. The selftest fires the FOURTH
// allocated command id to run this (after it has opened a couple of saved fixtures so the current session
// is non-empty), then asserts the toggle/read-back pairs, the CREATELEXER result, and the session round-
// trip. All via ::SendMessage - the shared router on every OS.
void p5log(const char* tag, long long v)
{
    probeLogLine("{\"k\":\"p5\",\"t\":\"%s\",\"v\":%lld}", tag, v);
}
void runPhase5Table()
{
    if (g_p5done) return;
    g_p5done = true;
    HWND h = g_npp._nppHandle;

    // -- UI chrome: toggle each element and read it back through the paired IS* message ----------------
    ::SendMessage(h, NPPM_HIDETOOLBAR, 0, TRUE);
    p5log("tb.hidden", static_cast<long long>(::SendMessage(h, NPPM_ISTOOLBARHIDDEN, 0, 0)));   // TRUE
    ::SendMessage(h, NPPM_HIDETOOLBAR, 0, FALSE);
    p5log("tb.shown",  static_cast<long long>(::SendMessage(h, NPPM_ISTOOLBARHIDDEN, 0, 0)));   // FALSE

    ::SendMessage(h, NPPM_HIDESTATUSBAR, 0, TRUE);
    p5log("sb.hidden", static_cast<long long>(::SendMessage(h, NPPM_ISSTATUSBARHIDDEN, 0, 0))); // TRUE
    ::SendMessage(h, NPPM_HIDESTATUSBAR, 0, FALSE);
    p5log("sb.shown",  static_cast<long long>(::SendMessage(h, NPPM_ISSTATUSBARHIDDEN, 0, 0))); // FALSE

    ::SendMessage(h, NPPM_SHOWDOCLIST, 0, TRUE);
    p5log("dl.shown",  static_cast<long long>(::SendMessage(h, NPPM_ISDOCLISTSHOWN, 0, 0)));    // TRUE
    ::SendMessage(h, NPPM_SHOWDOCLIST, 0, FALSE);
    p5log("dl.hidden", static_cast<long long>(::SendMessage(h, NPPM_ISDOCLISTSHOWN, 0, 0)));    // FALSE

    // Menubar: a portable documented no-op (macOS global menubar), so hiding it leaves it shown.
    ::SendMessage(h, NPPM_HIDEMENU, 0, TRUE);
    p5log("menu.hidden", static_cast<long long>(::SendMessage(h, NPPM_ISMENUHIDDEN, 0, 0)));    // FALSE (no-op)
    ::SendMessage(h, NPPM_HIDEMENU, 0, FALSE);

    // -- line-number margin width mode round-trip --
    ::SendMessage(h, NPPM_SETLINENUMBERWIDTHMODE, 0, LINENUMWIDTH_CONSTANT);
    p5log("lnw.constant", static_cast<long long>(::SendMessage(h, NPPM_GETLINENUMBERWIDTHMODE, 0, 0)));  // 1
    ::SendMessage(h, NPPM_SETLINENUMBERWIDTHMODE, 0, LINENUMWIDTH_DYNAMIC);
    p5log("lnw.dynamic",  static_cast<long long>(::SendMessage(h, NPPM_GETLINENUMBERWIDTHMODE, 0, 0)));  // 0

    // -- misc editor/UI state getters --
    p5log("autoindent", static_cast<long long>(::SendMessage(h, NPPM_ISAUTOINDENTON, 0, 0)));
    p5log("macro",      static_cast<long long>(::SendMessage(h, NPPM_GETCURRENTMACROSTATUS, 0, 0)));
    p5log("iconset",    static_cast<long long>(::SendMessage(h, NPPM_GETTOOLBARICONSETCHOICE, 0, 0)));

    // -- lexer registry: CREATELEXER("cpp") must be non-null and applyable via SCI_SETILEXER; "" -> NULL --
    const long long lx = static_cast<long long>(::SendMessage(h, NPPM_CREATELEXER, 0, reinterpret_cast<LPARAM>(L"cpp")));
    p5log("lexer.cpp", lx != 0 ? 1 : 0);
    if (lx) {   // apply it to the active editor - must not crash (the ILexer routes through nib.sci off-Windows)
        int which = -1;
        ::SendMessage(h, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
        HWND sci = (which == 1) ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
        ::SendMessage(sci, SCI_SETILEXER, 0, static_cast<LPARAM>(lx));
        p5log("lexer.applied", 1);   // reaching here means SCI_SETILEXER did not crash
    }
    p5log("lexer.empty", static_cast<long long>(::SendMessage(h, NPPM_CREATELEXER, 0, reinterpret_cast<LPARAM>(L""))));
    p5log("nbuserlang",  static_cast<long long>(::SendMessage(h, NPPM_GETNBUSERLANG, 0, 0)));

    // -- sessions: save the current (open) files, enumerate, then load back - all by the scratch path --
    p5log("sess.save", ::SendMessage(h, NPPM_SAVECURRENTSESSION, 0, reinterpret_cast<LPARAM>(g_sessPath)) != 0 ? 1 : 0);
    BOOL valid = FALSE;
    const long long nb = static_cast<long long>(
        ::SendMessage(h, NPPM_GETNBSESSIONFILES, reinterpret_cast<WPARAM>(&valid), reinterpret_cast<LPARAM>(g_sessPath)));
    p5log("sess.nb",    nb);
    p5log("sess.valid", valid ? 1 : 0);
    if (nb > 0) {
        wchar_t** arr = new wchar_t*[nb];
        for (int i = 0; i < nb; ++i) arr[i] = new wchar_t[MAX_PATH]();
        const long long got = static_cast<long long>(
            ::SendMessage(h, NPPM_GETSESSIONFILES, reinterpret_cast<WPARAM>(arr), reinterpret_cast<LPARAM>(g_sessPath)));
        p5log("sess.get",   got);
        p5log("sess.f0len", arr[0] ? static_cast<long long>(std::wcslen(arr[0])) : 0);   // first path is non-empty
        for (int i = 0; i < nb; ++i) delete[] arr[i];
        delete[] arr;
    }
    p5log("sess.load", ::SendMessage(h, NPPM_LOADSESSION, 0, reinterpret_cast<LPARAM>(g_sessPath)) != 0 ? 1 : 0);
}

// --- the template's original commands: pure NPPM_*/SCI_* message passing, zero platform code ------

// Resolve the currently-active editor view handle from the host, exactly as a real plugin does.
HWND currentScintilla()
{
    int which = -1;
    ::SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA,
                  0, reinterpret_cast<LPARAM>(&which));
    return (which == 1) ? g_npp._scintillaSecondHandle
                        : g_npp._scintillaMainHandle;
}

// Command 1: wrap the current selection in a pair of markers, using only SCI_*.
void wrapSelection()
{
    HWND sci = currentScintilla();
    const LRESULT selLen = ::SendMessage(sci, SCI_GETSELTEXT, 0, 0);
    if (selLen <= 0)
        return;
    const char marker[] = "<<wrapped>>";
    ::SendMessage(sci, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(marker));
}

// Command 2: ask the host for its version - a pure NPPM_* round-trip.
void showHostVersion()
{
    const LRESULT ver = ::SendMessage(g_npp._nppHandle, NPPM_GETNPPVERSION, 0, 0);
    probeLogLine("{\"k\":\"ver\",\"v\":%lld}", static_cast<long long>(ver));
}

// Command 3: enumerate every open file's path - the exact pattern a file-list / session plugin uses.
void listOpenFiles()
{
    const int nb = static_cast<int>(::SendMessage(g_npp._nppHandle, NPPM_GETNBOPENFILES, 0, 0));
    if (nb <= 0)
        return;
    wchar_t** names = new wchar_t*[nb];
    for (int i = 0; i < nb; ++i)
        names[i] = new wchar_t[MAX_PATH]();
    ::SendMessage(g_npp._nppHandle, NPPM_GETOPENFILENAMES_DEPRECATED,
                  reinterpret_cast<WPARAM>(names), static_cast<LPARAM>(nb));
    probeLogLine("{\"k\":\"files\",\"n\":%d}", nb);
    for (int i = 0; i < nb; ++i)
        delete[] names[i];
    delete[] names;
}

} // namespace

// ===========================================================================
//  The six exported entry points the host resolves by name (the contract).
// ===========================================================================
extern "C" {

// wxNote buffers are Unicode; kept for ABI compatibility.
NPP_EXPORT BOOL isUnicode()
{
    return TRUE;
}

// The host hands the plugin its three window/view handles here, once, at load. The bridge's NPPM_*
// router is already live at this point (on every OS), so the probe log can be set up now.
NPP_EXPORT void setInfo(NppData data)
{
    g_npp = data;
    initProbeLog();
}

// The plugin's display name in the Plugins/Extensions menu.
NPP_EXPORT const wchar_t* getName()
{
    return L"wxNote Probe";
}

// Return the command table; the host reads *count entries out of it (and then writes a host-granted
// dynamic command id into each _cmdID - see runProbeRegistrations).
NPP_EXPORT FuncItem* getFuncsArray(int* count)
{
    std::wcscpy(g_funcs[0]._itemName, L"Wrap selection");
    g_funcs[0]._pFunc = wrapSelection;
    std::wcscpy(g_funcs[1]._itemName, L"Show host version");
    g_funcs[1]._pFunc = showHostVersion;
    std::wcscpy(g_funcs[2]._itemName, L"List open files");
    g_funcs[2]._pFunc = listOpenFiles;

    if (count)
        *count = 3;
    return g_funcs;
}

// Editor/host notifications. EVERY notification is logged (code + idFrom) - that stream is the
// selftest's ground truth for ordering/id assertions - and NPPN_TBMODIFICATION additionally runs the
// one-shot probe registrations, exactly where a real plugin performs its toolbar/alloc init.
NPP_EXPORT void beNotified(SCNotification* scn)
{
    if (!scn)
        return;
    probeLogLine("{\"k\":\"n\",\"c\":%u,\"i\":%llu}",
                 static_cast<unsigned>(scn->nmhdr.code),
                 static_cast<unsigned long long>(scn->nmhdr.idFrom));
    switch (scn->nmhdr.code) {
        case NPPN_READY:          g_openFileCount = 0; break;   // host is up; safe to query the environment
        case NPPN_TBMODIFICATION: runProbeRegistrations(); break;
        case NPPN_FILEOPENED:     ++g_openFileCount; break;
        case NPPN_FILECLOSED:     if (g_openFileCount > 0) --g_openFileCount; break;
        // Phase 4: NPPN_GLOBALMODIFIED carries the real Scintilla modificationType - log it so the
        // selftest can assert the opted-in flags (SC_PERFORMED_UNDO / SC_MOD_BEFOREDELETE) came through.
        case NPPN_GLOBALMODIFIED: probeLogLine("{\"k\":\"gm\",\"mt\":%u}", static_cast<unsigned>(scn->modificationType)); break;
        default:                  break;
    }
}

// Direct host->plugin messages. The bridge relays a fired dynamically-allocated command id here as
// WM_COMMAND (wParam = the id) - logging it is what closes the selftest's dispatch round-trip.
NPP_EXPORT LRESULT messageProc(UINT message, WPARAM wParam, LPARAM /*lParam*/)
{
    if (message == WM_COMMAND) {
        probeLogLine("{\"k\":\"cmd\",\"id\":%d}", static_cast<int>(wParam));
        // The selftest fires the first allocated command id to trigger the Phase-2 scripted table once,
        // AFTER it has opened a fixture and placed the caret. Later (d)-test fires of the same id are
        // no-ops here (g_p2done guards it), so the modal Find-in-Files launch and the table run once only.
        if (static_cast<int>(wParam) == g_cmdFirst)
            runPhase2Table();
        else if (static_cast<int>(wParam) == g_cmdFirst + 1)
            runPhase3Table();   // the selftest fires the 2nd allocated id once its Phase-3 scenario is set up
        else if (static_cast<int>(wParam) == g_cmdFirst + 2)
            runPhase4Table();   // the 3rd allocated id: arm the event-fidelity opt-in before the selftest drives edits
        else if (static_cast<int>(wParam) == g_cmdFirst + 3)
            runPhase5Table();   // the 4th allocated id: sessions + UI-chrome + lexer registry
    }
    return TRUE;
}

} // extern "C"
