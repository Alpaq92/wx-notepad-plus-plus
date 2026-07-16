// SPDX-License-Identifier: GPL-3.0-or-later
//
// wxNote - cross-platform recompiled-plugin TEMPLATE (Phase 1 proof).
// Copyright 2026 The wxNote Authors.
//
// This is a minimal, compile-only Notepad++ plugin written the way a real N++
// plugin is written: it includes the N++ ABI header (PluginInterface.h), exports
// the six entry points the host resolves by name, and talks to the host and the
// editor using ONLY NPPM_* / SCI_* messages sent through ::SendMessage(). It uses
// no Win32 UI, no .rc resources, no HWND subclassing - i.e. the "recompile nearly
// unchanged" tier of docs/ARCHITECTURE.md.
//
// Its purpose is a LINK-ONLY PROOF: built off-Windows and linked against
// libnpp_shim, the plugin's ::SendMessage / ::SendMessageW calls resolve to the
// shim's exported forwarders (there is no Win32 to provide them), demonstrating
// that the six-symbol contract + the SendMessage seam close with no unresolved
// symbols. It is NOT run - there is no runtime N++ plugin loader off-Windows yet
// (that is later-phase work); this target only has to compile and link cleanly on
// the Linux/macOS CI runners.

#include "PluginInterface.h"   // NppData, FuncItem, the six extern "C" exports; pulls in npp_plugin_port.h + Scintilla.h + Notepad_plus_msgs.h

#include <cwchar>   // std::wcscpy (menu-label copy); on libstdc++/libc++ it is NOT pulled in by <cstring>

#ifndef _WIN32
// On Windows the author's ::SendMessage comes from <windows.h> (pulled in by
// npp_plugin_port.h). Off-Windows there is no <windows.h>, so the prototype of the
// forwarder that libnpp_shim exports must be brought into scope for the unchanged
// ::SendMessage(hwnd, NPPM_*/SCI_*, w, l) calls below to type-check and to resolve to
// the shim at link time. The signature matches shim/npp_shim.cpp exactly. (A future
// SDK revision may hoist this declaration into the shim/port headers so a recompiled
// plugin source needs zero edits; the template declares it locally to stay
// self-contained and to make the six-symbol + SendMessage contract explicit.)
extern "C" LRESULT SendMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace {

// The three host handles, delivered once by the host via setInfo(). Off-Windows
// these are opaque tokens the bridge hands out; the plugin only ever passes them
// back into ::SendMessage() - it never dereferences them.
NppData g_npp{};

// The plugin's menu commands, returned verbatim from getFuncsArray().
FuncItem g_funcs[3] = {};

// Maintained from the host's file lifecycle notifications (NPPN_FILEOPENED / NPPN_FILECLOSED),
// demonstrating that a recompiled plugin receives the same beNotified stream on every OS.
int g_openFileCount = 0;

// --- the actual work: pure NPPM_*/SCI_* message passing, zero platform code ----

// Resolve the currently-active editor view handle from the host, exactly as a real
// plugin does. Returns the main or the secondary Scintilla token.
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
    // Read the selection length, then replace the selection with a decorated copy.
    const LRESULT selLen = ::SendMessage(sci, SCI_GETSELTEXT, 0, 0);
    if (selLen <= 0)
        return;
    // A real plugin would fetch the text, transform it, and SCI_REPLACESEL it back;
    // here we just bracket it to keep the template short. SCI_REPLACESEL takes a
    // NUL-terminated string in lParam.
    const char marker[] = "<<wrapped>>";
    ::SendMessage(sci, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(marker));
}

// Command 2: ask the host for its version - a pure NPPM_* round-trip.
void showHostVersion()
{
    const LRESULT ver = ::SendMessage(g_npp._nppHandle, NPPM_GETNPPVERSION, 0, 0);
    (void)ver;   // a UI-free plugin would surface this; the template just proves the call links.
}

// Command 3: enumerate every open file's path - the exact pattern a file-list / session plugin uses:
// ask NPPM_GETNBOPENFILES for the count, allocate the array N++ fills, then NPPM_GETOPENFILENAMES.
// UI-free: it just performs the round-trip (a real plugin would display or persist the names).
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
    // names[0..nb-1] now hold each open document's path; a real plugin would use them here.
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

// The host hands the plugin its three window/view handles here, once, at load.
NPP_EXPORT void setInfo(NppData data)
{
    g_npp = data;
}

// The plugin's display name in the Plugins/Extensions menu.
NPP_EXPORT const wchar_t* getName()
{
    return L"wxNote Cross-Platform Example";
}

// Return the command table; the host reads *count entries out of it.
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

// Editor/host notifications. Switch on scn->nmhdr.code, exactly as a real plugin does - here we track
// the open-document count from the file lifecycle notifications to demonstrate that the same NPPN_*
// stream reaches a recompiled plugin on every OS (the SCN_* editor notifications are ignored).
NPP_EXPORT void beNotified(SCNotification* scn)
{
    if (!scn)
        return;
    switch (scn->nmhdr.code) {
        case NPPN_READY:       g_openFileCount = 0; break;   // host is up; safe to query the environment
        case NPPN_FILEOPENED:  ++g_openFileCount; break;
        case NPPN_FILECLOSED:  if (g_openFileCount > 0) --g_openFileCount; break;
        default:               break;
    }
}

// Direct host->plugin messages. Not used by this template (Phase 2 territory).
NPP_EXPORT LRESULT messageProc(UINT /*message*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    return TRUE;
}

} // extern "C"
