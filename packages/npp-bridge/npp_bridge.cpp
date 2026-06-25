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
        for (int i = 0; i < count; ++i) {
            if (!funcs[i]._itemName[0]) continue;   // separator
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

// The active document's full path (wide), via nib.documents; empty if untitled or unavailable.
static std::wstring activePathW()
{
    if (!g_docs || !g_host) return {};
    char buf[2048];
    int n = g_docs->active_path(g_host, buf, static_cast<int>(sizeof(buf)));
    if (n <= 0) return {};
    int w = ::MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
    std::wstring s(w > 0 ? w - 1 : 0, L'\0');
    if (w > 1) ::MultiByteToWideChar(CP_UTF8, 0, buf, -1, &s[0], w);
    return s;
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
        case NPPM_GETCURRENTSCINTILLA: if (lParam) *reinterpret_cast<int*>(lParam) = 0; out = TRUE; return true;   // main view
        case NPPM_GETCURRENTLANGTYPE:  if (lParam) *reinterpret_cast<int*>(lParam) = 0; out = TRUE; return true;   // L_TEXT
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
        events->subscribe(host, NIB_EV_TEXT_CHANGED,      on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_SELECTION_CHANGED, on_nib_event, nullptr);
        events->subscribe(host, NIB_EV_DOCUMENT_SAVED,    on_nib_event, nullptr);
    }
}

static const NibPluginApi PLUGIN = {
    NIB_ABI_VERSION, sizeof(NibPluginApi), "org.wxnpp.npp-bridge", activate, /*deactivate*/ nullptr
};

extern "C" NIB_API const NibPluginApi* nib_plugin_main(const NibBootstrap* boot)
{
    if (!boot || (boot->abi_version >> 16) != (NIB_ABI_VERSION >> 16)) return nullptr;
    return &PLUGIN;
}
