#pragma once
#include "menu_model.h"
#include "menu_labels_window.h"
#include "command_ids.h"

// ----------------------------------------------------------------- Window
// Window is genuine window-management only. wxNote's grouping: the first cluster is live control of
// the currently open documents -- Sort By (an affinity-paired asc/desc list, one pair per attribute:
// name, path, type, size, modified) leads because it shapes the Windows list, and Windows... opens
// that list. A separator then isolates the second cluster: Recent Window, the MRU for reopening a
// document you closed. Config-type items (Preferences, Style Configurator, Shortcut Mapper, Import,
// Edit Popup ContextMenu) and Localization live in the top-level Settings menu (menu_data_settings.h),
// not here -- "Window" is for managing windows, not application configuration.

static const MenuItemDef kWindowSortByItems[] = {
    { MenuItemKind::Normal, kCmdWindowSortFnAsc, &Label::WindowSortNameAsc,  "window.sortBy.nameAsc" },
    { MenuItemKind::Normal, kCmdWindowSortFnDsc, &Label::WindowSortNameDesc, "window.sortBy.nameDesc" },
    { MenuItemKind::Normal, kCmdWindowSortFpAsc, &Label::WindowSortPathAsc,  "window.sortBy.pathAsc" },
    { MenuItemKind::Normal, kCmdWindowSortFpDsc, &Label::WindowSortPathDesc, "window.sortBy.pathDesc" },
    { MenuItemKind::Normal, kCmdWindowSortFtAsc, &Label::WindowSortTypeAsc,  "window.sortBy.typeAsc" },
    { MenuItemKind::Normal, kCmdWindowSortFtDsc, &Label::WindowSortTypeDesc, "window.sortBy.typeDesc" },
    { MenuItemKind::Normal, kCmdWindowSortFsAsc, &Label::WindowSortSizeAsc,  "window.sortBy.sizeAsc" },
    { MenuItemKind::Normal, kCmdWindowSortFsDsc, &Label::WindowSortSizeDesc, "window.sortBy.sizeDesc" },
    { MenuItemKind::Normal, kCmdWindowSortFdAsc, &Label::WindowSortModifiedAsc,  "window.sortBy.modifiedAsc" },
    { MenuItemKind::Normal, kCmdWindowSortFdDsc, &Label::WindowSortModifiedDesc, "window.sortBy.modifiedDesc" },
};

static const MenuItemDef kWindowMenuItems[] = {
    { MenuItemKind::Submenu, 0, &Label::WindowSortBy, "window.sortBy", kWindowSortByItems, WXSIZEOF(kWindowSortByItems) },
    { MenuItemKind::Normal, kCmdWindowWindows, &Label::WindowWindows, "window.windows" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdWindowMruFirst, &Label::WindowRecentWindow, "window.recentWindow",
      nullptr, 0, /*initiallyDisabled=*/true },
};

static const MenuDef kWindowMenu = { "menu.window", &Label::MenuWindow, kWindowMenuItems, WXSIZEOF(kWindowMenuItems) };
