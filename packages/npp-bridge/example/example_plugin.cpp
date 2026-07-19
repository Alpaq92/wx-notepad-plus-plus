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
int  g_cmdFirst  = -1;      // first of the 2 dynamically-allocated command ids (NPPM_ALLOCATECMDID)
int  g_markFirst = -1;      // the allocated marker number (NPPM_ALLOCATEMARKER)
int  g_indFirst  = -1;      // the allocated indicator number (NPPM_ALLOCATEINDICATOR)

// The probe log: <plugins-config-dir>/wxn_probe.jsonl, one JSON object per line. Wide path on
// Windows (_wfopen - the config dir may be non-ASCII), UTF-8 narrow elsewhere.
#ifdef _WIN32
wchar_t g_logPath[MAX_PATH * 2] = L"";
#else
char    g_logPath[2048] = "";
#endif
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
// A 16x16 32bpp DIB section for the toolbar registration: an opaque two-tone square (straight alpha
// 0xFF everywhere). A DIB section reports bmBitsPixel == 32 with a live alpha channel, so it takes
// the bridge's premultiplied-32bpp conversion path deterministically.
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
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            unsigned char* p = px + (y * 16 + x) * 4;    // BGRA
            const bool border = (x == 0 || y == 0 || x == 15 || y == 15);
            p[0] = border ? 0x3E : 0xBB;   // B
            p[1] = border ? 0x8A : 0xF2;   // G
            p[2] = border ? 0x2B : 0xB2;   // R
            p[3] = 0xFF;                   // straight alpha, fully opaque
        }
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

    // The allocator family: 2 command ids + 1 marker + 1 indicator, results logged verbatim.
    int first = -1;
    long long ok = static_cast<long long>(
        ::SendMessage(g_npp._nppHandle, NPPM_ALLOCATECMDID, 2, reinterpret_cast<LPARAM>(&first)));
    g_cmdFirst = ok ? first : -1;
    probeLogLine("{\"k\":\"allocCmd\",\"ok\":%lld,\"first\":%d,\"count\":2}", ok, first);

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
        default:                  break;
    }
}

// Direct host->plugin messages. The bridge relays a fired dynamically-allocated command id here as
// WM_COMMAND (wParam = the id) - logging it is what closes the selftest's dispatch round-trip.
NPP_EXPORT LRESULT messageProc(UINT message, WPARAM wParam, LPARAM /*lParam*/)
{
    if (message == WM_COMMAND)
        probeLogLine("{\"k\":\"cmd\",\"id\":%d}", static_cast<int>(wParam));
    return TRUE;
}

} // extern "C"
