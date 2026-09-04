#pragma once
#include "menu_model.h"
#include "menu_labels_plugins.h"
#include "command_ids.h"

static const MenuItemDef kPluginsMenuItems[] = {
    // kCmdSettingPluginadm is Notepad++'s own Plugins Admin id, already in the frozen id table - so the
    // bridge forwards a plugin's request for it to the same dialog a user reaches from this menu.
    { MenuItemKind::Normal, kCmdSettingPluginadm,      &Label::PluginsManage,     "plugins.manage" },
    { MenuItemKind::Normal, kCmdSettingOpenPluginsDir, &Label::PluginsOpenFolder, "plugins.openFolder" },
};

static const MenuDef kExtensionsMenu = { "menu.extensions", &Label::MenuExtensions, kPluginsMenuItems, WXSIZEOF(kPluginsMenuItems) };
