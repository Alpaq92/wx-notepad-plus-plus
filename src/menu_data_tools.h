#pragma once
#include "menu_model.h"
#include "menu_labels_tools.h"
#include "menuCmdID.h"

static const MenuItemDef kToolsMd5Items[] = {
    { MenuItemKind::Normal, IDM_TOOL_MD5_GENERATE,               &Label::ToolsGenerate,             "tools.md5.generate" },
    { MenuItemKind::Normal, IDM_TOOL_MD5_GENERATEFROMFILE,       &Label::ToolsGenerateFromFile,      "tools.md5.generateFromFile" },
    { MenuItemKind::Normal, IDM_TOOL_MD5_GENERATEINTOCLIPBOARD,  &Label::ToolsGenerateIntoClipboard, "tools.md5.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha1Items[] = {
    { MenuItemKind::Normal, IDM_TOOL_SHA1_GENERATE,               &Label::ToolsGenerate,             "tools.sha1.generate" },
    { MenuItemKind::Normal, IDM_TOOL_SHA1_GENERATEFROMFILE,       &Label::ToolsGenerateFromFile,      "tools.sha1.generateFromFile" },
    { MenuItemKind::Normal, IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD,  &Label::ToolsGenerateIntoClipboard, "tools.sha1.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha256Items[] = {
    { MenuItemKind::Normal, IDM_TOOL_SHA256_GENERATE,               &Label::ToolsGenerate,             "tools.sha256.generate" },
    { MenuItemKind::Normal, IDM_TOOL_SHA256_GENERATEFROMFILE,       &Label::ToolsGenerateFromFile,      "tools.sha256.generateFromFile" },
    { MenuItemKind::Normal, IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD,  &Label::ToolsGenerateIntoClipboard, "tools.sha256.generateIntoClipboard" },
};
static const MenuItemDef kToolsSha512Items[] = {
    { MenuItemKind::Normal, IDM_TOOL_SHA512_GENERATE,               &Label::ToolsGenerate,             "tools.sha512.generate" },
    { MenuItemKind::Normal, IDM_TOOL_SHA512_GENERATEFROMFILE,       &Label::ToolsGenerateFromFile,      "tools.sha512.generateFromFile" },
    { MenuItemKind::Normal, IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD,  &Label::ToolsGenerateIntoClipboard, "tools.sha512.generateIntoClipboard" },
};

static const MenuItemDef kToolsMenuItems[] = {
    { MenuItemKind::Submenu, 0, &Label::ToolsMd5,    "tools.md5",    kToolsMd5Items,    WXSIZEOF(kToolsMd5Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha1,   "tools.sha1",   kToolsSha1Items,   WXSIZEOF(kToolsSha1Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha256, "tools.sha256", kToolsSha256Items, WXSIZEOF(kToolsSha256Items) },
    { MenuItemKind::Submenu, 0, &Label::ToolsSha512, "tools.sha512", kToolsSha512Items, WXSIZEOF(kToolsSha512Items) },
};

static const MenuDef kToolsMenu = { "menu.tools", &Label::MenuTools, kToolsMenuItems, WXSIZEOF(kToolsMenuItems) };
