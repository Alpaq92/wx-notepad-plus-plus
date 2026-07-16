#pragma once
#include "menu_model.h"
#include "menu_labels_file.h"
#include "command_ids.h"

// ----------------------------------------------------------------- File
// wxNote's File menu is organized by object lifecycle, most-frequent clusters first:
// OPEN (create/bring-in a document) leads, then SAVE, then CLOSE. A wxNote-distinct
// FILE-SYSTEM group (Rename, Delete) is pulled out on its own instead of being buried
// among the close actions, since acting on the file-on-disk is a different intent than
// closing a tab. Session, Print, and the recent-files + Exit tail follow as lower-frequency
// groups. Numbered/paired sequences keep their canonical CUA order (New before Open,
// Save before Save As, Load before Save session).

// Dynamically filled per what THIS machine actually has (see terminal_panel.h's detectWindowsExtra-
// OpenHereTools/detectLinuxTerminalEmulators/detectMacTerminalApps, populated in buildMenuBar() at the
// "slot.openContainingFolderTools" DynamicSlot below) - a fixed Explorer/cmd/PowerShell-shaped list
// made no sense cross-platform: cmd/PowerShell did nothing on Linux/macOS, and a single guessed
// "Terminal" entry ignored whatever terminal emulators (or pwsh/Cygwin/WSL) were actually installed.
static const MenuItemDef kFileOpenContainingFolderItems[] = {
#ifdef __WXMSW__
    // cmd/PowerShell stay STATIC with their original frozen ids: this is Windows, where real Notepad++
    // plugins exist and may invoke them via NPPM_MENUCOMMAND - every IDM_* must keep landing on a real
    // menu item. The DynamicSlot below adds only what's genuinely new to this machine (pwsh/Cygwin/WSL).
    { MenuItemKind::Normal, kCmdFileOpenFolder,     &Label::FileOpenExplorer,   "file.openContainingFolder.explorer" },
    { MenuItemKind::Normal, kCmdFileOpenCmd,        &Label::FileOpenCmd,       "file.openContainingFolder.cmd" },
    { MenuItemKind::Normal, kCmdFileOpenPowershell, &Label::FileOpenPowerShell,"file.openContainingFolder.powerShell" },
#else
    // Linux/macOS: no legacy plugin ABI to preserve (packages/npp-bridge is Windows-only), so the whole
    // terminal cluster is free to be built from what this machine actually has.
    { MenuItemKind::Normal, kCmdFileOpenFolder, &Label::FileOpenFileManager, "file.openContainingFolder.fileManager" },
#endif
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.openContainingFolderTools" },
    // No "Folder as Workspace" entry here - it would duplicate the top-level "Open Folder as
    // Workspace..." item below, which already pre-selects the current file's folder in its dialog
    // (see openFolderAsWorkspace()), so it covers both the "quick, current file" and "pick any folder"
    // cases with one menu item instead of two near-identical ones. kCmdFileContainingFolderAsWorkspace
    // itself is intentionally left wired in the dispatcher (containingFolderAsWorkspace()) even though
    // no menu item fires it anymore, so a plugin invoking the id via NPPM_MENUCOMMAND still works.
};

static const MenuItemDef kFileCloseMultipleDocumentsItems[] = {
    { MenuItemKind::Normal, kCmdFileCloseallButCurrent, &Label::FileCloseAllButCurrent, "file.closeMultipleDocuments.butCurrent" },
    { MenuItemKind::Normal, kCmdFileCloseallButPinned,  &Label::FileCloseAllButPinned,  "file.closeMultipleDocuments.butPinned" },
    { MenuItemKind::Normal, kCmdFileCloseallToleft,      &Label::FileCloseAllToLeft,     "file.closeMultipleDocuments.toLeft" },
    { MenuItemKind::Normal, kCmdFileCloseallToright,     &Label::FileCloseAllToRight,    "file.closeMultipleDocuments.toRight" },
    { MenuItemKind::Normal, kCmdFileCloseallUnchanged,   &Label::FileCloseAllUnchanged,  "file.closeMultipleDocuments.unchanged" },
};

