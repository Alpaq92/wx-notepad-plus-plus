#pragma once
#include "menu_model.h"
#include "menu_labels_selection.h"
#include "menu_data_edit.h"   // reuses kEditMultiselectAllItems/kEditMultiselectNextItems/kEditOnSelectionItems verbatim
#include "command_ids.h"

// ------------------------------------------------------------- Selection
// wxNote grouping rationale: ordered by everyday reach, not historical layout. The plain
// selection verbs (Select All, Begin/End Select, column-mode Begin/End) lead as the most-used
// cluster; the multi-cursor tools (both Multi-select submenus, then its Undo/Skip pair) follow;
// the side-panel toggles (Character Panel, Clipboard History) sit next; the On-Selection submenu
// trails last as the most situational entry. Separators mark those four intent clusters.

static const MenuItemDef kSelectionMenuItems[] = {
    { MenuItemKind::Normal,  kCmdEditSelectall,                 &Label::EditSelectAll,               "selection.selectAll", nullptr, 0, false, "Ctrl+A" },
    { MenuItemKind::Normal,  kCmdEditBeginEndSelect,            &Label::EditBeginEndSelect,           "selection.beginEndSelect" },
    { MenuItemKind::Normal,  kCmdEditBeginEndSelectColumnmode, &Label::EditBeginEndSelectColumnMode, "selection.beginEndSelectColumnMode" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectAll, "selection.multiselectAll",
      kEditMultiselectAllItems, WXSIZEOF(kEditMultiselectAllItems) },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectNext, "selection.multiselectNext",
      kEditMultiselectNextItems, WXSIZEOF(kEditMultiselectNextItems) },
    { MenuItemKind::Normal,  kCmdEditMultiSelectUndo,  &Label::EditMultiselectUndo, "selection.multiselectUndo" },
    { MenuItemKind::Normal,  kCmdEditMultiSelectSkip, &Label::EditMultiselectSkip, "selection.multiselectSkip" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  kCmdEditCharPanel,             &Label::EditCharPanel,             "selection.charPanel" },
    { MenuItemKind::Normal,  kCmdEditClipboardHistoryPanel, &Label::EditClipboardHistoryPanel, "selection.clipboardHistoryPanel" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditOnSelection, "selection.onSelection",
      kEditOnSelectionItems, WXSIZEOF(kEditOnSelectionItems) },
};

static const MenuDef kSelectionMenu = { "menu.selection", &Label::MenuSelection, kSelectionMenuItems, WXSIZEOF(kSelectionMenuItems) };
