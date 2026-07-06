#pragma once
#include "menu_model.h"
#include "menu_labels_window.h"
#include "menuCmdID.h"

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
    { MenuItemKind::Submenu, 0, &Label::WindowSortBy, "window.sortBy", kWindowSortByItems, WXSIZEOF(kWindowSortByItems) },
    { MenuItemKind::Normal, IDM_WINDOW_WINDOWS, &Label::WindowWindows, "window.windows" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_WINDOW_MRU_FIRST, &Label::WindowRecentWindow, "window.recentWindow",
      nullptr, 0, /*initiallyDisabled=*/true },
};

static const MenuDef kWindowMenu = { "menu.window", &Label::MenuWindow, kWindowMenuItems, WXSIZEOF(kWindowMenuItems) };
