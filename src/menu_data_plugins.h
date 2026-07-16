#pragma once
#include "menu_model.h"
#include "menu_labels_plugins.h"
#include "command_ids.h"

static const MenuItemDef kPluginsMenuItems[] = {
    { MenuItemKind::Normal, kCmdSettingOpenPluginsDir, &Label::PluginsOpenFolder, "plugins.openFolder" },
};

static const MenuDef kExtensionsMenu = { "menu.extensions", &Label::MenuExtensions, kPluginsMenuItems, WXSIZEOF(kPluginsMenuItems) };
