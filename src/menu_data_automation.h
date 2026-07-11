#pragma once
#include "menu_model.h"
#include "menu_labels_automation.h"
#include "menu_data_macro.h"   // reuses kMacroMenuItems verbatim
#include "menu_data_run.h"     // reuses kRunMenuItems verbatim
#include "menu_data_tools.h"   // reuses kToolsMd5/Sha1/Sha256/Sha512Items verbatim
#include "command_ids.h"

// ------------------------------------------------------------- Automation
// Phase B reshape: Macro, Run, and Tools (hash generation) - three near-empty top-level menus -
// merged into one "Automation" menu (see the reshape plan). Macro and Run each keep their own
// submenu shape unchanged; Tools' old top-level menu had no items of its own besides the 4 hash
// submenus, so those lift directly into Automation instead of nesting behind a redundant
// intermediate "Tools" submenu.

static const MenuItemDef kAutomationMenuItems[] = {
    { MenuItemKind::Submenu, 0, &Label::MenuMacro, "menu.macro", kMacroMenuItems, WXSIZEOF(kMacroMenuItems) },
    { MenuItemKind::Submenu, 0, &Label::MenuRun,   "menu.run",   kRunMenuItems,   WXSIZEOF(kRunMenuItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::ToolsMd5,    "tools.md5",    kToolsMd5Items,    WXSIZEOF(kToolsMd5Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha1,   "tools.sha1",   kToolsSha1Items,   WXSIZEOF(kToolsSha1Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha256, "tools.sha256", kToolsSha256Items, WXSIZEOF(kToolsSha256Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha512, "tools.sha512", kToolsSha512Items, WXSIZEOF(kToolsSha512Items) },
};

static const MenuDef kAutomationMenu = { "menu.automation", &Label::MenuAutomation, kAutomationMenuItems, WXSIZEOF(kAutomationMenuItems) };
