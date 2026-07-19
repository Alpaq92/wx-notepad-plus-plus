// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-bridge - the optional Notepad++ binary-plugin compatibility bridge for wxNote.
//
// It is a Nib plugin that rebuilds the Notepad++ NppData environment so real Notepad++ plugins can be
// hosted. This module is GPL because it reproduces Notepad++'s plugin ABI; the wxNote core does NOT
// depend on it - it is loaded, like any plugin, only if present. Keeping it separate + GPL is what lets
// the core stay Apache-2.0 (see LICENSING.md).
//
// On Windows it reaches the host's native handles through the nib.win32 capability and loads real N++
// plugin *DLLs* (LoadLibrary), handing them the rebuilt NppData and surfacing each FuncItem as a Nib
// command. On Linux/macOS (Phase 1 of docs/ARCHITECTURE.md) it dlopen()s *recompiled* N++
// plugins that were built against libnpp_shim, binds a host-dispatch vtable into each so the plugin's own
// SendMessage(NPPM_*/SCI_*) calls route back through bridge_dispatch, and serves SCI_* via the portable
// nib.sci capability (offered on every OS). The Win32-specific machinery (frame subclass, HMENU/status-bar
// / docking native-window plumbing) is guarded behind #ifdef _WIN32 and left inert off-Windows.
#include "nib.h"
#include "PluginInterface.h"   // NppData, FuncItem, PFUNC* - the Notepad++ ABI this bridge targets
#include "menuCmdID.h"         // IDM_* built-in command ids (== the host's frozen kCmd* values) - for NPPM actions
#include "Docking.h"           // tTbData / DWS_DF_* / CONT_* - the N++ docking-registration ABI
#include "abi_layout_asserts.h"  // compile-time guard: catches accidental ABI struct-layout drift
#include "shim/npp_shim.h"     // NppHostBridge / npp_shim_bind - the shim<->bridge contract (CONTRACT 2)

#ifdef _WIN32
  #include <windows.h>
  #include <commctrl.h>        // SetWindowSubclass / DefSubclassProc - the bridge owns the NPPM_* router
#else
  #include <dlfcn.h>           // dlopen / dlsym / dlclose / dlerror
  #include <filesystem>        // POSIX plugin-dir scan
  #include <cstdint>           // uint32_t (UTF conversions)
  #ifdef __APPLE__
    #include <mach-o/dyld.h>   // _NSGetExecutablePath (exeDir)
  #else
    #include <unistd.h>        // readlink (exeDir)
  #endif
#endif
#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>              // std::wcscpy / std::wcscmp (not always transitively included on GCC/clang)

// ---- module transport: the ONLY loader primitives that differ per OS ----------------------------
#ifdef _WIN32
  using DllHandle = HMODULE;
  static DllHandle dllOpen (const std::wstring& p){ return ::LoadLibraryW(p.c_str()); }
  static void*     dllSym  (DllHandle h, const char* n){ return reinterpret_cast<void*>(::GetProcAddress(h, n)); }
  static void      dllClose(DllHandle h){ ::FreeLibrary(h); }
  static constexpr const wchar_t* kPluginExt = L".dll";
#else
  using DllHandle = void*;
  static DllHandle dllOpen (const std::string& p){ return ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
  static void*     dllSym  (DllHandle h, const char* n){ return ::dlsym(h, n); }
  static void      dllClose(DllHandle h){ ::dlclose(h); }
  #ifdef __APPLE__
    static constexpr const char* kPluginExt = ".dylib";
  #else
    static constexpr const char* kPluginExt = ".so";
  #endif
  // The port header does not bring in the Win32 word-packing macro off-Windows.
  #ifndef MAKELONG
    #define MAKELONG(lo, hi) ((LONG)(((WORD)((DWORD)(lo) & 0xFFFF)) | (((DWORD)((WORD)((DWORD)(hi) & 0xFFFF))) << 16)))
  #endif
  // ...nor WM_COMMAND (the message number N++ relays dynamic plugin command ids under).
  #ifndef WM_COMMAND
    #define WM_COMMAND 0x0111
  #endif
#endif

static NppData g_npp = {};      // the Notepad++ environment, rebuilt from nib.win32 (Win) / opaque tokens (POSIX)
static NibHost*               g_host  = nullptr;  // stashed at activate so the NPPM router can reach Nib interfaces
static const NibDocumentsApi* g_docs  = nullptr;  // nib.documents - serves the current-file-path NPPM family
static const NibWin32Api*     g_win32 = nullptr;  // nib.win32 - serves docking (NPPM_DMM*); NULL off-Windows
static const NibSciApi*       g_sci   = nullptr;  // nib.sci - the portable SCI_* passthrough (every OS)
static const NibPathsApi*     g_paths = nullptr;  // nib.paths - user-data dir; the off-Windows plugins base
static const NibCommandsApi*  g_cmds  = nullptr;  // nib.commands - invoke a built-in command by id (NPPM_MENUCOMMAND)
static const NibUiApi*        g_ui      = nullptr;  // nib.ui - menu checkmarks + dark probe/palette (SETMENUITEMCHECK, ISDARKMODEENABLED, GETDARKMODECOLORS)
static const NibToolbarApi*   g_toolbar = nullptr;  // nib.toolbar - plugin toolbar buttons (ADDTOOLBARICON*); NULL on old hosts
static const NibAllocApi*     g_alloc   = nullptr;  // nib.alloc - cmd-id/marker/indicator grants (ALLOCATE* + FuncItem cmdIDs)
static bool                   g_eventsV2 = false;   // host speaks nib.events v2 (id-carrying SAVING/SAVED/BEFORE_OPEN)

struct NppPlugin {              // a loaded N++ plugin (kept alive for its FuncItem pointers + Stage-3 notifications)
    DllHandle    lib;
    PBENOTIFIED  beNotified;
    PMESSAGEPROC messageProc;
    FuncItem*    funcs;
    int          count;
    std::wstring module;        // module filename ("NppExec.dll" / "NppExec.so") - the NPPM_MSGTOPLUGIN address
};
static std::vector<NppPlugin> g_plugins;

// ---- wide<->UTF-8 (Win32 primitives on Windows; hand-rolled UTF-32 codecs off-Windows) ----------
#ifdef _WIN32
static std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring wFromUtf8(const char* s)
{
    if (!s || !*s) return {};
    int w = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);   // w counts the terminating NUL too
    if (w <= 1) return {};
    std::wstring r(static_cast<size_t>(w), L'\0');                  // allocate room for the content + NUL...
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, &r[0], w);
    r.resize(static_cast<size_t>(w - 1));                           // ...then drop the trailing NUL from size()
    return r;
}
#else
// wchar_t is 4-byte UTF-32 off-Windows -> hand-rolled UTF-32 <-> UTF-8 (no <codecvt> dep), same signatures.
static std::string toUtf8(const wchar_t* w)
{
    std::string s;
    if (!w) return s;
    for (; *w; ++w) {
        uint32_t c = static_cast<uint32_t>(*w);
        if      (c < 0x80)    { s.push_back((char)c); }
        else if (c < 0x800)   { s.push_back((char)(0xC0 | (c >> 6)));
                                s.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { s.push_back((char)(0xE0 | (c >> 12)));
                                s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                                s.push_back((char)(0x80 | (c & 0x3F))); }
        else                  { s.push_back((char)(0xF0 | (c >> 18)));
                                s.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
                                s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                                s.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return s;
}
static std::wstring wFromUtf8(const char* s)
{
    std::wstring r;
    if (!s) return r;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        uint32_t c; int extra;
        if      (*p < 0x80) { c = *p;        extra = 0; }
        else if (*p < 0xE0) { c = *p & 0x1F; extra = 1; }
        else if (*p < 0xF0) { c = *p & 0x0F; extra = 2; }
        else                { c = *p & 0x07; extra = 3; }
        ++p;
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i) { c = (c << 6) | (*p & 0x3F); ++p; }
        r.push_back(static_cast<wchar_t>(c));
    }
    return r;
}
#endif

// The directory the app runs from (the "N++ install dir" NPPM family). Wide on both OSes.
#ifdef _WIN32
static std::wstring exeDir()
{
    wchar_t buf[MAX_PATH]; DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n); size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}
#else
static std::wstring exeDir()
{
    std::string p;
  #ifdef __APPLE__
    char buf[4096]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0) p = buf;
  #else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; p = buf; }
  #endif
    const size_t s = p.find_last_of('/');
    if (s != std::string::npos) p.resize(s); else p = ".";
    return wFromUtf8(p.c_str());   // OS gives UTF-8; match the Windows exeDir return type
}
#endif

