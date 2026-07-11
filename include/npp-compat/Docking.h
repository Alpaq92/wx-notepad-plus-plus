// SPDX-License-Identifier: Apache-2.0
//
// wxNote - plugin ABI: dockable-panel registration.
// Copyright 2026 The wxNote Authors.
//
// Clean-room, cross-platform redeclaration of the structures and flags a plugin uses
// to register a dockable panel (sent via NPPM_DMMREGASDCKDLG). Layouts and constant
// values are reproduced as binary interoperability requires; wording and the
// cross-platform handling are original. No Notepad++ source text is copied.

#pragma once

#include "npp_plugin_port.h"

// Caption placement for a docking container.
#define CAPTION_TOP    TRUE
#define CAPTION_BOTTOM FALSE

// Docking-container positions.
#define CONT_LEFT    0
#define CONT_RIGHT   1
#define CONT_TOP     2
#define CONT_BOTTOM  3
#define DOCKCONT_MAX 4

// Bits for DockedWidgetData::uMask (which optional fields are populated).
#define DWS_ICONTAB        0x00000001  // a tab icon is supplied
#define DWS_ICONBAR        0x00000002  // a bar icon is supplied (legacy)
#define DWS_ADDINFO        0x00000004  // additional-info string is supplied
#define DWS_USEOWNDARKMODE 0x00000008  // the plugin paints its own dark mode
#define DWS_PARAMSALL      (DWS_ICONTAB | DWS_ICONBAR | DWS_ADDINFO)

// Default placement requested on the first registration (encoded in the high nibble).
#define DWS_DF_CONT_LEFT   (CONT_LEFT   << 28)
#define DWS_DF_CONT_RIGHT  (CONT_RIGHT  << 28)
#define DWS_DF_CONT_TOP    (CONT_TOP    << 28)
#define DWS_DF_CONT_BOTTOM (CONT_BOTTOM << 28)
#define DWS_DF_FLOATING    0x80000000

// Registration payload for a dockable plugin panel. Historically named "tTbData";
// both names are kept so existing plugin sources compile unchanged.
typedef struct DockedWidgetData
{
    HWND           hClient       = nullptr;  // the plugin's panel window
    const wchar_t* pszName       = nullptr;  // display name (tab / caption)
    int            dlgID         = 0;        // the FuncItem command id that toggles this panel
    UINT           uMask         = 0;        // DWS_* flags above
    HICON          hIconTab      = nullptr;  // optional tab icon
    const wchar_t* pszAddInfo    = nullptr;  // optional additional-info text
    RECT           rcFloat       = {};       // host-managed: floating rectangle
    int            iPrevCont     = 0;        // host-managed: previous container
    const wchar_t* pszModuleName = nullptr;  // the plugin's module file name
} DockedWidgetData, tTbData;

// Host-side docking-manager handle + the docked regions (passed to plugins where needed).
struct tDockMgr
{
    HWND hWnd                    = nullptr;
    RECT rcRegion[DOCKCONT_MAX]  = {};
};

#define HIT_TEST_THICKNESS 20
#define SPLITTER_WIDTH      4
