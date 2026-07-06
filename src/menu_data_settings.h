#pragma once
#include "menu_model.h"
#include "menu_labels_settings.h"
#include "menuCmdID.h"

static const MenuItemDef kSettingsImportItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_IMPORTPLUGIN,      &Label::SettingsImportPlugins,       "settings.import.plugins" },
    { MenuItemKind::Normal, IDM_SETTING_IMPORTSTYLETHEMES, &Label::SettingsImportStyleThemes,   "settings.import.styleThemes" },
};

static const MenuItemDef kSettingsMenuItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_PREFERENCE,       &Label::SettingsPreferences,       "settings.preferences" },
    { MenuItemKind::Normal, IDM_LANGSTYLE_CONFIG_DLG,     &Label::SettingsStyleConfigurator, "settings.styleConfigurator" },
    { MenuItemKind::Normal, IDM_SETTING_SHORTCUT_MAPPER,  &Label::SettingsShortcutMapper,    "settings.shortcutMapper" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SettingsImportSubmenu, "settings.import",
      kSettingsImportItems, WXSIZEOF(kSettingsImportItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_SETTING_EDITCONTEXTMENU, &Label::SettingsEditContextMenu, "settings.editContextMenu" },
};

static const MenuDef kSettingsMenu = { "menu.settings", &Label::MenuSettings, kSettingsMenuItems, WXSIZEOF(kSettingsMenuItems) };
