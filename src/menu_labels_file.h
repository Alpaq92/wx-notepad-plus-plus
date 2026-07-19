#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuFile() { return _("&File"); }

    inline const wxString FileNew() { return _("&New"); }
    inline const wxString FileOpen() { return _("&Open..."); }

    inline const wxString FileOpenContainingFolder() { return _("Open Containing &Folder"); }
    inline const wxString FileOpenExplorer() { return _("Explorer"); }
    inline const wxString FileOpenCmd() { return _("cmd"); }
    inline const wxString FileOpenPowerShell() { return _("PowerShell"); }
    // Non-Windows flavours of the submenu (see menu_data_file.h): macOS names its file manager (Finder,
    // a proper noun left untranslated); Linux gets the generic, translated terms.
    inline const wxString FileOpenFileManager()
    {
#ifdef __WXMAC__
        return "Finder";
#else
        return _("File Manager");
#endif
    }
    inline const wxString FileOpenTerminal() { return _("Terminal"); }
    inline const wxString FileFolderAsWorkspace() { return _("Folder as Workspace"); }

    inline const wxString FileOpenDefaultViewer() { return _("Open in &Default Viewer"); }
    inline const wxString FileOpenFolderAsWorkspace() { return _("Open Folder as &Workspace..."); }
    inline const wxString FileReload() { return _("Re&load from Disk"); }
    inline const wxString FileSave() { return _("&Save"); }
    inline const wxString FileSaveAs() { return _("Save &As..."); }
    inline const wxString FileSaveCopyAs() { return _("Save a Cop&y As..."); }
    inline const wxString FileSaveAll() { return _("Sa&ve All"); }
    inline const wxString FileRename() { return _("&Rename..."); }
    inline const wxString FileClose() { return _("&Close"); }
    inline const wxString FileCloseAll() { return _("Clos&e All"); }

    inline const wxString FileCloseMultipleDocuments() { return _("Close &Multiple Documents"); }
    inline const wxString FileCloseAllButCurrent() { return _("Close All but Active Document"); }
    inline const wxString FileCloseAllButPinned() { return _("Close All but Pinned Documents"); }
    inline const wxString FileCloseAllToLeft() { return _("Close All to the Left"); }
    inline const wxString FileCloseAllToRight() { return _("Close All to the Right"); }
    inline const wxString FileCloseAllUnchanged() { return _("Close All Unchanged"); }

    inline const wxString FileRestoreLastClosed() { return _("Restore Recent Closed File"); }
    inline const wxString FileDelete() { return _("Move to Recycle &Bin"); }

    inline const wxString FileLoadSession() { return _("Load Sess&ion..."); }
    inline const wxString FileSaveSession() { return _("Save Sess&ion..."); }

    inline const wxString FilePrint() { return _("&Print..."); }
    inline const wxString FilePrintNow() { return _("Print No&w"); }

    inline const wxString FileExit() { return _("E&xit"); }
}
