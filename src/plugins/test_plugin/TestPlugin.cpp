// Minimal Notepad++ plugin used to verify the wx shell's Win32 plugin loader end-to-end.
// It implements the real plugin ABI (PluginInterface.h): the host calls setInfo() with the
// NppData HWNDs, getFuncsArray() to populate the Plugins menu, and beNotified() for SCN_* events.
// "Insert Hello" talks to the editor exactly like a real plugin does - SendMessage(SCI_*) to the
// Scintilla HWND - which is the path that keeps working after the wxStyledTextCtrl port.

#include <windows.h>
#include <cstdio>
#include "PluginInterface.h"
#include "Docking.h"
#include "Scintilla.h"

static NppData  g_npp{};
static FuncItem g_funcs[6]{};
static int      g_notifyCount = 0;
static HWND     g_dock = nullptr;
static bool     g_dockReg = false;

static void cmdInsertHello()
{
    // The text reports the live SCN_ notification count, so this one command verifies both the
    // command dispatch AND that beNotified() is being forwarded to us.
    char buf[160];
    ::sprintf_s(buf, 160, "Hello from TestPlugin! (received %d SCN_ notifications via beNotified)", g_notifyCount);
    if (g_npp._scintillaMainHandle)
        ::SendMessageA(g_npp._scintillaMainHandle, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(buf));
}

static void cmdNppInfo()
{
    // Query the host via real NPPM_* messages (to the Notepad++ HWND) and insert what comes back.
    wchar_t path[1024] = {0}, nppdir[1024] = {0}, cfg[1024] = {0};
    ::SendMessageW(g_npp._nppHandle, NPPM_GETFULLCURRENTPATH, 1024, reinterpret_cast<LPARAM>(path));
    ::SendMessageW(g_npp._nppHandle, NPPM_GETNPPDIRECTORY,    1024, reinterpret_cast<LPARAM>(nppdir));
    ::SendMessageW(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 1024, reinterpret_cast<LPARAM>(cfg));
    char buf[4096];
    ::sprintf_s(buf, 4096, "NPPM_GETFULLCURRENTPATH: %ls | NPPM_GETNPPDIRECTORY: %ls | NPPM_GETPLUGINSCONFIGDIR: %ls", path, nppdir, cfg);
    if (g_npp._scintillaMainHandle)
        ::SendMessageA(g_npp._scintillaMainHandle, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(buf));
}

static void cmdOpenViaNppm()
{
    // Drive the host through a real NPPM_* command: ask Notepad++ to open a file.
    ::SendMessageW(g_npp._nppHandle, NPPM_DOOPEN, 0,
                   reinterpret_cast<LPARAM>(L"C:\\Users\\Alpaq\\AppData\\Local\\Temp\\sample.py"));
}

static void cmdShowDock()
{
    // A real NppExec-style flow: the plugin owns its window and asks the host to dock it.
    if (!g_dock)
        g_dock = ::CreateWindowExW(0, L"STATIC",
            L"  This panel is TestPlugin's OWN window,\r\n  docked by the wx host via NPPM_DMMREGASDCKDLG.\r\n\r\n  (NppExec-class plugins put their console here.)",
            WS_CHILD | SS_LEFT, 0, 0, 200, 120, g_npp._nppHandle, nullptr, nullptr, nullptr);
    if (!g_dockReg)
    {
        tTbData data{};
        data.hClient       = g_dock;
        data.pszName       = L"TestPlugin Panel";
        data.uMask         = DWS_DF_CONT_BOTTOM;   // default-dock at the bottom
        data.pszModuleName = L"TestPlugin.dll";
        ::SendMessageW(g_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
        g_dockReg = true;
    }
    ::SendMessageW(g_npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_dock));
}

static void cmdSaveViaNppm()
{
    // Append a marker straight to the editor, then ask the host to persist the file via NPPM_SAVECURRENTFILE.
    const char* mark = "\n// saved via NPPM_SAVECURRENTFILE\n";
    ::SendMessageA(g_npp._scintillaMainHandle, SCI_APPENDTEXT, ::lstrlenA(mark), reinterpret_cast<LPARAM>(mark));
    ::SendMessageW(g_npp._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
}

extern "C" __declspec(dllexport) void setInfo(NppData d) { g_npp = d; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"TestPlugin"; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* n)
{
    ::wcscpy_s(g_funcs[0]._itemName, menuItemSize, L"Insert Hello");
    g_funcs[0]._pFunc = cmdInsertHello;
    ::wcscpy_s(g_funcs[1]._itemName, menuItemSize, L"Insert NPP Info (NPPM_*)");
    g_funcs[1]._pFunc = cmdNppInfo;
    ::wcscpy_s(g_funcs[2]._itemName, menuItemSize, L"Open sample.py (NPPM_DOOPEN)");
    g_funcs[2]._pFunc = cmdOpenViaNppm;
    ::wcscpy_s(g_funcs[3]._itemName, menuItemSize, L"Show Dock Panel (NPPM_DMM*)");
    g_funcs[3]._pFunc = cmdShowDock;
    ::wcscpy_s(g_funcs[4]._itemName, menuItemSize, L"Append + Save (NPPM_SAVECURRENTFILE)");
    g_funcs[4]._pFunc = cmdSaveViaNppm;
    *n = 5;
    return g_funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification*) { ++g_notifyCount; }
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
