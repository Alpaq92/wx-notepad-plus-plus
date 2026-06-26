// SPDX-License-Identifier: GPL-3.0-or-later
//
// npp-bridge - the optional Notepad++ binary-plugin compatibility bridge for wxNotepad++.
//
// It is a Nib plugin (Windows-only) that reaches the host's native handles through the nib.win32
// capability and rebuilds the Notepad++ NppData environment, so real Notepad++ plugin DLLs can be
// hosted. This module is GPL because it reproduces Notepad++'s plugin ABI; the wxNotepad++ core does
// NOT depend on it - it is loaded, like any plugin, only if present. Keeping it separate + GPL is what
// lets the core relicense permissively (see docs/FUTURE_PLANS.md).
//
// STAGE 2: load the real N++ plugin DLLs (LoadLibrary), hand them the rebuilt NppData, and surface each
// FuncItem as a Nib command. The core's own loader is disabled, so the bridge is the sole N++ loader.
// (NPPM_*/SCI routing is still served transitionally by the core's frame subclass; Stage 3 moves it.)
#include "nib.h"
#include "PluginInterface.h"   // NppData, FuncItem, PFUNC* - the Notepad++ ABI this bridge targets
#include "Docking.h"           // tTbData / DWS_DF_* / CONT_* - the N++ docking-registration ABI
#include <windows.h>
#include <commctrl.h>          // SetWindowSubclass / DefSubclassProc - the bridge owns the NPPM_* router
#include <string>
#include <vector>
#include <cstdio>

static NppData g_npp = {};      // the Notepad++ environment, rebuilt from nib.win32
static NibHost*               g_host  = nullptr;  // stashed at activate so the NPPM router can reach Nib interfaces
static const NibDocumentsApi* g_docs  = nullptr;  // nib.documents - serves the current-file-path NPPM family
static const NibWin32Api*     g_win32 = nullptr;  // nib.win32 - serves docking (NPPM_DMM*)

struct NppPlugin {              // a loaded N++ plugin (kept alive for its FuncItem pointers + Stage-3 notifications)
    HMODULE      lib;
    PBENOTIFIED  beNotified;
    PMESSAGEPROC messageProc;
    FuncItem*    funcs;
    int          count;
};
static std::vector<NppPlugin> g_plugins;

static std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring exeDir()
{
    wchar_t buf[MAX_PATH]; DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n); size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
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
        const std::wstring dll = pdir + L"\\" + sub + L"\\" + sub + L".dll";
        if (::GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        HMODULE lib = ::LoadLibraryW(dll.c_str());
        if (!lib) continue;
        auto setInfo  = reinterpret_cast<PFUNCSETINFO>(::GetProcAddress(lib, "setInfo"));
        auto getFuncs = reinterpret_cast<PFUNCGETFUNCSARRAY>(::GetProcAddress(lib, "getFuncsArray"));
        auto getName  = reinterpret_cast<PFUNCGETNAME>(::GetProcAddress(lib, "getName"));
        if (!setInfo || !getFuncs || !getName) { ::FreeLibrary(lib); continue; }   // not a real N++ plugin

        setInfo(g_npp);                          // hand the plugin the rebuilt NppData (frame + editor HWNDs)
        int count = 0;
        FuncItem* funcs = getFuncs(&count);      // the plugin's menu commands (array lives in the DLL)
        const std::string pname = toUtf8(getName());
        for (int i = 0; funcs && i < count; ++i) {
            if (!funcs[i]._itemName[0] || ::lstrcmpW(funcs[i]._itemName, L"-SEPARATOR-") == 0) continue;   // separator
            const std::string id    = "npp." + pname + "." + std::to_string(i);
            const std::string title = pname + ": " + toUtf8(funcs[i]._itemName);
            cmds->register_command(host, id.c_str(), title.c_str(), npp_cmd_thunk, &funcs[i]);
        }
        g_plugins.push_back({ lib,
            reinterpret_cast<PBENOTIFIED>(::GetProcAddress(lib, "beNotified")),
            reinterpret_cast<PMESSAGEPROC>(::GetProcAddress(lib, "messageProc")),
            funcs, count });
    } while (::FindNextFileW(hf, &fd));
    ::FindClose(hf);
}

