#pragma once
#include "menu_model.h"
#include "menu_labels_help.h"
#include "command_ids.h"

// wxNote Help menu grouping (wxNote's own arrangement):
//   Web-resource links lead, since visiting the project online is the most
//   common reason to open Help: GitHub, Releases, Documentation, Report Issue.
//   Then update-checking on its own, then the diagnostics cluster
//   (Command-line Arguments + Debug Info) for troubleshooting, and About last
//   per the CUA convention of parking the identity item at the bottom.
static const MenuItemDef kHelpMenuItems[] = {
    { MenuItemKind::Normal, kCmdHomeSweetHome,   &Label::HelpGitHub,        "help.gitHub" },
    { MenuItemKind::Normal, kCmdProjectpage,     &Label::HelpReleases,      "help.releases" },
    { MenuItemKind::Normal, kCmdOnlineDocument,  &Label::HelpDocumentation, "help.documentation" },
    { MenuItemKind::Normal, kCmdForum,           &Label::HelpReportIssue,   "help.reportIssue" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdUpdateNpp,      &Label::HelpCheckForUpdates, "help.checkForUpdates" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdCmdLineArguments, &Label::HelpCmdLineArguments, "help.cmdLineArguments" },
    { MenuItemKind::Normal, kCmdDebuginfo,       &Label::HelpDebugInfo, "help.debugInfo" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdAboutBase,           &Label::HelpAbout,     "help.about", nullptr, 0, false, "F1" },
};

static const MenuDef kHelpMenu = { "menu.help", &Label::MenuHelp, kHelpMenuItems, WXSIZEOF(kHelpMenuItems) };
