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
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>

static NppData g_npp = {};      // the Notepad++ environment, rebuilt from nib.win32

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

static void activate(NibHost* host, NibQueryFn query)
{
    const NibWin32Api* w = static_cast<const NibWin32Api*>(query(host, NIB_IFACE_WIN32, 1));
    if (w) {
        g_npp._nppHandle             = static_cast<HWND>(w->main_window(host));
        g_npp._scintillaMainHandle   = static_cast<HWND>(w->editor_main(host));
        g_npp._scintillaSecondHandle = static_cast<HWND>(w->editor_second(host));
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