// The base a recompiled plugin's NPPM_GETPLUGINHOMEPATH/GETPLUGINSCONFIGDIR should report - the SAME
// place the loader reads plugins from, using the platform's own separator: <exe>/plugins on Windows,
// <user-data>/plugins off-Windows (the exe dir isn't user-writable when installed). Backslash paths off
// Windows would be literal filename characters, so keep them platform-correct.
static std::wstring pluginsRootW()
{
#ifdef _WIN32
    return exeDir() + L"\\plugins";
#else
    if (g_paths && g_paths->user_data_dir) {
        char buf[2048];
        const int n = g_paths->user_data_dir(g_host, buf, static_cast<int>(sizeof(buf)));
        if (n > 0) return wFromUtf8(buf) + L"/plugins";
    }
    return exeDir() + L"/plugins";
#endif
}
static std::wstring pluginsConfigDirW()
{
#ifdef _WIN32
    return pluginsRootW() + L"\\Config";
#else
    return pluginsRootW() + L"/Config";
#endif
}

// The Nib command a FuncItem maps to: invoke the plugin's command function.
static void npp_cmd_thunk(NibHost*, NibQueryFn, void* user)
{
    FuncItem* fi = static_cast<FuncItem*>(user);
    if (fi && fi->_pFunc) fi->_pFunc();
}

// Fire a bare NPPN_* application notification (the Notepad++ frame is its source, per the ABI) at every
// loaded plugin's beNotified. Used for lifecycle events that have no Scintilla-editor origin
// (NPPN_READY / NPPN_SHUTDOWN / NPPN_FILESAVED).
static void notifyNpp(unsigned code, uptr_t idFrom = 0)
{
    SCNotification scn = {};
    scn.nmhdr.hwndFrom = g_npp._nppHandle;   // NPPN_ notifications come from the frame, not an editor
    scn.nmhdr.code     = code;
    scn.nmhdr.idFrom   = idFrom;
    for (const auto& p : g_plugins) if (p.beNotified) p.beNotified(&scn);
}

// Stage 3: forward host editor events to every loaded N++ plugin's beNotified, as Scintilla notifications.
static void on_nib_event(NibHost*, const NibEvent* ev, void*)
{
    SCNotification scn = {};
    scn.nmhdr.hwndFrom = g_npp._scintillaMainHandle;
    switch (ev->kind) {
        case NIB_EV_TEXT_CHANGED:
            scn.nmhdr.code       = SCN_MODIFIED;
            scn.position         = ev->as.text.pos;
            scn.length           = ev->as.text.added ? ev->as.text.added : ev->as.text.removed;
            scn.modificationType = ev->as.text.added ? SC_MOD_INSERTTEXT : SC_MOD_DELETETEXT;
            break;
        case NIB_EV_SELECTION_CHANGED:
            scn.nmhdr.code = SCN_UPDATEUI;
            scn.updated    = SC_UPDATE_SELECTION;
            break;
        case NIB_EV_DOCUMENT_SAVED:
            // v1 (savepoint-derived): the editor-level save-point notification. The application-level
            // NPPN_FILESAVED comes from the v2 pipeline below (one per REAL disk write, id-carrying);
            // only when the host predates events v2 is it still derived here - active-doc id,
            // undo-to-savepoint false positives and all (the old, lower-fidelity behaviour).
            scn.nmhdr.code = SCN_SAVEPOINTREACHED;
            if (!g_eventsV2)
                notifyNpp(NPPN_FILESAVED,
                          (g_docs && g_docs->version >= 2 && g_docs->active_id)
                              ? static_cast<uptr_t>(g_docs->active_id(g_host)) : 0);
            break;
        case NIB_EV_DOCUMENT_ACTIVATED:
            scn.nmhdr.hwndFrom = g_npp._nppHandle;   // an NPPN_ notification comes from the frame, not the editor
            scn.nmhdr.code     = NPPN_BUFFERACTIVATED;
            scn.nmhdr.idFrom   = static_cast<uptr_t>(ev->as.document.id);   // the now-active buffer id
            break;
        case NIB_EV_DOCUMENT_OPENED:
            scn.nmhdr.hwndFrom = g_npp._nppHandle;
            scn.nmhdr.code     = NPPN_FILEOPENED;
            scn.nmhdr.idFrom   = static_cast<uptr_t>(ev->as.document.id);   // the opened file's buffer id
            break;
        case NIB_EV_DOCUMENT_CLOSED:
            // N++ pairs these on every close path: FILEBEFORECLOSE first, then FILECLOSED, both with
            // the SAME still-valid buffer id. The host guarantees id/path stay resolvable for this
            // whole callback (NIB_EV_DOCUMENT_CLOSED's ordering note in nib.h), so both notifications
            // can resolve the path via NPPM_GETFULLPATHFROMBUFFERID.
            notifyNpp(NPPN_FILEBEFORECLOSE, static_cast<uptr_t>(ev->as.document.id));
            scn.nmhdr.hwndFrom = g_npp._nppHandle;
            scn.nmhdr.code     = NPPN_FILECLOSED;
            scn.nmhdr.idFrom   = static_cast<uptr_t>(ev->as.document.id);   // the closing file's buffer id (still valid)
            break;
        default: return;
    }
    for (const auto& p : g_plugins) if (p.beNotified) p.beNotified(&scn);
}

// The nib.events v2 pipeline: id-carrying document lifecycle (subscribed only when the host offers the
// v2 table). SAVING fires before each real write attempt - during Save All once per buffer, each with
// that buffer's own id - and maps to NPPN_FILEBEFORESAVE; SAVED fires exactly once per real disk write
// (never on undo-to-savepoint) and maps to the id-carrying NPPN_FILESAVED; BEFORE_OPEN fires after the
// page exists but before content loads and maps to NPPN_FILEBEFOREOPEN. Kept separate from
// on_nib_event because the same NIB_EV_DOCUMENT_SAVED kind means different things per table version.
static void on_nib_event_v2(NibHost*, const NibEvent* ev, void*)
{
    switch (ev->kind) {
        case NIB_EV_DOCUMENT_SAVING:      notifyNpp(NPPN_FILEBEFORESAVE, static_cast<uptr_t>(ev->as.document.id)); break;
        case NIB_EV_DOCUMENT_SAVED:       notifyNpp(NPPN_FILESAVED,      static_cast<uptr_t>(ev->as.document.id)); break;
        case NIB_EV_DOCUMENT_BEFORE_OPEN: notifyNpp(NPPN_FILEBEFOREOPEN, static_cast<uptr_t>(ev->as.document.id)); break;
        default: break;
    }
}

// Diagnostic: report the rebuilt NppData + how many N++ plugins loaded.
static void cmd_info(NibHost* host, NibQueryFn query, void*)
{
    const NibEditorApi* ed = static_cast<const NibEditorApi*>(query(host, NIB_IFACE_EDITOR, 1));
    if (!ed) return;
    char buf[200];
    std::snprintf(buf, sizeof(buf),
        "// npp-bridge: %zu N++ plugin(s) loaded; NppData npp=%p sciMain=%p\n",
        g_plugins.size(), static_cast<void*>(g_npp._nppHandle), static_cast<void*>(g_npp._scintillaMainHandle));
    ed->replace_selection(host, buf);
}

// ---- SHARED: FuncItem registration (factored out of the Windows loader; reused verbatim by POSIX) -
// Compiled + run on Windows CI (the existing loader calls it), and by loadNppPluginsPosix off-Windows.
static void registerFuncItems(NibHost* host, const NibCommandsApi* cmds,
                              const std::string& pname, FuncItem* funcs, int count)
{
    for (int i = 0; funcs && i < count; ++i) {
        // separator: empty name or the literal L"-SEPARATOR-" (std::wcscmp is portable + zero-alloc on both OSes)
        if (!funcs[i]._itemName[0] || std::wcscmp(funcs[i]._itemName, L"-SEPARATOR-") == 0) continue;
        const std::string id    = "npp." + pname + "." + std::to_string(i);
        const std::string title = pname + ": " + toUtf8(funcs[i]._itemName);
        cmds->register_command(host, id.c_str(), title.c_str(), npp_cmd_thunk, &funcs[i]);
    }
}