// Stage 4: the bridge owns the NPPM_* router. It subclasses the frame (HWND from nib.win32) and serves
// the messages the loaded plugins send. The common ones (GETCURRENTSCINTILLA, version, menu, exe paths,
// MENUCOMMAND) are answered from the bridge's own resources; file-path / open-save / docking NPPM need
// richer Nib interfaces, so they are acknowledged-but-stubbed for now.
static HMENU g_pluginsMenu = nullptr;

// UTF-8 -> wide (the inverse of toUtf8); empty on empty/null input.
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
    if (lParam) ::lstrcpynW(reinterpret_cast<wchar_t*>(lParam), s.c_str(), wParam ? static_cast<int>(wParam) : MAX_PATH);
    return static_cast<LRESULT>(s.size());
}

static bool bridge_handleNppm(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& out)
{
    switch (msg) {
        case NPPM_GETCURRENTSCINTILLA: if (lParam) *reinterpret_cast<int*>(lParam) = activeView(); out = TRUE; return true;   // 0=main, 1=sub
        case NPPM_GETCURRENTLANGTYPE:  if (lParam) *reinterpret_cast<int*>(lParam) = langTypeForPath(activePathW()); out = TRUE; return true;
        case NPPM_GETNPPVERSION:       out = MAKELONG(96, 8); return true;
        case NPPM_GETNBOPENFILES:      out = g_docs ? g_docs->count(g_host) : 1; return true;
        case NPPM_GETMENUHANDLE:       out = reinterpret_cast<LRESULT>(g_pluginsMenu); return true;
        case NPPM_MENUCOMMAND:         ::SendMessageW(g_npp._nppHandle, WM_COMMAND, static_cast<WPARAM>(lParam), 0); out = TRUE; return true;
        case NPPM_GETNPPDIRECTORY:     if (lParam) ::lstrcpynW(reinterpret_cast<wchar_t*>(lParam), exeDir().c_str(), MAX_PATH); out = TRUE; return true;
        case NPPM_GETPLUGINSCONFIGDIR: if (lParam) ::lstrcpynW(reinterpret_cast<wchar_t*>(lParam), (exeDir() + L"\\plugins\\Config").c_str(), MAX_PATH); out = TRUE; return true;
        // the active document's path family, served from nib.documents
        case NPPM_GETFULLCURRENTPATH:  out = putPath(wParam, lParam, activePathW()); return true;
        case NPPM_GETCURRENTDIRECTORY: { const std::wstring p = activePathW(); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, s == std::wstring::npos ? L"" : p.substr(0, s)); return true; }
        case NPPM_GETFILENAME:         { const std::wstring p = activePathW(); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, s == std::wstring::npos ? p : p.substr(s + 1)); return true; }
        case NPPM_GETNAMEPART:         { std::wstring f = activePathW(); const size_t s = f.find_last_of(L"\\/"); if (s != std::wstring::npos) f = f.substr(s + 1); const size_t d = f.find_last_of(L'.'); out = putPath(wParam, lParam, d == std::wstring::npos ? f : f.substr(0, d)); return true; }
        case NPPM_GETEXTPART:          { const std::wstring p = activePathW(); const size_t d = p.find_last_of(L'.'); const size_t s = p.find_last_of(L"\\/"); out = putPath(wParam, lParam, (d != std::wstring::npos && (s == std::wstring::npos || d > s)) ? p.substr(d) : L""); return true; }
        case NPPM_DOOPEN:              if (g_docs && lParam) g_docs->open(g_host, toUtf8(reinterpret_cast<const wchar_t*>(lParam)).c_str()); out = TRUE; return true;
        case NPPM_SAVECURRENTFILE:     out = (g_docs && g_docs->save_active(g_host)) ? TRUE : FALSE; return true;
        // docking: the plugin owns a native window and asks us to host it in a dock pane
        case NPPM_DMMREGASDCKDLG:      if (g_win32 && lParam) {
                                           const tTbData* d = reinterpret_cast<const tTbData*>(lParam);
                                           int edge = 0;   // bottom
                                           switch ((d->uMask >> 28) & 0x0F) { case CONT_LEFT: edge = 1; break; case CONT_RIGHT: edge = 2; break; case CONT_TOP: edge = 3; break; default: edge = 0; }
                                           if (d->hClient) g_win32->dock_native(g_host, d->hClient, toUtf8(d->pszName).c_str(), edge);
                                       } out = TRUE; return true;
        case NPPM_DMMSHOW:             if (g_win32 && lParam) g_win32->show_dock(g_host, reinterpret_cast<void*>(lParam), 1); out = TRUE; return true;
        case NPPM_DMMHIDE:             if (g_win32 && lParam) g_win32->show_dock(g_host, reinterpret_cast<void*>(lParam), 0); out = TRUE; return true;
        case NPPM_DMMUPDATEDISPINFO:   if (lParam) ::InvalidateRect(reinterpret_cast<HWND>(lParam), nullptr, TRUE); out = TRUE; return true;
        // ---- additional coverage, served from existing nib.* + the frame's native children ----
        case NPPM_SWITCHTOFILE:        if (g_docs && lParam) g_docs->open(g_host, toUtf8(reinterpret_cast<const wchar_t*>(lParam)).c_str()); out = TRUE; return true;   // open-or-switch
        case NPPM_GETCURRENTVIEW:      out = activeView(); return true;   // 0=main, 1=sub
        case NPPM_GETBUFFERLANGTYPE:   out = langTypeForPath(pathFromIdW(static_cast<intptr_t>(wParam))); return true;   // by file extension
        case NPPM_GETPLUGINHOMEPATH:   if (lParam) ::lstrcpynW(reinterpret_cast<wchar_t*>(lParam), (exeDir() + L"\\plugins").c_str(), wParam ? static_cast<int>(wParam) : MAX_PATH); out = TRUE; return true;
        case NPPM_SETSTATUSBAR:        { HWND sb = ::FindWindowExW(g_npp._nppHandle, nullptr, L"msctls_statusbar32", nullptr);   // the wx status bar is a native msctls_statusbar32
                                         if (sb && lParam) ::SendMessageW(sb, SB_SETTEXTW, static_cast<WPARAM>(wParam), lParam); out = TRUE; return true; }
        // per-buffer tracking, via nib.documents v2 (the host's EditorPage* is the opaque buffer id)
        case NPPM_GETCURRENTBUFFERID:      out = (g_docs && g_docs->version >= 2 && g_docs->active_id) ? g_docs->active_id(g_host) : 0; return true;
        case NPPM_GETFULLPATHFROMBUFFERID: out = putPath(0, lParam, pathFromIdW(static_cast<intptr_t>(wParam))); return true;
        default:                       return false;   // not one of ours -> fall through to DefSubclassProc
    }
}

