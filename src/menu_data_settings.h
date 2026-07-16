#pragma once
#include "menu_model.h"
#include "menu_labels_settings.h"
#include "command_ids.h"

// ----------------------------------------------------------------- Settings
// wxNote arranges this menu by configuration scope, most-frequent first. Preferences is by far the
// most-used entry, so it stands alone at the top as the headline. Beneath it, the two configurator
// dialogs (Style Configurator, then Shortcut Mapper) form their own cluster. The less-frequent
// maintenance items follow, each in its own separator group - the Import submenu, then Edit Popup
// ContextMenu - with the dynamic Localization submenu last. These config-type items live here rather
// than under the "Window" menu, which is reserved for genuine window management.
//
// "slot.localization" is a DynamicSlot: the Localization submenu is built at runtime from the app's
// UI-language list (main.cpp's UI_LANG_IDS/uiLangName), so buildMenuBar() resolves this slot after the
// tree walk via reg.slot("slot.localization") - resolution is by symbolic name, so the top-level menu
// the slot lives under needs no main.cpp change.
static const MenuItemDef kSettingsImportItems[] = {
    { MenuItemKind::Normal, kCmdSettingImportPlugin,      &Label::SettingsImportPlugins,       "settings.import.plugins" },
    { MenuItemKind::Normal, kCmdSettingImportStyleThemes, &Label::SettingsImportStyleThemes,   "settings.import.styleThemes" },
};

static const MenuItemDef kSettingsMenuItems[] = {
    { MenuItemKind::Normal, kCmdSettingPreference,      &Label::SettingsPreferences,       "settings.preferences" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdLangstyleConfigDlg,    &Label::SettingsStyleConfigurator, "settings.styleConfigurator" },
    { MenuItemKind::Normal, kCmdSettingShortcutMapper, &Label::SettingsShortcutMapper,    "settings.shortcutMapper" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SettingsImportSubmenu, "settings.import",
      kSettingsImportItems, WXSIZEOF(kSettingsImportItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdSettingEditContextMenu, &Label::SettingsEditContextMenu, "settings.editContextMenu" },
    { MenuItemKind::Separator },
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.localization" },
};

static const MenuDef kSettingsMenu = { "menu.settings", &Label::MenuSettings, kSettingsMenuItems, WXSIZEOF(kSettingsMenuItems) };