// ---- SHARED: finish loading one already-dlopen'd module (both loaders differ only in how they find it).
// Resolves the three required N++ entry points, delivers the NppData, registers the plugin's FuncItems,
// and records it. bindTarget wires the plugin's outbound SendMessage before setInfo (POSIX; nullptr on
// Windows, where the plugin uses the OS ::SendMessage). moduleFile is the plugin's on-disk filename -
// the address NPPM_MSGTOPLUGIN routes by. Returns false + closes lib if it's not a plugin.
static bool loadOneNppModule(NibHost* host, const NibCommandsApi* cmds, DllHandle lib,
                             const char* diagName, const NppHostBridge* bindTarget,
                             const std::wstring& moduleFile)
{
    auto setInfo  = reinterpret_cast<PFUNCSETINFO>      (dllSym(lib, "setInfo"));
    auto getFuncs = reinterpret_cast<PFUNCGETFUNCSARRAY>(dllSym(lib, "getFuncsArray"));
    auto getName  = reinterpret_cast<PFUNCGETNAME>      (dllSym(lib, "getName"));
    if (!setInfo || !getFuncs || !getName) { dllClose(lib); return false; }   // not a real N++ plugin
#ifndef _WIN32
    // Absent npp_shim_bind => the plugin was built without libnpp_shim; say so instead of silently
    // letting its SendMessage calls no-op.
    if (bindTarget) {
        if (auto bind = reinterpret_cast<void(*)(const NppHostBridge*)>(dllSym(lib, "npp_shim_bind")))
            bind(bindTarget);
        else
            std::fprintf(stderr, "[npp-bridge] %s: built without libnpp_shim; its SendMessage calls will be inert.\n",
                         diagName ? diagName : "plugin");
    }
#else
    (void)diagName; (void)bindTarget;
#endif
    setInfo(g_npp);                          // hand over the NppData (real HWNDs on Win, opaque tokens off-Win)
    int count = 0;
    FuncItem* funcs = getFuncs(&count);      // the plugin's menu commands (array owned by the module)
    // Assign each FuncItem a real, dispatchable command id from the host's dynamic pool - what N++'s
    // plugin manager does with ID_PLUGINS_CMD+n. These are the ids the plugin passes back through
    // NPPM_SETMENUITEMCHECK / NPPM_ADDTOOLBARICON* / NPPM_GETSHORTCUTBYCMDID, and when one fires
    // (toolbar click) it dispatches through on_alloc_command on the host's wx-event command path -
    // never a raw WM_COMMAND, so ids above 32767 survive the 16-bit wrap (see nib.alloc in nib.h).
    if (g_alloc && g_alloc->alloc_cmd_ids && funcs && count > 0) {
        int first = 0;
        if (g_alloc->alloc_cmd_ids(host, count, &first))
            for (int i = 0; i < count; ++i) funcs[i]._cmdID = first + i;
    }
    registerFuncItems(host, cmds, toUtf8(getName()), funcs, count);
    g_plugins.push_back({ lib,
        reinterpret_cast<PBENOTIFIED> (dllSym(lib, "beNotified")),
        reinterpret_cast<PMESSAGEPROC>(dllSym(lib, "messageProc")),
        funcs, count, moduleFile });
    return true;
}

#ifdef _WIN32
// Scan <exe>/plugins/<Name>/<Name>.dll, LoadLibrary each, setInfo(NppData), and register its commands.
static void loadNppPlugins(NibHost* host, const NibCommandsApi* cmds)
{
    const std::wstring pdir = exeDir() + L"\\plugins";
    WIN32_FIND_DATAW fd;
    HANDLE hf = ::FindFirstFileW((pdir + L"\\*").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring sub = fd.cFileName;
        const std::wstring dll = pdir + L"\\" + sub + L"\\" + sub + kPluginExt;
        if (::GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        DllHandle lib = dllOpen(dll);
        if (!lib) continue;
        loadOneNppModule(host, cmds, lib, nullptr, nullptr, sub + kPluginExt);   // Windows plugins use the OS ::SendMessage (no shim)
    } while (::FindNextFileW(hf, &fd));
    ::FindClose(hf);
}
#endif // _WIN32

// Stage 4: the bridge owns the NPPM_* router. On Windows it subclasses the frame; off-Windows the router
// tail is reached via the plugin's own SendMessage through bridge_dispatch. The common messages are
// answered from the bridge's own resources; file-path / open-save / docking NPPM need richer Nib
// interfaces, so they are acknowledged-but-stubbed for now.
#ifdef _WIN32
static HMENU g_pluginsMenu = nullptr;
#endif

// Copy src into a plugin-provided wchar buffer with a bounded, NUL-terminated copy. On Windows this is
// exactly ::lstrcpynW (unchanged truncation/NUL semantics); off-Windows a portable equivalent.
static void copyWideZ(wchar_t* dst, const wchar_t* src, int cap)
{
#ifdef _WIN32
    ::lstrcpynW(dst, src, cap);
#else
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = L'\0';
#endif
}

// The active document's full path (wide), via nib.documents; empty if untitled or unavailable.
static std::wstring activePathW()
{
    if (!g_docs || !g_host) return {};
    char buf[2048];
    int n = g_docs->active_path(g_host, buf, static_cast<int>(sizeof(buf)));
    return n <= 0 ? std::wstring{} : wFromUtf8(buf);
}
// The full path (wide) of the document with buffer id `id`; empty if not found (needs nib.documents v2).
static std::wstring pathFromIdW(intptr_t id)
{
    if (!g_docs || !g_host || g_docs->version < 2 || !g_docs->path_from_id) return {};
    char buf[2048];
    int n = g_docs->path_from_id(g_host, id, buf, static_cast<int>(sizeof(buf)));
    return n <= 0 ? std::wstring{} : wFromUtf8(buf);
}
// The focused editor view (0 = main, 1 = sub), via nib.documents v3; 0 (main) if unavailable.
static int activeView()
{
    return (g_docs && g_docs->version >= 3 && g_docs->active_view) ? g_docs->active_view(g_host) : 0;
}
// Map a file path's extension to a Notepad++ LangType. The L_* enum is N++ ABI, so this mapping lives in
// the GPL bridge (not the permissive core). Untitled / unknown -> L_TEXT.
static int langTypeForPath(const std::wstring& path)
{
    const size_t dot = path.find_last_of(L'.');
    const size_t sl  = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sl != std::wstring::npos && dot < sl)) return L_TEXT;
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);   // ASCII lower
    auto is = [&](const wchar_t* s){ return e == s; };
    if (is(L"cpp")||is(L"cc")||is(L"cxx")||is(L"hpp")||is(L"hxx")||is(L"ino")) return L_CPP;
    if (is(L"c")||is(L"h"))                  return L_C;
    if (is(L"cs"))                           return L_CS;
    if (is(L"py")||is(L"pyw"))               return L_PYTHON;
    if (is(L"js")||is(L"mjs")||is(L"jsx"))   return L_JAVASCRIPT;
    if (is(L"ts")||is(L"tsx"))               return L_TYPESCRIPT;
    if (is(L"json"))                         return L_JSON;
    if (is(L"java"))                         return L_JAVA;
    if (is(L"html")||is(L"htm"))             return L_HTML;
    if (is(L"xml")||is(L"xaml")||is(L"svg")) return L_XML;
    if (is(L"css"))                          return L_CSS;
    if (is(L"php"))                          return L_PHP;
    if (is(L"sql"))                          return L_SQL;
    if (is(L"lua"))                          return L_LUA;
    if (is(L"pl")||is(L"pm"))                return L_PERL;
    if (is(L"rb"))                           return L_RUBY;
    if (is(L"rs"))                           return L_RUST;
    if (is(L"go"))                           return L_GOLANG;
    if (is(L"sh")||is(L"bash"))              return L_BASH;
    if (is(L"bat")||is(L"cmd"))              return L_BATCH;
    if (is(L"ini"))                          return L_INI;
    if (is(L"yml")||is(L"yaml"))             return L_YAML;
    if (is(L"vb"))                           return L_VB;
    if (is(L"ps1"))                          return L_POWERSHELL;
    if (is(L"pas"))                          return L_PASCAL;
    if (is(L"tex"))                          return L_TEX;
    if (is(L"asm")||is(L"s"))                return L_ASM;
    if (is(L"diff")||is(L"patch"))           return L_DIFF;
    return L_TEXT;
}
// Copy s into the plugin's wchar buffer (lParam); wParam is the buffer cap when set. Returns the length.
static LRESULT putPath(WPARAM wParam, LPARAM lParam, const std::wstring& s)
{
    if (lParam) copyWideZ(reinterpret_cast<wchar_t*>(lParam), s.c_str(), wParam ? static_cast<int>(wParam) : MAX_PATH);
    return static_cast<LRESULT>(s.size());
}

// ---- Phase-1 helpers: plugin relay, allocated-command dispatch, dark palette, language map -------

