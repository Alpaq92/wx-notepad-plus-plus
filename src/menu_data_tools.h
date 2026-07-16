#pragma once
#include "menu_model.h"
#include "menu_labels_tools.h"
#include "command_ids.h"

static const MenuItemDef kToolsMd5Items[] = {
    { MenuItemKind::Normal, kCmdToolMd5Generate,               &Label::ToolsGenerate,             "tools.md5.generate" },
    { MenuItemKind::Normal, kCmdToolMd5GenerateFromFile,       &Label::ToolsGenerateFromFile,      "tools.md5.generateFromFile" },
    { MenuItemKind::Normal, kCmdToolMd5GenerateIntoClipboard,  &Label::ToolsGenerateIntoClipboard, "tools.md5.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha1Items[] = {
    { MenuItemKind::Normal, kCmdToolSha1Generate,               &Label::ToolsGenerate,             "tools.sha1.generate" },
    { MenuItemKind::Normal, kCmdToolSha1GenerateFromFile,       &Label::ToolsGenerateFromFile,      "tools.sha1.generateFromFile" },
    { MenuItemKind::Normal, kCmdToolSha1GenerateIntoClipboard,  &Label::ToolsGenerateIntoClipboard, "tools.sha1.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha256Items[] = {
    { MenuItemKind::Normal, kCmdToolSha256Generate,               &Label::ToolsGenerate,             "tools.sha256.generate" },
    { MenuItemKind::Normal, kCmdToolSha256GenerateFromFile,       &Label::ToolsGenerateFromFile,      "tools.sha256.generateFromFile" },
    { MenuItemKind::Normal, kCmdToolSha256GenerateIntoClipboard,  &Label::ToolsGenerateIntoClipboard, "tools.sha256.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha512Items[] = {
    { MenuItemKind::Normal, kCmdToolSha512Generate,               &Label::ToolsGenerate,             "tools.sha512.generate" },
    { MenuItemKind::Normal, kCmdToolSha512GenerateFromFile,       &Label::ToolsGenerateFromFile,      "tools.sha512.generateFromFile" },
    { MenuItemKind::Normal, kCmdToolSha512GenerateIntoClipboard,  &Label::ToolsGenerateIntoClipboard, "tools.sha512.generateIntoClipboard" },
};

// Hash generators for the Automation menu. wxNote surfaces these strongest-first:
// the presentation order (SHA-256, SHA-512, SHA-1, MD5) is set where these arrays are
// listed in menu_data_automation.h, leading with the algorithms wxNote recommends today
// and demoting the legacy digests. Array-definition order here is irrelevant to that
// ordering - it only supplies the four submenus' contents. Each submenu keeps the
// canonical generate / from-file / into-clipboard sequence untouched.
