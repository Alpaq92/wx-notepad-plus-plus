#pragma once
#include "menu_model.h"
#include "menu_labels_window.h"
#include "menu_labels_settings.h"   // reuses Label::Settings* verbatim - see below
#include "menu_data_settings.h"     // reuses kSettingsImportItems verbatim
#include "menuCmdID.h"

// ----------------------------------------------------------------- Window
// Phase B reshape: Window absorbs Settings and the dynamically-inserted Localization menu (see the
// reshape plan - "Window: repurposed as the app-wide 'environment' menu"). Settings' items and the
// old Window menu's items each keep their own exact wording/ids/nesting; only the grouping changed.
//
// "slot.localization" is a DynamicSlot: the Localization submenu is built at runtime from the app's
// UI-language list (main.cpp's UI_LANG_IDS/uiLangName), not static data, so main.cpp's buildMenuBar()
// resolves this slot after the tree walk - exactly the same pattern already used for
// "slot.recentFiles" in the File menu, just now inserting a submenu instead of appending in-place.

static const MenuItemDef kWindowSortByItems[] = {
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FN_ASC, &Label::WindowSortNameAsc,  "window.sortBy.nameAsc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FN_DSC, &Label::WindowSortNameDesc, "window.sortBy.nameDesc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FP_ASC, &Label::WindowSortPathAsc,  "window.sortBy.pathAsc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FP_DSC, &Label::WindowSortPathDesc, "window.sortBy.pathDesc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FT_ASC, &Label::WindowSortTypeAsc,  "window.sortBy.typeAsc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FT_DSC, &Label::WindowSortTypeDesc, "window.sortBy.typeDesc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FS_ASC, &Label::WindowSortSizeAsc,  "window.sortBy.sizeAsc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FS_DSC, &Label::WindowSortSizeDesc, "window.sortBy.sizeDesc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FD_ASC, &Label::WindowSortModifiedAsc,  "window.sortBy.modifiedAsc" },
    { MenuItemKind::Normal, IDM_WINDOW_SORT_FD_DSC, &Label::WindowSortModifiedDesc, "window.sortBy.modifiedDesc" },
};

static const MenuItemDef kWindowMenuItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_PREFERENCE,      &Label::SettingsPreferences,       "window.preferences" },
    { MenuItemKind::Normal, IDM_LANGSTYLE_CONFIG_DLG,    &Label::SettingsStyleConfigurator, "window.styleConfigurator" },
    { MenuItemKind::Normal, IDM_SETTING_SHORTCUT_MAPPER, &Label::SettingsShortcutMapper,    "window.shortcutMapper" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SettingsImportSubmenu, "window.import",
      kSettingsImportItems, WXSIZEOF(kSettingsImportItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_SETTING_EDITCONTEXTMENU, &Label::SettingsEditContextMenu, "window.editContextMenu" },
    { MenuItemKind::Separator },
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.localization" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::WindowSortBy, "window.sortBy", kWindowSortByItems, WXSIZEOF(kWindowSortByItems) },
    { MenuItemKind::Normal, IDM_WINDOW_WINDOWS, &Label::WindowWindows, "window.windows" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_WINDOW_MRU_FIRST, &Label::WindowRecentWindow, "window.recentWindow",
      nullptr, 0, /*initiallyDisabled=*/true },
};

static const MenuDef kWindowMenu = { "menu.window", &Label::MenuWindow, kWindowMenuItems, WXSIZEOF(kWindowMenuItems) };
