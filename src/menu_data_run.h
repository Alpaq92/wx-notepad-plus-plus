#pragma once
#include "menu_model.h"
#include "menu_labels_run.h"
#include "command_ids.h"

static const MenuItemDef kRunMenuItems[] = {
    { MenuItemKind::Normal, IDM_EXECUTE,                       &Label::RunExecute,              "run.execute" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EXECUTE_VALIDATE_SHORTCUTSXML, &Label::RunValidateShortcutsXml,  "run.validateShortcutsXml" },
};

static const MenuDef kRunMenu = { "menu.run", &Label::MenuRun, kRunMenuItems, WXSIZEOF(kRunMenuItems) };
