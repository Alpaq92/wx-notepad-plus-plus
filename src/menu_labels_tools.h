#pragma once
#include <wx/intl.h>

namespace Label {
inline const wxString MenuTools()          { return _("T&ools"); }
inline const wxString ToolsMd5()           { return _("MD5"); }
inline const wxString ToolsSha1()          { return _("SHA-1"); }
inline const wxString ToolsSha256()        { return _("SHA-256"); }
inline const wxString ToolsSha512()        { return _("SHA-512"); }
inline const wxString ToolsGenerate()      { return _("Generate..."); }
inline const wxString ToolsGenerateFromFile() { return _("Generate from files..."); }
inline const wxString ToolsGenerateIntoClipboard() { return _("Generate from selection into clipboard"); }
}
