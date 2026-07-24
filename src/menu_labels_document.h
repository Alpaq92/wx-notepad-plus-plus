#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuDocument() { return _("&Document"); }

    // Compare (side-by-side diff) and Spell Check moved here from the View menu - both act on the
    // current document. Strings unchanged, so existing translations carry over by msgid.
    inline const wxString DocumentCompare()          { return _("&Compare"); }
    inline const wxString DocumentCompareFile()       { return _("Compare with &File..."); }
    inline const wxString DocumentCompareClipboard()  { return _("Compare with Cli&pboard"); }
    inline const wxString DocumentCompareNext()       { return _("&Next Difference"); }
    inline const wxString DocumentComparePrev()       { return _("&Previous Difference"); }
    inline const wxString DocumentCompareClear()      { return _("C&lear Compare"); }
    inline const wxString DocumentSpellCheck()        { return _("Spell Chec&k"); }
    inline const wxString DocumentSpellEnable()       { return _("&Enable"); }
    inline const wxString DocumentSpellCommentsOnly() { return _("Check only &comments and strings"); }
    inline const wxString DocumentSpellManage()       { return _("&Manage dictionaries..."); }
}
