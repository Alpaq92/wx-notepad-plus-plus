#pragma once
#include "menu_model.h"
#include "menu_labels_selection.h"
#include "menu_data_edit.h"   // reuses kEditMultiselectAllItems/kEditMultiselectNextItems/kEditOnSelectionItems verbatim
#include "command_ids.h"

// ------------------------------------------------------------- Selection
// Phase B reshape: the selection/multi-cursor cluster carved out of the old Edit menu (see
// docs/ plan - "Selection: Edit's selection/multi-cursor items"). Every item below is the exact
// same IDM_*/label/child-array as it was under Edit; only which top-level menu owns it changed.

static const MenuItemDef kSelectionMenuItems[] = {
    { MenuItemKind::Normal,  IDM_EDIT_SELECTALL,                 &Label::EditSelectAll,               "selection.selectAll" },
    { MenuItemKind::Normal,  IDM_EDIT_BEGINENDSELECT,            &Label::EditBeginEndSelect,           "selection.beginEndSelect" },
    { MenuItemKind::Normal,  IDM_EDIT_BEGINENDSELECT_COLUMNMODE, &Label::EditBeginEndSelectColumnMode, "selection.beginEndSelectColumnMode" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectAll, "selection.multiselectAll",
      kEditMultiselectAllItems, WXSIZEOF(kEditMultiselectAllItems) },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectNext, "selection.multiselectNext",
      kEditMultiselectNextItems, WXSIZEOF(kEditMultiselectNextItems) },
    { MenuItemKind::Normal,  IDM_EDIT_MULTISELECTUNDO,  &Label::EditMultiselectUndo, "selection.multiselectUndo" },
    { MenuItemKind::Normal,  IDM_EDIT_MULTISELECTSSKIP, &Label::EditMultiselectSkip, "selection.multiselectSkip" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_EDIT_CHAR_PANEL,             &Label::EditCharPanel,             "selection.charPanel" },
    { MenuItemKind::Normal,  IDM_EDIT_CLIPBOARDHISTORY_PANEL, &Label::EditClipboardHistoryPanel, "selection.clipboardHistoryPanel" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditOnSelection, "selection.onSelection",
      kEditOnSelectionItems, WXSIZEOF(kEditOnSelectionItems) },
};

static const MenuDef kSelectionMenu = { "menu.selection", &Label::MenuSelection, kSelectionMenuItems, WXSIZEOF(kSelectionMenuItems) };
