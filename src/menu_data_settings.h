#pragma once
#include "menu_model.h"
#include "menu_labels_settings.h"
#include "menuCmdID.h"

// ----------------------------------------------------------------- Settings
// Settings is a standalone top-level menu again (post-Phase-B revision - Preferences under "Window"
// was a weird home per user feedback). It owns the config-type items (Preferences, Style Configurator,
// Shortcut Mapper, Import, Edit Popup ContextMenu) and the dynamically-inserted Localization submenu;
// the old Window menu keeps only genuine window management (Sort By / Windows / Recent Window).
//
// "slot.localization" is a DynamicSlot: the Localization submenu is built at runtime from the app's
// UI-language list (main.cpp's UI_LANG_IDS/uiLangName), so main.cpp's buildMenuBar() resolves this slot
// after the tree walk via reg.slot("slot.localization") - the resolution is by symbolic name, so which
// top-level menu the slot lives under (it moved from Window to here) needs no main.cpp change.
static const MenuItemDef kSettingsImportItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_IMPORTPLUGIN,      &Label::SettingsImportPlugins,       "settings.import.plugins" },
    { MenuItemKind::Normal, IDM_SETTING_IMPORTSTYLETHEMES, &Label::SettingsImportStyleThemes,   "settings.import.styleThemes" },
};

static const MenuItemDef kSettingsMenuItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_PREFERENCE,      &Label::SettingsPreferences,       "settings.preferences" },
    { MenuItemKind::Normal, IDM_LANGSTYLE_CONFIG_DLG,    &Label::SettingsStyleConfigurator, "settings.styleConfigurator" },
    { MenuItemKind::Normal, IDM_SETTING_SHORTCUT_MAPPER, &Label::SettingsShortcutMapper,    "settings.shortcutMapper" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SettingsImportSubmenu, "settings.import",
      kSettingsImportItems, WXSIZEOF(kSettingsImportItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_SETTING_EDITCONTEXTMENU, &Label::SettingsEditContextMenu, "settings.editContextMenu" },
    { MenuItemKind::Separator },
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.localization" },
};

static const MenuDef kSettingsMenu = { "menu.settings", &Label::MenuSettings, kSettingsMenuItems, WXSIZEOF(kSettingsMenuItems) };
