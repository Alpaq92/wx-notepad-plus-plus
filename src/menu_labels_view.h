#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuView() { return _("&View"); }

    inline const wxString ViewAlwaysOnTop() { return _("Always on &Top"); }
    inline const wxString ViewFullScreenToggle() { return _("To&ggle Full Screen Mode\tF11"); }
    inline const wxString ViewPostIt() { return _("&Post-It\tF12"); }
    inline const wxString ViewDistractionFree() { return _("D&istraction Free Mode"); }

    inline const wxString ViewViewCurrentFileIn() { return _("&View Current File in"); }
    inline const wxString ViewInFirefox() { return _("&Firefox"); }
    inline const wxString ViewInChrome() { return _("&Chrome"); }
    inline const wxString ViewInEdge() { return _("&Edge"); }
    inline const wxString ViewInIE() { return _("&IE"); }

    inline const wxString ViewShowSymbol() { return _("Show S&ymbol"); }
    inline const wxString ViewShowSpaceAndTab() { return _("Show Space and Tab"); }
    inline const wxString ViewShowEOL() { return _("Show End of Line"); }
    inline const wxString ViewShowNPC() { return _("Show Non-Printing Characters"); }
    inline const wxString ViewShowNpcCcUniEOL() { return _("Show Control Characters && Unicode EOL"); }
    inline const wxString ViewShowAllCharacters() { return _("Show All Characters"); }
    inline const wxString ViewShowIndentGuide() { return _("Show Indent Guide"); }
    inline const wxString ViewShowWrapSymbol() { return _("Show Wrap Symbol"); }

    inline const wxString ViewZoom() { return _("&Zoom"); }
    inline const wxString ViewZoomIn() { return _("Zoom &In (Ctrl+Mouse Wheel Up)"); }
    inline const wxString ViewZoomOut() { return _("Zoom &Out (Ctrl+Mouse Wheel Down)"); }
    inline const wxString ViewZoomRestore() { return _("&Restore Default Zoom"); }

    inline const wxString ViewMoveCloneCurrentDocument() { return _("&Move/Clone Current Document"); }
    inline const wxString ViewGotoAnotherView() { return _("Move to Other &View"); }
    inline const wxString ViewCloneToAnotherView() { return _("Clone to Other Vie&w"); }
    inline const wxString ViewGotoNewInstance() { return _("Mo&ve to New Instance"); }
    inline const wxString ViewLoadInNewInstance() { return _("&Open in New Instance"); }

    inline const wxString ViewTab() { return _("Ta&b"); }
    inline const wxString ViewTab1() { return _("1st Tab\tCtrl+1"); }
    inline const wxString ViewTab2() { return _("2nd Tab\tCtrl+2"); }
    inline const wxString ViewTab3() { return _("3rd Tab\tCtrl+3"); }
    inline const wxString ViewTab4() { return _("4th Tab\tCtrl+4"); }
    inline const wxString ViewTab5() { return _("5th Tab\tCtrl+5"); }
    inline const wxString ViewTab6() { return _("6th Tab\tCtrl+6"); }
    inline const wxString ViewTab7() { return _("7th Tab\tCtrl+7"); }
    inline const wxString ViewTab8() { return _("8th Tab\tCtrl+8"); }
    inline const wxString ViewTab9() { return _("9th Tab\tCtrl+9"); }
    inline const wxString ViewTabFirst() { return _("First Tab"); }
    inline const wxString ViewTabLast() { return _("Last Tab"); }
    inline const wxString ViewTabNext() { return _("Next Tab\tCtrl+Tab"); }
    inline const wxString ViewTabPrev() { return _("Previous Tab\tCtrl+Shift+Tab"); }
    inline const wxString ViewMoveToStart() { return _("Move to Start"); }
    inline const wxString ViewMoveToEnd() { return _("Move to End"); }
    inline const wxString ViewTabMoveForward() { return _("Move Tab Forward"); }
    inline const wxString ViewTabMoveBackward() { return _("Move Tab Backward"); }
    inline const wxString ViewTabApplyColour1() { return _("Apply Color 1"); }
    inline const wxString ViewTabApplyColour2() { return _("Apply Color 2"); }
    inline const wxString ViewTabApplyColour3() { return _("Apply Color 3"); }
    inline const wxString ViewTabApplyColour4() { return _("Apply Color 4"); }
    inline const wxString ViewTabApplyColour5() { return _("Apply Color 5"); }
    inline const wxString ViewTabRemoveColour() { return _("Remove Color"); }

    inline const wxString ViewWordWrap() { return _("&Word wrap"); }
    inline const wxString ViewHideLines() { return _("&Hide Lines"); }

    inline const wxString ViewFoldAll() { return _("Fold All\tAlt+0"); }
    inline const wxString ViewUnfoldAll() { return _("Unfold All\tAlt+Shift+0"); }
    inline const wxString ViewFoldCurrent() { return _("Fold Current Level\tCtrl+Alt+F"); }
    inline const wxString ViewUnfoldCurrent() { return _("Unfold Current Level\tCtrl+Alt+Shift+F"); }

    inline const wxString ViewFoldLevel() { return _("Fold Level"); }
    inline const wxString ViewFold1() { return _("1\tAlt+1"); }
    inline const wxString ViewFold2() { return _("2\tAlt+2"); }
    inline const wxString ViewFold3() { return _("3\tAlt+3"); }
    inline const wxString ViewFold4() { return _("4\tAlt+4"); }
    inline const wxString ViewFold5() { return _("5\tAlt+5"); }
    inline const wxString ViewFold6() { return _("6\tAlt+6"); }
    inline const wxString ViewFold7() { return _("7\tAlt+7"); }
    inline const wxString ViewFold8() { return _("8\tAlt+8"); }

    inline const wxString ViewUnfoldLevel() { return _("Unfold Level"); }
    inline const wxString ViewUnfold1() { return _("1\tAlt+Shift+1"); }
    inline const wxString ViewUnfold2() { return _("2\tAlt+Shift+2"); }
    inline const wxString ViewUnfold3() { return _("3\tAlt+Shift+3"); }
    inline const wxString ViewUnfold4() { return _("4\tAlt+Shift+4"); }
    inline const wxString ViewUnfold5() { return _("5\tAlt+Shift+5"); }
    inline const wxString ViewUnfold6() { return _("6\tAlt+Shift+6"); }
    inline const wxString ViewUnfold7() { return _("7\tAlt+Shift+7"); }
    inline const wxString ViewUnfold8() { return _("8\tAlt+Shift+8"); }

    inline const wxString ViewSummary() { return _("&Summary..."); }

    inline const wxString ViewProjectPanels() { return _("Pro&ject Panels"); }
    inline const wxString ViewProjectPanel1() { return _("Project Panel &1"); }
    inline const wxString ViewProjectPanel2() { return _("Project Panel &2"); }
    inline const wxString ViewProjectPanel3() { return _("Project Panel &3"); }

    inline const wxString ViewFileBrowser() { return _("Folder as Wor&kspace"); }
    inline const wxString ViewDocMap() { return _("&Document Map"); }
    inline const wxString ViewDocList() { return _("D&ocument List"); }
    inline const wxString ViewFuncList() { return _("Function &List"); }

    inline const wxString ViewEditRTL() { return _("T&ext Direction RTL"); }
    inline const wxString ViewEditLTR() { return _("Te&xt Direction LTR"); }

    inline const wxString ViewMonitoring() { return _("Monito&ring (tail -f)"); }
}