static LRESULT CALLBACK bridge_frame_proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (msg >= (WM_USER + 1000)) {   // NPPM_ (NPPMSG = WM_USER+1000) + the RUNCOMMAND_USER (WM_USER+3000) family
        LRESULT out = 0;
        if (bridge_handleNppm(msg, w, l, out)) return out;
    }
    return ::DefSubclassProc(h, msg, w, l);
}

static void activate(NibHost* host, NibQueryFn query)
{
    g_host = host;
    g_docs = static_cast<const NibDocumentsApi*>(query(host, NIB_IFACE_DOCUMENTS, 1));   // current-file-path NPPM
    const NibWin32Api* w = static_cast<const NibWin32Api*>(query(host, NIB_IFACE_WIN32, 1));
    g_win32 = w;   // docking (NPPM_DMM*) routes through nib.win32
    if (w) {
        g_npp._nppHandle             = static_cast<HWND>(w->main_window(host));
        g_npp._scintillaMainHandle   = static_cast<HWND>(w->editor_main(host));
        g_npp._scintillaSecondHandle = static_cast<HWND>(w->editor_second(host));
        g_pluginsMenu = static_cast<HMENU>(w->plugins_menu(host));
        if (g_npp._nppHandle) ::SetWindowSubclass(g_npp._nppHandle, bridge_frame_proc, 1, 0);   // own the NPPM_* router
    }
    const NibCommandsApi* cmds = static_cast<const NibCommandsApi*>(query(host, NIB_IFACE_COMMANDS, 1));
    if (!cmds) return;
    cmds->register_command(host, "org.wxnpp.npp-bridge.info", "N++ Bridge: NppData Info", cmd_info, nullptr);
    if (w) loadNppPlugins(host, cmds);

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
    if (g_npp._nppHandle) ::RemoveWindowSubclass(g_npp._nppHandle, bridge_frame_proc, 1);   // unhook before we unload
    // The loaded N++ plugin DLLs + any docked windows are reclaimed at process exit.
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnpp.npp-bridge", activate, deactivate
};

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;
    return &PLUGIN;
}