static const MenuItemDef kFileMenuItems[] = {
    // OPEN group - create or bring a document into the editor.
    { MenuItemKind::Normal,  kCmdFileNew,                   &Label::FileNew,                   "file.new" },
    { MenuItemKind::Normal,  kCmdFileOpen,                  &Label::FileOpen,                  "file.open" },
    { MenuItemKind::Submenu, 0, &Label::FileOpenContainingFolder, "file.openContainingFolder",
      kFileOpenContainingFolderItems, WXSIZEOF(kFileOpenContainingFolderItems) },
    { MenuItemKind::Normal,  kCmdFileOpenDefaultViewer,   &Label::FileOpenDefaultViewer,     "file.openDefaultViewer" },
    { MenuItemKind::Normal,  kCmdFileOpenFolderAsWorkspace, &Label::FileOpenFolderAsWorkspace, "file.openFolderAsWorkspace" },
    { MenuItemKind::Normal,  kCmdFileReload,                &Label::FileReload,                "file.reload" },
    { MenuItemKind::Separator },
    // SAVE group.
    { MenuItemKind::Normal,  kCmdFileSave,                  &Label::FileSave,                  "file.save" },
    { MenuItemKind::Normal,  kCmdFileSaveas,                &Label::FileSaveAs,                "file.saveAs" },
    { MenuItemKind::Normal,  kCmdFileSavecopyas,            &Label::FileSaveCopyAs,            "file.saveCopyAs" },
    { MenuItemKind::Normal,  kCmdFileSaveall,               &Label::FileSaveAll,               "file.saveAll" },
    { MenuItemKind::Separator },
    // CLOSE group - dismiss open documents/tabs.
    { MenuItemKind::Normal,  kCmdFileClose,                 &Label::FileClose,                 "file.close" },
    { MenuItemKind::Normal,  kCmdFileCloseall,              &Label::FileCloseAll,              "file.closeAll" },
    { MenuItemKind::Submenu, 0, &Label::FileCloseMultipleDocuments, "file.closeMultipleDocuments",
      kFileCloseMultipleDocumentsItems, WXSIZEOF(kFileCloseMultipleDocumentsItems) },
    { MenuItemKind::Normal,  kCmdFileRestoreLastClosedFile, &Label::FileRestoreLastClosed,     "file.restoreLastClosed" },
    { MenuItemKind::Separator },
    // FILE-SYSTEM group - act on the file on disk, not the tab.
    { MenuItemKind::Normal,  kCmdFileRename,                &Label::FileRename,                "file.rename" },
    { MenuItemKind::Normal,  kCmdFileDelete,                &Label::FileDelete,                "file.delete" },
    { MenuItemKind::Separator },
    // Session.
    { MenuItemKind::Normal,  kCmdFileLoadsession,           &Label::FileLoadSession,           "file.loadSession" },
    { MenuItemKind::Normal,  kCmdFileSavesession,           &Label::FileSaveSession,           "file.saveSession" },
    { MenuItemKind::Separator },
    // Print.
    { MenuItemKind::Normal,  kCmdFilePrint,                 &Label::FilePrint,                 "file.print" },
    { MenuItemKind::Normal,  kCmdFilePrintnow,              &Label::FilePrintNow,              "file.printNow" },
    { MenuItemKind::Separator },
    // The "Recent Files" submenu (wxFileHistory) is inserted here at runtime by buildMenuBar().
    { MenuItemKind::DynamicSlot, 0, nullptr, "slot.recentFiles" },
    { MenuItemKind::Normal,  kCmdFileExit,                  &Label::FileExit,                  "file.exit" },
};

static const MenuDef kFileMenu = { "menu.file", &Label::MenuFile, kFileMenuItems, WXSIZEOF(kFileMenuItems) };
