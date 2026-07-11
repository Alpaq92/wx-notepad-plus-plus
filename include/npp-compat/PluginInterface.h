// SPDX-License-Identifier: Apache-2.0
//
// wxNote - plugin ABI: the plugin entry-point interface.
// Copyright 2026 The wxNote Authors.
//
// Clean-room, cross-platform redeclaration of the Notepad++ plugin entry interface.
// The struct field layouts, function signatures, and exported symbol names below are
// reproduced exactly as binary interoperability requires (a plugin compiled against
// Notepad++'s headers must see the same layout and call the same names) - those are
// functional facts, not creative expression. The wording, ordering, comments, and the
// cross-platform handling are original to wxNote; no Notepad++ source text is
// copied. Public reference: https://npp-user-manual.org/docs/plugin-communication/

#pragma once

#include "npp_plugin_port.h"    // portable Win32 type vocabulary (HWND, LRESULT, NPP_CDECL, NPP_EXPORT, ...)
#include "Scintilla.h"          // SCNotification (Scintilla - permissive HPND license)
#include "Notepad_plus_msgs.h"  // NPPM_* / NPPN_* message ids and their payload structs

// The three handles the host passes to a plugin at startup: the host's main window
// and its two editor views (primary / secondary).
struct NppData
{
    HWND _nppHandle             = nullptr;
    HWND _scintillaMainHandle   = nullptr;
    HWND _scintillaSecondHandle = nullptr;
};

// Plugin callback / query function-pointer types.
typedef const wchar_t* (NPP_CDECL* PFUNCGETNAME)();
typedef void           (NPP_CDECL* PFUNCSETINFO)(NppData);
typedef void           (NPP_CDECL* PFUNCPLUGINCMD)();
typedef void           (NPP_CDECL* PBENOTIFIED)(SCNotification*);
typedef LRESULT        (NPP_CDECL* PMESSAGEPROC)(UINT message, WPARAM wParam, LPARAM lParam);

// An optional accelerator a plugin command may register.
struct ShortcutKey
{
    bool  _isCtrl  = false;
    bool  _isAlt   = false;
    bool  _isShift = false;
    UCHAR _key     = 0;
};

// Maximum length, in wide characters, of a plugin menu-item label.
constexpr int menuItemSize = 64;

// One plugin menu command, as returned in the array from getFuncsArray().
struct FuncItem
{
    wchar_t        _itemName[menuItemSize] = { L'\0' };
    PFUNCPLUGINCMD _pFunc                  = nullptr;
    int            _cmdID                  = 0;
    bool           _init2Check             = false;
    ShortcutKey*   _pShKey                 = nullptr;
};

typedef FuncItem* (NPP_CDECL* PFUNCGETFUNCSARRAY)(int*);

// The symbols every plugin shared library exports; the host resolves them by name on load.
extern "C"
{
    NPP_EXPORT void           setInfo(NppData);
    NPP_EXPORT const wchar_t* getName();
    NPP_EXPORT FuncItem*      getFuncsArray(int*);
    NPP_EXPORT void           beNotified(SCNotification*);
    NPP_EXPORT LRESULT        messageProc(UINT message, WPARAM wParam, LPARAM lParam);
    NPP_EXPORT BOOL           isUnicode();   // retained for ABI; wxNote buffers are Unicode
}
