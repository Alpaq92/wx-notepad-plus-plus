#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuGo() { return _("&Go"); }

    inline const wxString SearchFind() { return _("&Find...\tCtrl+F"); }
    inline const wxString SearchFindInFiles() { return _("Find in Fi&les...\tCtrl+Shift+F"); }
    inline const wxString SearchFindNext() { return _("Find &Next\tF3"); }
    inline const wxString SearchFindPrev() { return _("Find &Previous\tShift+F3"); }
    inline const wxString SearchSetAndFindNext() { return _("&Select and Find Next\tCtrl+F3"); }
    inline const wxString SearchSetAndFindPrev() { return _("&Select and Find Previous\tCtrl+Shift+F3"); }
    inline const wxString SearchVolatileFindNext() { return _("Find (&Volatile) Next"); }
    inline const wxString SearchVolatileFindPrev() { return _("Find (&Volatile) Previous"); }
    inline const wxString SearchReplace() { return _("&Replace...\tCtrl+H"); }
    inline const wxString SearchFindIncrement() { return _("&Incremental Search\tCtrl+Alt+I"); }
    inline const wxString SearchFocusOnFoundResults() { return _("Search Results &Window\tF7"); }
    inline const wxString SearchGotoNextFound() { return _("Next Search Resul&t\tF4"); }
    inline const wxString SearchGotoPrevFound() { return _("Previous Search Resul&t\tShift+F4"); }
    inline const wxString SearchGotoLine() { return _("&Go to...\tCtrl+G"); }
    inline const wxString SearchGotoMatchingBrace() { return _("Go to &Matching Brace\tCtrl+B"); }
    inline const wxString SearchSelectMatchingBraces() { return _("Select All In-betw&een {} [] or ()"); }
    inline const wxString SearchMark() { return _("Mar&k..."); }

    // ---- Change History submenu
    inline const wxString SearchChangeHistory() { return _("Change History"); }
    inline const wxString SearchChangedNext() { return _("Go to Next Change"); }
    inline const wxString SearchChangedPrev() { return _("Go to Previous Change"); }
    inline const wxString SearchClearChangeHistory() { return _("Clear Change History"); }

    // ---- Shared "Using Nth Style" labels - reused by "Style All Occurrences
    // of Token" AND "Style One Token" (10 Append calls, only 5 distinct strings)
    inline const wxString SearchStyleAllOccurrences() { return _("Style &All Occurrences of Token"); }
    inline const wxString SearchMarkStyleUsing1st() { return _("Using 1st Style"); }
    inline const wxString SearchMarkStyleUsing2nd() { return _("Using 2nd Style"); }
    inline const wxString SearchMarkStyleUsing3rd() { return _("Using 3rd Style"); }
    inline const wxString SearchMarkStyleUsing4th() { return _("Using 4th Style"); }
    inline const wxString SearchMarkStyleUsing5th() { return _("Using 5th Style"); }

    inline const wxString SearchStyleOneToken() { return _("Style &One Token"); }

    // ---- Clear Style submenu
    inline const wxString SearchClearStyle() { return _("Clear Style"); }
    inline const wxString SearchClearStyle1st() { return _("Clear 1st Style"); }
    inline const wxString SearchClearStyle2nd() { return _("Clear 2nd Style"); }
    inline const wxString SearchClearStyle3rd() { return _("Clear 3rd Style"); }
    inline const wxString SearchClearStyle4th() { return _("Clear 4th Style"); }
    inline const wxString SearchClearStyle5th() { return _("Clear 5th Style"); }
    inline const wxString SearchClearAllStyles() { return _("Clear all Styles"); }

    // ---- Shared "Nth Style" labels - reused by "Jump Up", "Jump Down" AND
    // "Copy Styled Text" (also shares "Find Mark Style" below, 3 times)
    inline const wxString SearchMarkStyle1st() { return _("1st Style"); }
    inline const wxString SearchMarkStyle2nd() { return _("2nd Style"); }
    inline const wxString SearchMarkStyle3rd() { return _("3rd Style"); }
    inline const wxString SearchMarkStyle4th() { return _("4th Style"); }
    inline const wxString SearchMarkStyle5th() { return _("5th Style"); }
    inline const wxString SearchFindMarkStyle() { return _("Find Mark Style"); }

    inline const wxString SearchJumpUp() { return _("&Jump Up"); }
    inline const wxString SearchJumpDown() { return _("Jump &Down"); }

    // ---- Copy Styled Text submenu
    inline const wxString SearchCopyStyledText() { return _("&Copy Styled Text"); }
    inline const wxString SearchAllStyles() { return _("All Styles"); }

    // ---- Bookmark submenu
    inline const wxString SearchBookmark() { return _("&Bookmark"); }
    inline const wxString SearchToggleBookmark() { return _("Toggle Bookmark\tCtrl+F2"); }
    inline const wxString SearchNextBookmark() { return _("Next Bookmark\tF2"); }
    inline const wxString SearchPrevBookmark() { return _("Previous Bookmark\tShift+F2"); }
    inline const wxString SearchClearBookmarks() { return _("Clear All Bookmarks"); }
    inline const wxString SearchCutMarkedLines() { return _("Cut Bookmarked Lines"); }
    inline const wxString SearchCopyMarkedLines() { return _("Copy Bookmarked Lines"); }
    inline const wxString SearchPasteMarkedLines() { return _("Paste to (Replace) Bookmarked Lines"); }
    inline const wxString SearchDeleteMarkedLines() { return _("Remove Bookmarked Lines"); }
    inline const wxString SearchDeleteUnmarkedLines() { return _("Remove Non-Bookmarked Lines"); }
    inline const wxString SearchInverseMarks() { return _("Inverse Bookmarks"); }

    inline const wxString SearchFindCharInRange() { return _("Find characters in rang&e..."); }
}
