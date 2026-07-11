#pragma once
#include "menu_model.h"
#include "menu_labels_help.h"
#include "command_ids.h"

static const MenuItemDef kHelpMenuItems[] = {
    { MenuItemKind::Normal, IDM_CMDLINEARGUMENTS, &Label::HelpCmdLineArguments, "help.cmdLineArguments" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_HOMESWEETHOME,   &Label::HelpGitHub,        "help.gitHub" },
    { MenuItemKind::Normal, IDM_PROJECTPAGE,     &Label::HelpReleases,      "help.releases" },
    { MenuItemKind::Normal, IDM_ONLINEDOCUMENT,  &Label::HelpDocumentation, "help.documentation" },
    { MenuItemKind::Normal, IDM_FORUM,           &Label::HelpReportIssue,   "help.reportIssue" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_UPDATE_NPP,      &Label::HelpCheckForUpdates, "help.checkForUpdates" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_DEBUGINFO,       &Label::HelpDebugInfo, "help.debugInfo" },
    { MenuItemKind::Normal, IDM_ABOUT,           &Label::HelpAbout,     "help.about" },
};

static const MenuDef kHelpMenu = { "menu.help", &Label::MenuHelp, kHelpMenuItems, WXSIZEOF(kHelpMenuItems) };