// ASCII-case-insensitive wide compare - enough for the ASCII module filenames N++ plugins ship under.
static bool wideIEq(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t x = a[i], y = b[i];
        if (x >= L'A' && x <= L'Z') x = static_cast<wchar_t>(x + 32);
        if (y >= L'A' && y <= L'Z') y = static_cast<wchar_t>(y + 32);
        if (x != y) return false;
    }
    return true;
}
static std::wstring stemOf(const std::wstring& f)
{ const size_t d = f.find_last_of(L'.'); return d == std::wstring::npos ? f : f.substr(0, d); }
// NPPM_MSGTOPLUGIN addressing: match the recorded module filename - or, so a POSIX-recompiled plugin
// keeps answering to the name its Windows binary shipped under (L"NppExec.dll" vs NppExec.so), the stem.
static bool moduleMatches(const std::wstring& module, const wchar_t* want)
{
    if (!want || !*want) return false;
    const std::wstring w = want;
    return wideIEq(module, w) || wideIEq(stemOf(module), stemOf(w));
}

// The FuncItem a bridge-assigned command id belongs to (NULL if the id is not one of ours).
static const FuncItem* funcItemForCmd(int cmdId)
{
    if (cmdId == 0) return nullptr;
    for (const auto& p : g_plugins)
        for (int i = 0; i < p.count; ++i)
            if (p.funcs && p.funcs[i]._cmdID == cmdId) return &p.funcs[i];
    return nullptr;
}

// The bridge's nib.alloc command sink: a fired allocated id is either one of the FuncItem ids the
// bridge assigned at load (invoke that item's _pFunc - the same action its menu entry runs), or a
// dynamic id a plugin allocated itself via NPPM_ALLOCATECMDID - relay WM_COMMAND to every plugin's
// messageProc, exactly how N++ relays a dynamic command id it cannot resolve (plugins filter on the
// id). Registered once in activate(); the HOST drops the sink before plugin unmap (nib.alloc
// contract), so no pointer here outlives FreeLibrary.
static void on_alloc_command(NibHost*, int cmdId, void*)
{
    if (const FuncItem* fi = funcItemForCmd(cmdId)) { if (fi->_pFunc) fi->_pFunc(); return; }
    for (const auto& p : g_plugins)
        if (p.messageProc) p.messageProc(WM_COMMAND, static_cast<WPARAM>(cmdId), 0);
}

// The NPPM_GETDARKMODECOLORS payload - N++'s NppDarkMode::Colors ABI layout (12 COLORREFs, this exact
// order). Declared by this GPL module (the npp-compat header carries only the dmf* constants); the
// static_assert below keeps the layout from drifting silently, abi_layout_asserts.h-style.
namespace NppDarkMode {
    struct Colors {
        COLORREF background       = 0;
        COLORREF softerBackground = 0;
        COLORREF hotBackground    = 0;
        COLORREF pureBackground   = 0;
        COLORREF errorBackground  = 0;
        COLORREF text             = 0;
        COLORREF darkerText       = 0;
        COLORREF disabledText     = 0;
        COLORREF linkText         = 0;
        COLORREF edge             = 0;
        COLORREF hotEdge          = 0;
        COLORREF disabledEdge     = 0;
    };
}
static_assert(sizeof(NppDarkMode::Colors) == 12 * sizeof(COLORREF), "NppDarkMode::Colors ABI layout drift");

// nib.ui speaks portable 0xRRGGBB; the N++ ABI (like Scintilla) speaks COLORREF 0x00bbggrr.
static COLORREF rgbToColorref(uint32_t rgb)
{ return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16); }

// LangType -> the host's frozen Language-menu command id (kCmd* == IDM_LANG_*, the composition lever).
// Only languages the host's own Language menu carries map; the rest return 0 and
// NPPM_SETCURRENTLANGTYPE answers a documented FALSE (never a silently-dead menu id).
static int menuIdForLangType(int lt)
{
    switch (lt) {
        case L_TEXT: return IDM_LANG_TEXT;               // "None (Normal Text)"
        case L_PHP: return IDM_LANG_PHP;                 case L_C: return IDM_LANG_C;
        case L_CPP: return IDM_LANG_CPP;                 case L_CS: return IDM_LANG_CS;
        case L_OBJC: return IDM_LANG_OBJC;               case L_JAVA: return IDM_LANG_JAVA;
        case L_RC: return IDM_LANG_RC;                   case L_HTML: return IDM_LANG_HTML;
        case L_XML: return IDM_LANG_XML;                 case L_MAKEFILE: return IDM_LANG_MAKEFILE;
        case L_PASCAL: return IDM_LANG_PASCAL;           case L_BATCH: return IDM_LANG_BATCH;
        case L_ASP: return IDM_LANG_ASP;                 case L_SQL: return IDM_LANG_SQL;
        case L_VB: return IDM_LANG_VB;                   case L_CSS: return IDM_LANG_CSS;
        case L_PERL: return IDM_LANG_PERL;               case L_PYTHON: return IDM_LANG_PYTHON;
        case L_LUA: return IDM_LANG_LUA;                 case L_TEX: return IDM_LANG_TEX;
        case L_FORTRAN: return IDM_LANG_FORTRAN;         case L_BASH: return IDM_LANG_BASH;
        case L_FLASH: return IDM_LANG_FLASH;             case L_NSIS: return IDM_LANG_NSIS;
        case L_TCL: return IDM_LANG_TCL;                 case L_LISP: return IDM_LANG_LISP;
        case L_SCHEME: return IDM_LANG_SCHEME;           case L_ASM: return IDM_LANG_ASM;
        case L_DIFF: return IDM_LANG_DIFF;               case L_PROPS: return IDM_LANG_PROPS;
        case L_PS: return IDM_LANG_PS;                   case L_RUBY: return IDM_LANG_RUBY;
        case L_SMALLTALK: return IDM_LANG_SMALLTALK;     case L_VHDL: return IDM_LANG_VHDL;
        case L_KIX: return IDM_LANG_KIX;                 case L_AU3: return IDM_LANG_AU3;
        case L_CAML: return IDM_LANG_CAML;               case L_ADA: return IDM_LANG_ADA;
        case L_VERILOG: return IDM_LANG_VERILOG;         case L_MATLAB: return IDM_LANG_MATLAB;
        case L_HASKELL: return IDM_LANG_HASKELL;         case L_INNO: return IDM_LANG_INNO;
        case L_CMAKE: return IDM_LANG_CMAKE;             case L_YAML: return IDM_LANG_YAML;
        case L_COBOL: return IDM_LANG_COBOL;             case L_GUI4CLI: return IDM_LANG_GUI4CLI;
        case L_D: return IDM_LANG_D;                     case L_POWERSHELL: return IDM_LANG_POWERSHELL;
        case L_R: return IDM_LANG_R;                     case L_JSP: return IDM_LANG_JSP;
        case L_COFFEESCRIPT: return IDM_LANG_COFFEESCRIPT; case L_JSON: return IDM_LANG_JSON;
        case L_JAVASCRIPT: return IDM_LANG_JS;           case L_FORTRAN_77: return IDM_LANG_FORTRAN_77;
        case L_BAANC: return IDM_LANG_BAANC;             case L_SREC: return IDM_LANG_SREC;
        case L_IHEX: return IDM_LANG_IHEX;               case L_TEHEX: return IDM_LANG_TEHEX;
        case L_SWIFT: return IDM_LANG_SWIFT;             case L_ASN1: return IDM_LANG_ASN1;
        case L_AVS: return IDM_LANG_AVS;                 case L_BLITZBASIC: return IDM_LANG_BLITZBASIC;
        case L_PUREBASIC: return IDM_LANG_PUREBASIC;     case L_FREEBASIC: return IDM_LANG_FREEBASIC;
        case L_CSOUND: return IDM_LANG_CSOUND;           case L_ERLANG: return IDM_LANG_ERLANG;
        case L_ESCRIPT: return IDM_LANG_ESCRIPT;         case L_FORTH: return IDM_LANG_FORTH;
        case L_LATEX: return IDM_LANG_LATEX;             case L_MMIXAL: return IDM_LANG_MMIXAL;
        case L_NIM: return IDM_LANG_NIM;                 case L_NNCRONTAB: return IDM_LANG_NNCRONTAB;
        case L_OSCRIPT: return IDM_LANG_OSCRIPT;         case L_REBOL: return IDM_LANG_REBOL;
        case L_REGISTRY: return IDM_LANG_REGISTRY;       case L_RUST: return IDM_LANG_RUST;
        case L_SPICE: return IDM_LANG_SPICE;             case L_TXT2TAGS: return IDM_LANG_TXT2TAGS;
        case L_VISUALPROLOG: return IDM_LANG_VISUALPROLOG; case L_TYPESCRIPT: return IDM_LANG_TYPESCRIPT;
        case L_JSON5: return IDM_LANG_JSON5;             case L_MSSQL: return IDM_LANG_MSSQL;
        case L_GDSCRIPT: return IDM_LANG_GDSCRIPT;       case L_HOLLYWOOD: return IDM_LANG_HOLLYWOOD;
        case L_GOLANG: return IDM_LANG_GOLANG;           case L_RAKU: return IDM_LANG_RAKU;
        case L_TOML: return IDM_LANG_TOML;               case L_SAS: return IDM_LANG_SAS;
        default: return 0;   // L_INI/L_ASCII/L_USER/L_JS_EMBEDDED/... have no host Language-menu entry
    }
}

