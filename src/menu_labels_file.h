#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuFile() { return _("&File"); }

    inline const wxString FileNew() { return _("&New\tCtrl+N"); }
    inline const wxString FileOpen() { return _("&Open...\tCtrl+O"); }

    inline const wxString FileOpenContainingFolder() { return _("Open Containing &Folder"); }
    inline const wxString FileOpenExplorer() { return _("Explorer"); }
    inline const wxString FileOpenCmd() { return _("cmd"); }
    inline const wxString FileOpenPowerShell() { return _("PowerShell"); }
    inline const wxString FileFolderAsWorkspace() { return _("Folder as Workspace"); }

    inline const wxString FileOpenDefaultViewer() { return _("Open in &Default Viewer"); }
    inline const wxString FileOpenFolderAsWorkspace() { return _("Open Folder as &Workspace..."); }
    inline const wxString FileReload() { return _("Re&load from Disk"); }
    inline const wxString FileSave() { return _("&Save\tCtrl+S"); }
    inline const wxString FileSaveAs() { return _("Save &As...\tCtrl+Alt+S"); }
    inline const wxString FileSaveCopyAs() { return _("Save a Cop&y As..."); }
    inline const wxString FileSaveAll() { return _("Sa&ve All\tCtrl+Shift+S"); }
    inline const wxString FileRename() { return _("&Rename..."); }
    inline const wxString FileClose() { return _("&Close\tCtrl+W"); }
    inline const wxString FileCloseAll() { return _("Clos&e All\tCtrl+Shift+W"); }

    inline const wxString FileCloseMultipleDocuments() { return _("Close &Multiple Documents"); }
    inline const wxString FileCloseAllButCurrent() { return _("Close All but Active Document"); }
    inline const wxString FileCloseAllButPinned() { return _("Close All but Pinned Documents"); }
    inline const wxString FileCloseAllToLeft() { return _("Close All to the Left"); }
    inline const wxString FileCloseAllToRight() { return _("Close All to the Right"); }
    inline const wxString FileCloseAllUnchanged() { return _("Close All Unchanged"); }

    inline const wxString FileRestoreLastClosed() { return _("Restore Recent Closed File\tCtrl+Shift+T"); }
    inline const wxString FileDelete() { return _("Move to Recycle &Bin"); }

    inline const wxString FileLoadSession() { return _("Load Sess&ion..."); }
    inline const wxString FileSaveSession() { return _("Save Sess&ion..."); }

    inline const wxString FilePrint() { return _("&Print...\tCtrl+P"); }
    inline const wxString FilePrintNow() { return _("Print No&w"); }

    inline const wxString FileExit() { return _("E&xit\tAlt+F4"); }
}
