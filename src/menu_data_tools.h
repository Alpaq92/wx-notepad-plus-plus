#pragma once
#include "menu_model.h"
#include "menu_labels_tools.h"
#include "command_ids.h"

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

// Phase B reshape: Tools is no longer a standalone top-level menu - it had no items of its own
// besides these 4 hash submenus, which now lift directly into the new Automation menu (see
// menu_data_automation.h) instead of nesting behind a now-redundant "Tools" wrapper.