#ifdef _WIN32
// ---- HBITMAP/HICON -> straight-alpha RGBA (the nib.toolbar ABI) ----------------------------------
// The alpha-fidelity cases (the plan's named risk): a plugin's toolbarIcons bitmap may be 32bpp with
// PREMULTIPLIED alpha, 32bpp with a dead (all-zero) alpha channel, or classic 24bpp using N++'s
// RGB(192,192,192) "toolbar grey" background as the implicit transparency mask. Icons carry straight
// alpha - or none at all, in which case it is derived from the icon's AND mask.
static bool dibBits32(HDC dc, HBITMAP hbm, int w, int h, std::vector<unsigned char>& bgra)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;                     // negative height = top-down row order
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bgra.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    return ::GetDIBits(dc, hbm, 0, static_cast<UINT>(h), bgra.data(), &bi, DIB_RGB_COLORS) == h;
}
static void bgraToRgba(const std::vector<unsigned char>& bgra, std::vector<unsigned char>& rgba)
{
    rgba.resize(bgra.size());
    for (size_t i = 0; i + 3 < bgra.size(); i += 4)
    { rgba[i] = bgra[i + 2]; rgba[i + 1] = bgra[i + 1]; rgba[i + 2] = bgra[i]; rgba[i + 3] = bgra[i + 3]; }
}
static bool hbmpToRgba(HBITMAP hbm, std::vector<unsigned char>& rgba, int& w, int& h)
{
    BITMAP bm = {};
    if (!hbm || !::GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return false;
    w = bm.bmWidth; h = bm.bmHeight;
    HDC dc = ::CreateCompatibleDC(nullptr);
    if (!dc) return false;
    std::vector<unsigned char> px;
    const bool ok = dibBits32(dc, hbm, w, h, px);
    ::DeleteDC(dc);
    if (!ok) return false;
    bool anyAlpha = false, premulShaped = true;
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        const unsigned char b = px[i], g = px[i + 1], r = px[i + 2], a = px[i + 3];
        if (a) anyAlpha = true;
        if (b > a || g > a || r > a) premulShaped = false;   // impossible shape for premultiplied data
    }
    if (bm.bmBitsPixel == 32 && anyAlpha) {
        // A live alpha channel. GDI 32bpp bitmaps are conventionally PREMULTIPLIED (AlphaBlend's
        // format): when every channel is <= its alpha - the only shape premultiplied data can have -
        // un-premultiply into the straight alpha the nib ABI wants; otherwise it can only already be
        // straight and is copied through.
        if (premulShaped)
            for (size_t i = 0; i + 3 < px.size(); i += 4) {
                const unsigned a = px[i + 3];
                if (a && a != 255)
                    for (int c = 0; c < 3; ++c)
                        px[i + c] = static_cast<unsigned char>((px[i + c] * 255u + a / 2) / a);
            }
    } else {
        // 24bpp (or a 32bpp source whose alpha channel is dead): opaque, except N++'s documented
        // RGB(192,192,192) transparent-background convention for plugin toolbar bitmaps.
        for (size_t i = 0; i + 3 < px.size(); i += 4)
            px[i + 3] = (px[i] == 192 && px[i + 1] == 192 && px[i + 2] == 192) ? 0 : 255;
    }
    bgraToRgba(px, rgba);
    return true;
}
static bool hiconToRgba(HICON hic, std::vector<unsigned char>& rgba, int& w, int& h)
{
    ICONINFO ii = {};
    if (!hic || !::GetIconInfo(hic, &ii)) return false;
    bool ok = false;
    if (ii.hbmColor) {   // colour icon (a monochrome icon's double-height mask layout is not served)
        BITMAP bm = {};
        if (::GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            w = bm.bmWidth; h = bm.bmHeight;
            if (HDC dc = ::CreateCompatibleDC(nullptr)) {
                std::vector<unsigned char> px, mask;
                if (dibBits32(dc, ii.hbmColor, w, h, px)) {
                    bool anyAlpha = false;
                    for (size_t i = 3; i < px.size(); i += 4) if (px[i]) { anyAlpha = true; break; }
                    if (!anyAlpha) {
                        // No per-pixel alpha: derive it from the icon's AND mask (a set mask bit
                        // reads back white = transparent), or fall back to fully opaque.
                        if (ii.hbmMask && dibBits32(dc, ii.hbmMask, w, h, mask))
                            for (size_t i = 0; i + 3 < px.size(); i += 4) px[i + 3] = mask[i] ? 0 : 255;
                        else
                            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
                    }
                    // Icon colour planes carry STRAIGHT alpha (DrawIconEx blends at draw time):
                    // no un-premultiply here, just the BGRA->RGBA channel swap.
                    bgraToRgba(px, rgba);
                    ok = true;
                }
                ::DeleteDC(dc);
            }
        }
    }
    if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  ::DeleteObject(ii.hbmMask);
    return ok;
}
#endif // _WIN32

static bool bridge_handleNppm(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& out)
{
    switch (msg) {
        case NPPM_GETCURRENTSCINTILLA: if (lParam) *reinterpret_cast<int*>(lParam) = activeView(); out = TRUE; return true;   // 0=main, 1=sub
        case NPPM_GETCURRENTLANGTYPE:  if (lParam) *reinterpret_cast<int*>(lParam) = langTypeForPath(activePathW()); out = TRUE; return true;
        case NPPM_GETNPPVERSION:       out = MAKELONG(96, 8); return true;
        case NPPM_GETNBOPENFILES:      out = g_docs ? g_docs->count(g_host) : 1; return true;
        case NPPM_GETOPENFILENAMES_DEPRECATED: {   // classic "list every open file's path" (the value plugins still send)
            wchar_t** names = reinterpret_cast<wchar_t**>(wParam);
            const int nb = static_cast<int>(lParam);
            if (!names || !g_docs || g_docs->version < 4 || !g_docs->doc_path_at) { out = 0; return true; }
            const int total = g_docs->count(g_host);
            int filled = 0;
            for (; filled < nb && filled < total; ++filled) {
                char utf8[MAX_PATH * 3] = {0};
                g_docs->doc_path_at(g_host, filled, utf8, static_cast<int>(sizeof(utf8)));
                if (names[filled]) copyWideZ(names[filled], wFromUtf8(utf8).c_str(), MAX_PATH);
            }
            out = filled; return true;
        }
        case NPPM_GETMENUHANDLE:
#ifdef _WIN32
            out = reinterpret_cast<LRESULT>(g_pluginsMenu);
#else
            out = 0;   // no native menu handle off-Windows
#endif
            return true;
        case NPPM_MENUCOMMAND:
            // Invoke the built-in command by id through nib.commands (portable, and it routes via the wx
            // menu dispatcher so large frozen ids don't wrap in a 16-bit WM_COMMAND). Fall back to a raw
            // WM_COMMAND on Windows only if the host predates nib.commands v2.
            if (g_cmds && g_cmds->version >= 2 && g_cmds->invoke_command)
                g_cmds->invoke_command(g_host, static_cast<int>(lParam));
#ifdef _WIN32
            else
                ::SendMessageW(g_npp._nppHandle, WM_COMMAND, static_cast<WPARAM>(lParam), 0);
#endif
            out = TRUE; return true;
        case NPPM_GETNPPDIRECTORY:     if (lParam) copyWideZ(reinterpret_cast<wchar_t*>(lParam), exeDir().c_str(), MAX_PATH); out = TRUE; return true;
        case NPPM_GETPLUGINSCONFIGDIR: if (lParam) copyWideZ(reinterpret_cast<wchar_t*>(lParam), pluginsConfigDirW().c_str(), MAX_PATH); out = TRUE; return true;
        // the active document's path family, served from nib.documents
        case NPPM_GETFULLCURRENTPATH:  out = putPath(wParam, lParam, activePathW()); return true;
        case NPPM_GETCURRENTDIRECTORY: { const std::wstring p = activePathW(); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, s == std::wstring::npos ? L"" : p.substr(0, s)); return true; }
        case NPPM_GETFILENAME:         { const std::wstring p = activePathW(); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, s == std::wstring::npos ? p : p.substr(s + 1)); return true; }
        case NPPM_GETNAMEPART:         { std::wstring f = activePathW(); const size_t s = f.find_last_of(L"\\/"); if (s != std::wstring::npos) f = f.substr(s + 1); const size_t d = f.find_last_of(L'.'); out = putPath(wParam, lParam, d == std::wstring::npos ? f : f.substr(0, d)); return true; }
        case NPPM_GETEXTPART:          { const std::wstring p = activePathW(); const size_t d = p.find_last_of(L'.'); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, (d != std::wstring::npos && (s == std::wstring::npos || d > s)) ? p.substr(d) : L""); return true; }
        case NPPM_DOOPEN:              if (g_docs && lParam) g_docs->open(g_host, toUtf8(reinterpret_cast<const wchar_t*>(lParam)).c_str()); out = TRUE; return true;
        case NPPM_SAVECURRENTFILE:     out = (g_docs && g_docs->save_active(g_host)) ? TRUE : FALSE; return true;
        case NPPM_SAVEALLFILES:        // "save every open file" -> the host's own Save All command (frozen id)
            if (g_cmds && g_cmds->version >= 2 && g_cmds->invoke_command) g_cmds->invoke_command(g_host, IDM_FILE_SAVEALL);
            out = TRUE; return true;
        // docking: the plugin owns a native window and asks us to host it in a dock pane (nib.win32; NULL off-Windows -> inert)
        case NPPM_DMMREGASDCKDLG:      if (g_win32 && lParam) {
                                           const tTbData* d = reinterpret_cast<const tTbData*>(lParam);
                                           int edge = 0;   // bottom
                                           switch ((d->uMask >> 28) & 0x0F) { case CONT_LEFT: edge = 1; break; case CONT_RIGHT: edge = 2; break; case CONT_TOP: edge = 3; break; default: edge = 0; }
                                           if (d->hClient) g_win32->dock_native(g_host, d->hClient, toUtf8(d->pszName).c_str(), edge);
                                       } out = TRUE; return true;
        case NPPM_DMMSHOW:             if (g_win32 && lParam) g_win32->show_dock(g_host, reinterpret_cast<void*>(lParam), 1); out = TRUE; return true;
        case NPPM_DMMHIDE:             if (g_win32 && lParam) g_win32->show_dock(g_host, reinterpret_cast<void*>(lParam), 0); out = TRUE; return true;
        case NPPM_DMMUPDATEDISPINFO:
