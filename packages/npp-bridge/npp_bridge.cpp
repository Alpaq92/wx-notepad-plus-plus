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
#endif

static NppData g_npp = {};      // the Notepad++ environment, rebuilt from nib.win32 (Win) / opaque tokens (POSIX)
static NibHost*               g_host  = nullptr;  // stashed at activate so the NPPM router can reach Nib interfaces
static const NibDocumentsApi* g_docs  = nullptr;  // nib.documents - serves the current-file-path NPPM family
static const NibWin32Api*     g_win32 = nullptr;  // nib.win32 - serves docking (NPPM_DMM*); NULL off-Windows
static const NibSciApi*       g_sci   = nullptr;  // nib.sci - the portable SCI_* passthrough (every OS)
static const NibPathsApi*     g_paths = nullptr;  // nib.paths - user-data dir; the off-Windows plugins base

struct NppPlugin {              // a loaded N++ plugin (kept alive for its FuncItem pointers + Stage-3 notifications)
    DllHandle    lib;
    PBENOTIFIED  beNotified;
    PMESSAGEPROC messageProc;
    FuncItem*    funcs;
    int          count;
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
            scn.nmhdr.code = SCN_SAVEPOINTREACHED;
            break;
        case NIB_EV_DOCUMENT_ACTIVATED:
            scn.nmhdr.hwndFrom = g_npp._nppHandle;   // an NPPN_ notification comes from the frame, not the editor
            scn.nmhdr.code     = NPPN_BUFFERACTIVATED;
            scn.nmhdr.idFrom   = static_cast<uptr_t>(ev->as.document.id);   // the now-active buffer id
            break;
        default: return;
    }
    for (const auto& p : g_plugins) if (p.beNotified) p.beNotified(&scn);
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
// Windows, where the plugin uses the OS ::SendMessage). Returns false + closes lib if it's not a plugin.
static bool loadOneNppModule(NibHost* host, const NibCommandsApi* cmds, DllHandle lib,
                             const char* diagName, const NppHostBridge* bindTarget)
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
    registerFuncItems(host, cmds, toUtf8(getName()), funcs, count);
    g_plugins.push_back({ lib,
        reinterpret_cast<PBENOTIFIED> (dllSym(lib, "beNotified")),
        reinterpret_cast<PMESSAGEPROC>(dllSym(lib, "messageProc")),
        funcs, count });
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
        loadOneNppModule(host, cmds, lib, nullptr, nullptr);   // Windows plugins use the OS ::SendMessage (no shim)
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

static bool bridge_handleNppm(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& out)
{
    switch (msg) {
        case NPPM_GETCURRENTSCINTILLA: if (lParam) *reinterpret_cast<int*>(lParam) = activeView(); out = TRUE; return true;   // 0=main, 1=sub
        case NPPM_GETCURRENTLANGTYPE:  if (lParam) *reinterpret_cast<int*>(lParam) = langTypeForPath(activePathW()); out = TRUE; return true;
        case NPPM_GETNPPVERSION:       out = MAKELONG(96, 8); return true;
        case NPPM_GETNBOPENFILES:      out = g_docs ? g_docs->count(g_host) : 1; return true;
        case NPPM_GETMENUHANDLE:
#ifdef _WIN32
            out = reinterpret_cast<LRESULT>(g_pluginsMenu);
#else
            out = 0;   // no native menu handle off-Windows
#endif
            return true;
        case NPPM_MENUCOMMAND:
#ifdef _WIN32
            ::SendMessageW(g_npp._nppHandle, WM_COMMAND, static_cast<WPARAM>(lParam), 0);
#else
            (void)lParam;   // off-Windows N++ menu commands route via the Nib command table (future), not WM_COMMAND
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
        loadOneNppModule(host, cmds, lib, name.c_str(), &g_hostBridge);
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

    // Forward editor events to the loaded plugins' beNotified (Stage 3).
    const NibEventsApi* events = static_cast<const NibEventsApi*>(query(host, NIB_IFACE_EVENTS, 1));
    if (events) {
        events->subscribe(host, NIB_EV_TEXT_CHANGED,       on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_SELECTION_CHANGED,  on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_DOCUMENT_SAVED,     on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_DOCUMENT_ACTIVATED, on_nib_event, nullptr);   // -> NPPN_BUFFERACTIVATED
    }
}

static void deactivate(NibHost*)
{
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
