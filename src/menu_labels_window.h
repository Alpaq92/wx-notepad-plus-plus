#pragma once
#include <wx/intl.h>

namespace Label {
inline const wxString MenuWindow()          { return _("&Window"); }
inline const wxString WindowSortBy()        { return _("Sort By"); }
inline const wxString WindowSortNameAsc()   { return _("Name A to Z"); }
inline const wxString WindowSortNameDesc()  { return _("Name Z to A"); }
inline const wxString WindowSortPathAsc()   { return _("Path A to Z"); }
inline const wxString WindowSortPathDesc()  { return _("Path Z to A"); }
inline const wxString WindowSortTypeAsc()   { return _("Type A to Z"); }
inline const wxString WindowSortTypeDesc()  { return _("Type Z to A"); }
inline const wxString WindowSortSizeAsc()   { return _("Content Length Ascending"); }
inline const wxString WindowSortSizeDesc()  { return _("Content Length Descending"); }
inline const wxString WindowSortModifiedAsc()  { return _("Modified Time Ascending"); }
inline const wxString WindowSortModifiedDesc() { return _("Modified Time Descending"); }
inline const wxString WindowWindows()       { return _("&Windows..."); }
inline const wxString WindowRecentWindow()  { return _("Recent Window"); }
}
