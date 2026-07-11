#pragma once
#include "menu_model.h"
#include "menu_labels_macro.h"
#include "command_ids.h"

static const MenuItemDef kMacroMenuItems[] = {
    { MenuItemKind::Normal, IDM_MACRO_STARTRECORDINGMACRO,   &Label::MacroStartRecording, "macro.startRecording" },
    { MenuItemKind::Normal, IDM_MACRO_STOPRECORDINGMACRO,    &Label::MacroStopRecording,  "macro.stopRecording" },
    { MenuItemKind::Normal, IDM_MACRO_PLAYBACKRECORDEDMACRO, &Label::MacroPlayback,       "macro.playback" },
    { MenuItemKind::Normal, IDM_MACRO_SAVECURRENTMACRO,      &Label::MacroSaveCurrent,    "macro.saveCurrent" },
    { MenuItemKind::Normal, IDM_MACRO_RUNMULTIMACRODLG,      &Label::MacroRunMultiTimes,  "macro.runMultiTimes" },
};

static const MenuDef kMacroMenu = { "menu.macro", &Label::MenuMacro, kMacroMenuItems, WXSIZEOF(kMacroMenuItems) };
