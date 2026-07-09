#pragma once
#include "menu_model.h"
#include "menu_labels_settings.h"
#include "menuCmdID.h"

// Phase B reshape: Settings is no longer a standalone top-level menu - its items moved directly
// into the new Window menu's own table (see menu_data_window.h). Only this Import submenu's item
// array is still shared/reused verbatim from there.
static const MenuItemDef kSettingsImportItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_IMPORTPLUGIN,      &Label::SettingsImportPlugins,       "settings.import.plugins" },
    { MenuItemKind::Normal, IDM_SETTING_IMPORTSTYLETHEMES, &Label::SettingsImportStyleThemes,   "settings.import.styleThemes" },
};
