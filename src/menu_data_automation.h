#pragma once
#include "menu_model.h"
#include "menu_labels_automation.h"
#include "menu_data_macro.h"   // reuses kMacroMenuItems verbatim
#include "menu_data_run.h"     // reuses kRunMenuItems verbatim
#include "menu_data_tools.h"   // reuses kToolsMd5/Sha1/Sha256/Sha512Items verbatim
#include "command_ids.h"

// ------------------------------------------------------------- Automation
// wxNote's "Automation" menu gathers everything a user reaches for to make the editor act on its
// own. It is ordered by everyday frequency: the two interactive submenus lead - Macro first
// (record/replay is the most-used automation), then Run - followed by a separator and the passive
// hash-generation "Tools" cluster. The four hash submenus are listed strongest-first
// (sha256, sha512, sha1, md5) so the recommended digests sit at the top and the legacy ones trail.

static const MenuItemDef kAutomationMenuItems[] = {
    { MenuItemKind::Submenu, 0, &Label::MenuMacro, "menu.macro", kMacroMenuItems, WXSIZEOF(kMacroMenuItems) },
    { MenuItemKind::Submenu, 0, &Label::MenuRun,   "menu.run",   kRunMenuItems,   WXSIZEOF(kRunMenuItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha256, "tools.sha256", kToolsSha256Items, WXSIZEOF(kToolsSha256Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha512, "tools.sha512", kToolsSha512Items, WXSIZEOF(kToolsSha512Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha1,   "tools.sha1",   kToolsSha1Items,   WXSIZEOF(kToolsSha1Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsMd5,    "tools.md5",    kToolsMd5Items,    WXSIZEOF(kToolsMd5Items) },
};

static const MenuDef kAutomationMenu = { "menu.automation", &Label::MenuAutomation, kAutomationMenuItems, WXSIZEOF(kAutomationMenuItems) };