#ifdef _WIN32
            if (lParam) ::InvalidateRect(reinterpret_cast<HWND>(lParam), nullptr, TRUE);
#else
            (void)lParam;   // the host repaints its own dock panes off-Windows
#endif
            out = TRUE; return true;
        // ---- additional coverage, served from existing nib.* + the frame's native children ----
        case NPPM_SWITCHTOFILE:        if (g_docs && lParam) g_docs->open(g_host, toUtf8(reinterpret_cast<const wchar_t*>(lParam)).c_str()); out = TRUE; return true;   // open-or-switch
        case NPPM_GETCURRENTVIEW:      out = activeView(); return true;   // 0=main, 1=sub
        // cursor position, via nib.sci (the portable SCI_* passthrough) - some plugins query these
        // rather than calling Scintilla directly. Both 0-based, as in Notepad++.
        case NPPM_GETCURRENTLINE:
            out = g_sci ? static_cast<LRESULT>(g_sci->sci_call(g_host, activeView(), SCI_LINEFROMPOSITION,
                      static_cast<uintptr_t>(g_sci->sci_call(g_host, activeView(), SCI_GETCURRENTPOS, 0, 0)), 0)) : 0;
            return true;
        case NPPM_GETCURRENTCOLUMN:
            out = g_sci ? static_cast<LRESULT>(g_sci->sci_call(g_host, activeView(), SCI_GETCOLUMN,
                      static_cast<uintptr_t>(g_sci->sci_call(g_host, activeView(), SCI_GETCURRENTPOS, 0, 0)), 0)) : 0;
            return true;
        case NPPM_GETBUFFERLANGTYPE:   out = langTypeForPath(pathFromIdW(static_cast<intptr_t>(wParam))); return true;   // by file extension
        case NPPM_GETPLUGINHOMEPATH:   if (lParam) copyWideZ(reinterpret_cast<wchar_t*>(lParam), pluginsRootW().c_str(), wParam ? static_cast<int>(wParam) : MAX_PATH); out = TRUE; return true;
        case NPPM_SETSTATUSBAR:
#ifdef _WIN32
            { HWND sb = ::FindWindowExW(g_npp._nppHandle, nullptr, L"msctls_statusbar32", nullptr);   // the wx status bar is a native msctls_statusbar32
              if (sb && lParam) ::SendMessageW(sb, SB_SETTEXTW, static_cast<WPARAM>(wParam), lParam); }
#else
            (void)wParam; (void)lParam;   // off-Windows status-bar text will route through a future nib capability
