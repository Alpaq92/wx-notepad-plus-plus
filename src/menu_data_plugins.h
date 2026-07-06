#pragma once
#include "menu_model.h"
#include "menu_labels_plugins.h"
#include "menuCmdID.h"

static const MenuItemDef kPluginsMenuItems[] = {
    { MenuItemKind::Normal, IDM_SETTING_OPENPLUGINSDIR, &Label::PluginsOpenFolder, "plugins.openFolder" },
};

static const MenuDef kPluginsMenu = { "menu.plugins", &Label::MenuPlugins, kPluginsMenuItems, WXSIZEOF(kPluginsMenuItems) };
