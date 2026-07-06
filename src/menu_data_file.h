#pragma once
#include "menu_model.h"
#include "menu_labels_file.h"
#include "menuCmdID.h"

// ----------------------------------------------------------------- File
// Mechanical, zero-behavior-change port of the File menu from the old
// inline buildNppMainMenu() (see src/npp_menu.h). Same items, same order,
// same IDM_* ids, same labels, same shortcuts.

static const MenuItemDef kFileOpenContainingFolderItems[] = {
    { MenuItemKind::Normal, IDM_FILE_OPEN_FOLDER,                 &Label::FileOpenExplorer,      "file.openContainingFolder.explorer" },
    { MenuItemKind::Normal, IDM_FILE_OPEN_CMD,                    &Label::FileOpenCmd,           "file.openContainingFolder.cmd" },
    { MenuItemKind::Normal, IDM_FILE_OPEN_POWERSHELL,             &Label::FileOpenPowerShell,    "file.openContainingFolder.powerShell" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_FILE_CONTAININGFOLDERASWORKSPACE, &Label::FileFolderAsWorkspace, "file.openContainingFolder.folderAsWorkspace" },
};

static const MenuItemDef kFileCloseMultipleDocumentsItems[] = {
    { MenuItemKind::Normal, IDM_FILE_CLOSEALL_BUT_CURRENT, &Label::FileCloseAllButCurrent, "file.closeMultipleDocuments.butCurrent" },
    { MenuItemKind::Normal, IDM_FILE_CLOSEALL_BUT_PINNED,  &Label::FileCloseAllButPinned,  "file.closeMultipleDocuments.butPinned" },
    { MenuItemKind::Normal, IDM_FILE_CLOSEALL_TOLEFT,      &Label::FileCloseAllToLeft,     "file.closeMultipleDocuments.toLeft" },
    { MenuItemKind::Normal, IDM_FILE_CLOSEALL_TORIGHT,     &Label::FileCloseAllToRight,    "file.closeMultipleDocuments.toRight" },
    { MenuItemKind::Normal, IDM_FILE_CLOSEALL_UNCHANGED,   &Label::FileCloseAllUnchanged,  "file.closeMultipleDocuments.unchanged" },
};

static const MenuItemDef kFileMenuItems[] = {
    { MenuItemKind::Normal,  IDM_FILE_NEW,                   &Label::FileNew,                   "file.new" },
    { MenuItemKind::Normal,  IDM_FILE_OPEN,                  &Label::FileOpen,                  "file.open" },
    { MenuItemKind::Submenu, 0, &Label::FileOpenContainingFolder, "file.openContainingFolder",
      kFileOpenContainingFolderItems, WXSIZEOF(kFileOpenContainingFolderItems) },
    { MenuItemKind::Normal,  IDM_FILE_OPEN_DEFAULT_VIEWER,   &Label::FileOpenDefaultViewer,     "file.openDefaultViewer" },
    { MenuItemKind::Normal,  IDM_FILE_OPENFOLDERASWORKSPACE, &Label::FileOpenFolderAsWorkspace, "file.openFolderAsWorkspace" },
    { MenuItemKind::Normal,  IDM_FILE_RELOAD,                &Label::FileReload,                "file.reload" },
    { MenuItemKind::Normal,  IDM_FILE_SAVE,                  &Label::FileSave,                  "file.save" },
    { MenuItemKind::Normal,  IDM_FILE_SAVEAS,                &Label::FileSaveAs,                "file.saveAs" },
    { MenuItemKind::Normal,  IDM_FILE_SAVECOPYAS,            &Label::FileSaveCopyAs,            "file.saveCopyAs" },
    { MenuItemKind::Normal,  IDM_FILE_SAVEALL,               &Label::FileSaveAll,               "file.saveAll" },
    { MenuItemKind::Normal,  IDM_FILE_RENAME,                &Label::FileRename,                "file.rename" },
    { MenuItemKind::Normal,  IDM_FILE_CLOSE,                 &Label::FileClose,                 "file.close" },
    { MenuItemKind::Normal,  IDM_FILE_CLOSEALL,              &Label::FileCloseAll,              "file.closeAll" },
    { MenuItemKind::Submenu, 0, &Label::FileCloseMultipleDocuments, "file.closeMultipleDocuments",
      kFileCloseMultipleDocumentsItems, WXSIZEOF(kFileCloseMultipleDocumentsItems) },
    { MenuItemKind::Normal,  IDM_FILE_RESTORELASTCLOSEDFILE, &Label::FileRestoreLastClosed,     "file.restoreLastClosed" },
    { MenuItemKind::Normal,  IDM_FILE_DELETE,                &Label::FileDelete,                "file.delete" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_FILE_LOADSESSION,           &Label::FileLoadSession,           "file.loadSession" },
    { MenuItemKind::Normal,  IDM_FILE_SAVESESSION,           &Label::FileSaveSession,           "file.saveSession" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_FILE_PRINT,                 &Label::FilePrint,                 "file.print" },
    { MenuItemKind::Normal,  IDM_FILE_PRINTNOW,              &Label::FilePrintNow,              "file.printNow" },
    { MenuItemKind::Separator },
    // The "Recent Files" submenu (wxFileHistory) is inserted here at runtime by buildMenuBar().
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.recentFiles" },
    { MenuItemKind::Normal,  IDM_FILE_EXIT,                  &Label::FileExit,                  "file.exit" },
};

static const MenuDef kFileMenu = { "menu.file", &Label::MenuFile, kFileMenuItems, WXSIZEOF(kFileMenuItems) };
