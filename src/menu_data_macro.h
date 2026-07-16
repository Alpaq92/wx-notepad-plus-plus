#pragma once
#include "menu_model.h"
#include "menu_labels_macro.h"
#include "command_ids.h"

// wxNote Macro menu: items follow the record-then-reuse workflow a user
// actually walks through — capture (Start, Stop), then replay (Playback),
// then persist and scale (Save Current, Run Multiple Times). Ordered by that
// lifecycle rather than by any external menu's layout.
static const MenuItemDef kMacroMenuItems[] = {
    { MenuItemKind::Normal, kCmdMacroStartRecordingMacro,   &Label::MacroStartRecording, "macro.startRecording" },
    { MenuItemKind::Normal, kCmdMacroStopRecordingMacro,    &Label::MacroStopRecording,  "macro.stopRecording" },
    { MenuItemKind::Normal, kCmdMacroPlaybackRecordedMacro, &Label::MacroPlayback,       "macro.playback" },
    { MenuItemKind::Normal, kCmdMacroSaveCurrentMacro,      &Label::MacroSaveCurrent,    "macro.saveCurrent" },
    { MenuItemKind::Normal, kCmdMacroRunMultiMacroDlg,      &Label::MacroRunMultiTimes,  "macro.runMultiTimes" },
};

static const MenuDef kMacroMenu = { "menu.macro", &Label::MenuMacro, kMacroMenuItems, WXSIZEOF(kMacroMenuItems) };