#endif
            out = TRUE; return true;
        // per-buffer tracking, via nib.documents v2 (the host's EditorPage* is the opaque buffer id)
        case NPPM_GETCURRENTBUFFERID:      out = (g_docs && g_docs->version >= 2 && g_docs->active_id) ? g_docs->active_id(g_host) : 0; return true;
        case NPPM_GETFULLPATHFROMBUFFERID: out = putPath(0, lParam, pathFromIdW(static_cast<intptr_t>(wParam))); return true;
        // ---- Phase 1: dark mode + editor default colours (nib.ui / nib.sci) ----
        case NPPM_ISDARKMODEENABLED:
            out = (g_ui && g_ui->is_dark && g_ui->is_dark(g_host)) ? TRUE : FALSE; return true;
        case NPPM_GETDARKMODECOLORS: {
            // wParam must be sizeof(NppDarkMode::Colors), lParam the caller's struct (N++ semantics);
            // the host's portable 0xRRGGBB palette is laid into the COLORREF ABI layout.
            auto* colors = reinterpret_cast<NppDarkMode::Colors*>(lParam);
            NibUiDarkColors c = {}; c.version = 1; c.struct_size = sizeof(c);
            if (static_cast<size_t>(wParam) != sizeof(NppDarkMode::Colors) || !colors
                || !g_ui || !g_ui->dark_colors || !g_ui->dark_colors(g_host, &c)) { out = FALSE; return true; }
            colors->background       = rgbToColorref(c.background);
            colors->softerBackground = rgbToColorref(c.softer_background);
            colors->hotBackground    = rgbToColorref(c.hot_background);
            colors->pureBackground   = rgbToColorref(c.pure_background);
            colors->errorBackground  = rgbToColorref(c.error_background);
            colors->text             = rgbToColorref(c.text);
            colors->darkerText       = rgbToColorref(c.darker_text);
            colors->disabledText     = rgbToColorref(c.disabled_text);
            colors->linkText         = rgbToColorref(c.link_text);
            colors->edge             = rgbToColorref(c.edge);
            colors->hotEdge          = rgbToColorref(c.hot_edge);
            colors->disabledEdge     = rgbToColorref(c.disabled_edge);
            out = TRUE; return true;
        }
        case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR:   // returns COLORREF 0x00bbggrr == Scintilla's own colour layout
            out = g_sci ? static_cast<LRESULT>(g_sci->sci_call(g_host, activeView(), SCI_STYLEGETFORE, STYLE_DEFAULT, 0) & 0xFFFFFF) : 0;
            return true;
        case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR:
            out = g_sci ? static_cast<LRESULT>(g_sci->sci_call(g_host, activeView(), SCI_STYLEGETBACK, STYLE_DEFAULT, 0) & 0xFFFFFF) : 0;
            return true;
        // ---- Phase 1: menu state / language / reload / plugin-to-plugin relay ----
        case NPPM_SETMENUITEMCHECK:
            // wParam = a FuncItem's bridge-assigned cmd id, lParam = check state. Honest answer: TRUE
            // only when the host found a real CHECKABLE menu item under that id - its Extensions-menu
            // entries are not checkable today, so plugin items report FALSE rather than pretending.
            out = (g_ui && g_ui->menu_check && g_ui->menu_check(g_host, static_cast<int>(wParam), lParam ? 1 : 0)) ? TRUE : FALSE;
            return true;
        case NPPM_SETCURRENTLANGTYPE: {
            // lParam = the LangType to force on the active buffer. Composition: the host's own
            // Language-menu command (frozen kCmd* == IDM_LANG_*), through the same wx-event
            // dispatcher a menu pick uses - never a raw WM_COMMAND.
            const int id = menuIdForLangType(static_cast<int>(lParam));
            if (!id || !g_cmds || g_cmds->version < 2 || !g_cmds->invoke_command) { out = FALSE; return true; }
            g_cmds->invoke_command(g_host, id);
            out = TRUE; return true;
        }
        case NPPM_RELOADFILE: {
            // lParam = full path to reload (wParam's "with alert" flag is not honoured - the host
            // reloads silently). Open-or-switch to the file, then File > Reload (IDM_FILE_RELOAD,
            // frozen id) re-reads the now-active buffer from disk.
            const wchar_t* p = reinterpret_cast<const wchar_t*>(lParam);
            if (!p || !*p || !g_docs || !g_docs->open(g_host, toUtf8(p).c_str())) { out = FALSE; return true; }
            if (g_cmds && g_cmds->version >= 2 && g_cmds->invoke_command) g_cmds->invoke_command(g_host, IDM_FILE_RELOAD);
            out = TRUE; return true;
        }
        case NPPM_MSGTOPLUGIN: {
            // wParam = dest module filename (e.g. L"NppExec.dll"), lParam = CommunicationInfo*.
            // Deliver to the named plugin's already-resolved messageProc, N++-style (the dest reads
            // internalMsg/srcModuleName/info out of lParam); TRUE only if actually delivered.
            const wchar_t* dest = reinterpret_cast<const wchar_t*>(wParam);
            out = FALSE;
            if (!dest || !*dest || !lParam) return true;
            for (const auto& p : g_plugins)
                if (p.messageProc && moduleMatches(p.module, dest)) { p.messageProc(msg, wParam, lParam); out = TRUE; break; }
            return true;
        }
        // ---- Phase 1: toolbar buttons (Windows converts the native images to the portable RGBA ABI) ----
        case NPPM_ADDTOOLBARICON_DEPRECATED:      // == the pre-rename NPPM_ADDTOOLBARICON value shipped binaries / C# SDKs still send
        case NPPM_ADDTOOLBARICON_FORDARKMODE: {
#ifdef _WIN32
            const auto* ic = reinterpret_cast<const toolbarIcons*>(lParam);   // FORDARKMODE only appends a field
            if (!g_toolbar || !g_toolbar->add_tool || !ic || !wParam) { out = FALSE; return true; }
            const HICON darkIcon = (msg == NPPM_ADDTOOLBARICON_FORDARKMODE)
                ? reinterpret_cast<const toolbarIconsWithDarkMode*>(lParam)->hToolbarIconDarkMode : nullptr;
            const bool dark = g_ui && g_ui->is_dark && g_ui->is_dark(g_host);
            std::vector<unsigned char> rgba; int w = 0, h = 0;
            bool got = false;
            if (dark) {   // dark chrome: the dark icon variant first, then the light icon, then the bmp
                if (darkIcon)                 got = hiconToRgba(darkIcon, rgba, w, h);
                if (!got && ic->hToolbarIcon) got = hiconToRgba(ic->hToolbarIcon, rgba, w, h);
                if (!got && ic->hToolbarBmp)  got = hbmpToRgba(ic->hToolbarBmp, rgba, w, h);
            } else {      // light chrome: the classic bmp is what plugins design for light toolbars
                if (ic->hToolbarBmp)          got = hbmpToRgba(ic->hToolbarBmp, rgba, w, h);
                if (!got && ic->hToolbarIcon) got = hiconToRgba(ic->hToolbarIcon, rgba, w, h);
                if (!got && darkIcon)         got = hiconToRgba(darkIcon, rgba, w, h);
            }
            if (!got) { out = FALSE; return true; }
            NibToolbarIcon icon = {};
            icon.version = 1; icon.struct_size = sizeof(icon);
            icon.width = w; icon.height = h; icon.rgba = rgba.data();   // host copies the pixels immediately
            const FuncItem* fi = funcItemForCmd(static_cast<int>(wParam));   // tooltip = the item's menu name, as N++
            const std::string tip = fi ? toUtf8(fi->_itemName) : std::string();
            out = g_toolbar->add_tool(g_host, static_cast<int>(wParam), &icon, tip.c_str()) ? TRUE : FALSE;
#else
            // A recompiled plugin cannot own an HBITMAP/HICON off-Windows: documented no-op SUCCESS,
            // so TBMODIFICATION-driven init paths keep running (the notification fires on every OS).
            (void)wParam; (void)lParam;
            out = TRUE;
#endif
            return true;
        }
        // ---- Phase 1: the allocator family (host-owned ranges; fired ids dispatch via wx events) ----
        case NPPM_ALLOCATESUPPORTED_DEPRECATED:   // == the pre-rename NPPM_ALLOCATESUPPORTED probe (DSpellCheck gates on it)
            out = TRUE; return true;
        case NPPM_ALLOCATECMDID:                  // wParam = count, lParam = int* first granted number (all three)
            out = (g_alloc && g_alloc->alloc_cmd_ids && lParam
                   && g_alloc->alloc_cmd_ids(g_host, static_cast<int>(wParam), reinterpret_cast<int*>(lParam))) ? TRUE : FALSE;
            return true;
        case NPPM_ALLOCATEMARKER:
            out = (g_alloc && g_alloc->alloc_markers && lParam
                   && g_alloc->alloc_markers(g_host, static_cast<int>(wParam), reinterpret_cast<int*>(lParam))) ? TRUE : FALSE;
            return true;
        case NPPM_ALLOCATEINDICATOR:              // also the value C# SDKs spell NPPM_ALLOCATEINDICATORS (same number)
            out = (g_alloc && g_alloc->alloc_indicators && lParam
                   && g_alloc->alloc_indicators(g_host, static_cast<int>(wParam), reinterpret_cast<int*>(lParam))) ? TRUE : FALSE;
            return true;
        default:                       return false;   // not one of ours -> caller falls through (DefSubclassProc / 0)
    }
}

#ifdef _WIN32
static LRESULT CALLBACK bridge_frame_proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (msg >= (WM_USER + 1000)) {   // NPPM_ (NPPMSG = WM_USER+1000) + the RUNCOMMAND_USER (WM_USER+3000) family
        LRESULT out = 0;
        if (bridge_handleNppm(msg, w, l, out)) return out;
    }
    return ::DefSubclassProc(h, msg, w, l);
}
#endif // _WIN32

#ifndef _WIN32
// ---- NON-WINDOWS dispatch seam: the plugin's own SendMessage(NPPM_*/SCI_*) lands here --------------
// Opaque, non-null NppData handles for the POSIX path: their ADDRESSES are the identity. They are never
// passed to any OS API; bridge_dispatch reverse-maps them by pointer. Stable across tab switches (they
// wrap the view, not the current document).
static char g_tokFrame, g_tokMain, g_tokSub;

static int viewOfHandle(HWND h)
{
    if (h == static_cast<HWND>(&g_tokMain)) return 0;   // NppData._scintillaMainHandle
    if (h == static_cast<HWND>(&g_tokSub))  return 1;   // NppData._scintillaSecondHandle
    return -1;                                          // the frame handle (or unknown) -> not an editor
}

// Plain (ctx,handle,msg,w,l) signature -- deliberately NOT a subclass-proc signature -- so the SAME
// function is reachable from the shim seam.
static LRESULT bridge_dispatch(void* /*ctx*/, HWND handle, UINT msg, WPARAM w, LPARAM l)
{
    // Route by the HANDLE, not the message number: NPPM_*/RUNCOMMAND numbers (WM_USER+1000 = 2024, and
    // 4024+) NEST inside the Scintilla ranges [2000,3000)/[4000,5000), so number alone can't tell them
    // apart. On Windows they're disambiguated by WHICH HWND received the message (editor vs frame); the
    // shim forwards the plugin's handle, so we do the same - an editor handle -> Scintilla (nib.sci),
    // the frame handle -> the de-Win32'd NPPM switch (the identical one the Windows router runs).
    const int view = viewOfHandle(handle);
    if (view >= 0)
        return g_sci ? static_cast<LRESULT>(g_sci->sci_call(g_host, view, msg, w, l)) : 0;

    if (msg >= (WM_USER + 1000)) {
        LRESULT out = 0;
        if (bridge_handleNppm(msg, w, l, out)) return out;
    }
    return 0;
}

// The host-dispatch vtable handed to each plugin's shim (via npp_shim_bind) so its outbound SendMessage
// reaches us. ctx is unused (bridge_dispatch reaches host state through file-scope g_*).
static NppHostBridge g_hostBridge = { nullptr, &bridge_dispatch };

