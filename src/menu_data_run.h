#pragma once
#include "menu_model.h"
#include "menu_labels_run.h"
#include "command_ids.h"

// wxNote Run menu: the everyday action (Execute an external command) leads;
// the niche maintenance action (validate the shortcuts XML) is separated below
// so power-user tooling never crowds the primary control.
static const MenuItemDef kRunMenuItems[] = {
    { MenuItemKind::Normal, kCmdExecuteBase,                       &Label::RunExecute,              "run.execute" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdExecuteValidateShortcutsXml, &Label::RunValidateShortcutsXml,  "run.validateShortcutsXml" },
};

static const MenuDef kRunMenu = { "menu.run", &Label::MenuRun, kRunMenuItems, WXSIZEOF(kRunMenuItems) };