// Scan <pluginsRoot>/<Name>/<Name>.so|.dylib, dlopen each, bind the shim, setInfo(token NppData), register.
static void loadNppPluginsPosix(NibHost* host, const NibCommandsApi* cmds, const std::string& pluginsRoot)
{
    g_npp._nppHandle             = &g_tokFrame;
    g_npp._scintillaMainHandle   = &g_tokMain;
    g_npp._scintillaSecondHandle = &g_tokSub;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(fs::path(pluginsRoot), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string name = it->path().filename().string();
        const fs::path    mod  = it->path() / (name + kPluginExt);   // <Name>/<Name>.so|.dylib
        if (!fs::exists(mod, ec)) continue;

        DllHandle lib = dllOpen(mod.string());
        if (!lib) { std::fprintf(stderr, "[npp-bridge] dlopen failed: %s\n", ::dlerror()); continue; }

        // Wire the plugin's outbound SendMessage (via g_hostBridge) before setInfo; see loadOneNppModule.
        // isUnicode is intentionally NOT resolved/honored in Phase 1 (plugins treated as wide, as today).
        loadOneNppModule(host, cmds, lib, name.c_str(), &g_hostBridge, wFromUtf8((name + kPluginExt).c_str()));
    }
}
#endif // !_WIN32

static void activate(NibHost* host, NibQueryFn query)
{
    g_host = host;
    g_docs = static_cast<const NibDocumentsApi*>(query(host, NIB_IFACE_DOCUMENTS, 1));   // current-file-path NPPM
    g_sci  = static_cast<const NibSciApi*>(query(host, NIB_IFACE_SCI, 1));               // portable SCI_* passthrough (every OS)
    g_paths = static_cast<const NibPathsApi*>(query(host, NIB_IFACE_PATHS, 1));          // off-Windows plugins base + NPPM home/config dir
    const NibWin32Api* w = static_cast<const NibWin32Api*>(query(host, NIB_IFACE_WIN32, 1));
    g_win32 = w;   // docking (NPPM_DMM*) routes through nib.win32; NULL off-Windows
    g_ui      = static_cast<const NibUiApi*>(query(host, NIB_IFACE_UI, 1));            // menu checks + dark palette
    g_toolbar = static_cast<const NibToolbarApi*>(query(host, NIB_IFACE_TOOLBAR, 1));  // plugin toolbar buttons
    g_alloc   = static_cast<const NibAllocApi*>(query(host, NIB_IFACE_ALLOC, 1));      // cmd/marker/indicator grants
    // The bridge's one command sink: fired allocated ids dispatch FuncItem actions and relay dynamic
    // NPPM_ALLOCATECMDID ids to every plugin's messageProc (see on_alloc_command). Registered BEFORE
    // the plugins load so the very first granted id is already routable; the HOST drops the sink
    // before plugin unmap (nib.alloc contract - nothing dangles into an unmapped DLL).
    if (g_alloc && g_alloc->on_command) g_alloc->on_command(host, on_alloc_command, nullptr);
#ifdef _WIN32
    if (w) {
        g_npp._nppHandle             = static_cast<HWND>(w->main_window(host));
        g_npp._scintillaMainHandle   = static_cast<HWND>(w->editor_main(host));
        g_npp._scintillaSecondHandle = static_cast<HWND>(w->editor_second(host));
        g_pluginsMenu = static_cast<HMENU>(w->plugins_menu(host));
        if (g_npp._nppHandle) ::SetWindowSubclass(g_npp._nppHandle, bridge_frame_proc, 1, 0);   // own the NPPM_* router
    }
#endif
    const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1));
    if (!cmds) return;
    g_cmds = cmds;   // stash for NPPM_MENUCOMMAND (invoke a built-in command by id)
    cmds->register_command(host, "org.wxn.npp-bridge.info", "N++ Bridge: NppData Info", cmd_info, nullptr);
#ifdef _WIN32
    if (w) loadNppPlugins(host, cmds);
#else
    {
        // Plugins load from user_data_dir()/plugins (the exe dir isn't user-writable when installed) -
        // the same base pluginsRootW() reports to NPPM_GETPLUGINHOMEPATH/GETPLUGINSCONFIGDIR.
        const std::wstring root = pluginsRootW();
        if (!root.empty()) loadNppPluginsPosix(host, cmds, toUtf8(root.c_str()));
    }
#endif

    // Forward editor events to the loaded plugins' beNotified (Stage 3). The v1 table carries the
    // savepoint-derived SAVED plus the document lifecycle; the v2 table (when the host speaks it) adds
    // the id-carrying real-save pipeline: SAVING -> NPPN_FILEBEFORESAVE, SAVED -> NPPN_FILESAVED
    // (exactly one per real disk write, the written buffer's own id even during background Save All -
    // never a false fire on undo-to-savepoint), BEFORE_OPEN -> NPPN_FILEBEFOREOPEN. Without v2 the
    // bridge falls back to deriving FILESAVED from the savepoint (see on_nib_event).
    const NibEventsApi* events   = static_cast<const NibEventsApi*>(query(host, NIB_IFACE_EVENTS, 1));
    const NibEventsApi* eventsV2 = static_cast<const NibEventsApi*>(query(host, NIB_IFACE_EVENTS, 2));
    g_eventsV2 = eventsV2 != nullptr;
    if (events) {
        events->subscribe(host, NIB_EV_TEXT_CHANGED,       on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_SELECTION_CHANGED,  on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_DOCUMENT_SAVED,     on_nib_event, nullptr);   // savepoint -> SCN_SAVEPOINTREACHED
        events->subscribe(host, NIB_EV_DOCUMENT_ACTIVATED, on_nib_event, nullptr);   // -> NPPN_BUFFERACTIVATED
        events->subscribe(host, NIB_EV_DOCUMENT_OPENED,    on_nib_event, nullptr);   // -> NPPN_FILEOPENED
        events->subscribe(host, NIB_EV_DOCUMENT_CLOSED,    on_nib_event, nullptr);   // -> NPPN_FILEBEFORECLOSE + NPPN_FILECLOSED
    }
    if (eventsV2) {
        eventsV2->subscribe(host, NIB_EV_DOCUMENT_SAVING,      on_nib_event_v2, nullptr);  // -> NPPN_FILEBEFORESAVE
        eventsV2->subscribe(host, NIB_EV_DOCUMENT_SAVED,       on_nib_event_v2, nullptr);  // -> NPPN_FILESAVED (id-carrying)
        eventsV2->subscribe(host, NIB_EV_DOCUMENT_BEFORE_OPEN, on_nib_event_v2, nullptr);  // -> NPPN_FILEBEFOREOPEN
    }

    // Real Notepad++ sends NPPN_READY once its UI is up; the bridge sends it after finishing loading +
    // wiring the plugins, which is the point at which they can safely query the environment. Plugins
    // commonly do first-run init (toolbar buttons, docking dialogs, menu state) in this handler.
    notifyNpp(NPPN_READY);
    // The toolbar-registration window. Real N++ fires TBMODIFICATION while (re)building its toolbar,
    // BEFORE READY; wxNote's toolbar already exists by now, so the bridge opens the window right after
    // READY instead (documented divergence - see the README's compatibility notes). It fires on EVERY
    // OS: off-Windows the ADDTOOLBARICON* answers are no-op TRUE, but plugins' init code that hangs off
    // this notification must still run.
    notifyNpp(NPPN_TBMODIFICATION);
}

static void deactivate(NibHost*)
{
    notifyNpp(NPPN_SHUTDOWN);   // let plugins persist state / release resources while still loaded
#ifdef _WIN32
    // RemoveWindowSubclass here relies on the host calling deactivate()/unload OUTSIDE the frame's own
    // WM_CLOSE/WM_DESTROY dispatch (see the host's onCloseWindow: it defers unloadNibPlugins() via
    // CallAfter). Calling this reentrantly - from a handler still nested inside that same window's
    // message dispatch, since this proc IS subclassed onto it - only DEFERS the removal (documented
    // comctl32 behaviour), and if FreeLibrary then runs before the deferred removal finalizes, a
    // later WM_DESTROY/WM_NCDESTROY on the same call chain jumps straight into the now-unmapped DLL.
    if (g_npp._nppHandle) ::RemoveWindowSubclass(g_npp._nppHandle, bridge_frame_proc, 1);   // unhook before we unload
#endif
    // The loaded N++ plugin modules + any docked windows are reclaimed at process exit.
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxn.npp-bridge", activate, deactivate
};

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;
    return &PLUGIN;
}
