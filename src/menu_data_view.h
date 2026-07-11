#pragma once
#include "menu_model.h"
#include "menu_labels_view.h"
#include "menuCmdID.h"

// ----------------------------------------------------------------- View
// Mechanical, zero-behavior-change port of the View menu from the old
// inline buildNppMainMenu() (see src/npp_menu.h). Same items, same order,
// same IDM_* ids, same labels, same shortcuts, same nesting.

static const MenuItemDef kViewCurrentFileInItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_IN_FIREFOX, &Label::ViewInFirefox, "view.viewCurrentFileIn.firefox" },
    { MenuItemKind::Normal, IDM_VIEW_IN_CHROME,  &Label::ViewInChrome,  "view.viewCurrentFileIn.chrome" },
    { MenuItemKind::Normal, IDM_VIEW_IN_EDGE,    &Label::ViewInEdge,    "view.viewCurrentFileIn.edge" },
    { MenuItemKind::Normal, IDM_VIEW_IN_IE,      &Label::ViewInIE,      "view.viewCurrentFileIn.ie" },
};

static const MenuItemDef kViewShowSymbolItems[] = {
    { MenuItemKind::Check, IDM_VIEW_TAB_SPACE,      &Label::ViewShowSpaceAndTab,   "view.showSymbol.spaceAndTab" },
    { MenuItemKind::Check, IDM_VIEW_EOL,            &Label::ViewShowEOL,           "view.showSymbol.eol" },
    { MenuItemKind::Check, IDM_VIEW_NPC,            &Label::ViewShowNPC,           "view.showSymbol.npc" },
    { MenuItemKind::Check, IDM_VIEW_NPC_CCUNIEOL,   &Label::ViewShowNpcCcUniEOL,   "view.showSymbol.npcCcUniEol" },
    { MenuItemKind::Check, IDM_VIEW_ALL_CHARACTERS, &Label::ViewShowAllCharacters, "view.showSymbol.allCharacters" },
    { MenuItemKind::Separator },
    { MenuItemKind::Check, IDM_VIEW_INDENT_GUIDE,   &Label::ViewShowIndentGuide,   "view.showSymbol.indentGuide" },
    { MenuItemKind::Check, IDM_VIEW_WRAP_SYMBOL,    &Label::ViewShowWrapSymbol,    "view.showSymbol.wrapSymbol" },
};

static const MenuItemDef kViewZoomItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_ZOOMIN,      &Label::ViewZoomIn,      "view.zoom.in" },
    { MenuItemKind::Normal, IDM_VIEW_ZOOMOUT,     &Label::ViewZoomOut,     "view.zoom.out" },
    { MenuItemKind::Normal, IDM_VIEW_ZOOMRESTORE, &Label::ViewZoomRestore, "view.zoom.restore" },
};

static const MenuItemDef kViewMoveCloneCurrentDocumentItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_GOTO_ANOTHER_VIEW,     &Label::ViewGotoAnotherView,     "view.moveClone.gotoAnotherView" },
    { MenuItemKind::Normal, IDM_VIEW_CLONE_TO_ANOTHER_VIEW, &Label::ViewCloneToAnotherView,  "view.moveClone.cloneToAnotherView" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_GOTO_NEW_INSTANCE,     &Label::ViewGotoNewInstance,     "view.moveClone.gotoNewInstance" },
    { MenuItemKind::Normal, IDM_VIEW_LOAD_IN_NEW_INSTANCE,  &Label::ViewLoadInNewInstance,   "view.moveClone.loadInNewInstance" },
};

static const MenuItemDef kViewTabItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_TAB1, &Label::ViewTab1, "view.tab.tab1" },
    { MenuItemKind::Normal, IDM_VIEW_TAB2, &Label::ViewTab2, "view.tab.tab2" },
    { MenuItemKind::Normal, IDM_VIEW_TAB3, &Label::ViewTab3, "view.tab.tab3" },
    { MenuItemKind::Normal, IDM_VIEW_TAB4, &Label::ViewTab4, "view.tab.tab4" },
    { MenuItemKind::Normal, IDM_VIEW_TAB5, &Label::ViewTab5, "view.tab.tab5" },
    { MenuItemKind::Normal, IDM_VIEW_TAB6, &Label::ViewTab6, "view.tab.tab6" },
    { MenuItemKind::Normal, IDM_VIEW_TAB7, &Label::ViewTab7, "view.tab.tab7" },
    { MenuItemKind::Normal, IDM_VIEW_TAB8, &Label::ViewTab8, "view.tab.tab8" },
    { MenuItemKind::Normal, IDM_VIEW_TAB9, &Label::ViewTab9, "view.tab.tab9" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_TAB_START, &Label::ViewTabFirst, "view.tab.first" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_END,   &Label::ViewTabLast,  "view.tab.last" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_NEXT,  &Label::ViewTabNext,  "view.tab.next" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_PREV,  &Label::ViewTabPrev,  "view.tab.prev" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_GOTO_START,          &Label::ViewMoveToStart,      "view.tab.moveToStart" },
    { MenuItemKind::Normal, IDM_VIEW_GOTO_END,            &Label::ViewMoveToEnd,        "view.tab.moveToEnd" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_MOVEFORWARD,     &Label::ViewTabMoveForward,   "view.tab.moveForward" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_MOVEBACKWARD,    &Label::ViewTabMoveBackward,  "view.tab.moveBackward" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_1,    &Label::ViewTabApplyColour1, "view.tab.colour1" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_2,    &Label::ViewTabApplyColour2, "view.tab.colour2" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_3,    &Label::ViewTabApplyColour3, "view.tab.colour3" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_4,    &Label::ViewTabApplyColour4, "view.tab.colour4" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_5,    &Label::ViewTabApplyColour5, "view.tab.colour5" },
    { MenuItemKind::Normal, IDM_VIEW_TAB_COLOUR_NONE, &Label::ViewTabRemoveColour, "view.tab.colourNone" },
};

static const MenuItemDef kViewFoldLevelItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_FOLD_1, &Label::ViewFold1, "view.foldLevel.1" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_2, &Label::ViewFold2, "view.foldLevel.2" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_3, &Label::ViewFold3, "view.foldLevel.3" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_4, &Label::ViewFold4, "view.foldLevel.4" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_5, &Label::ViewFold5, "view.foldLevel.5" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_6, &Label::ViewFold6, "view.foldLevel.6" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_7, &Label::ViewFold7, "view.foldLevel.7" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_8, &Label::ViewFold8, "view.foldLevel.8" },
};

static const MenuItemDef kViewUnfoldLevelItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_1, &Label::ViewUnfold1, "view.unfoldLevel.1" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_2, &Label::ViewUnfold2, "view.unfoldLevel.2" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_3, &Label::ViewUnfold3, "view.unfoldLevel.3" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_4, &Label::ViewUnfold4, "view.unfoldLevel.4" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_5, &Label::ViewUnfold5, "view.unfoldLevel.5" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_6, &Label::ViewUnfold6, "view.unfoldLevel.6" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_7, &Label::ViewUnfold7, "view.unfoldLevel.7" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_8, &Label::ViewUnfold8, "view.unfoldLevel.8" },
};

static const MenuItemDef kViewProjectPanelsItems[] = {
    { MenuItemKind::Normal, IDM_VIEW_PROJECT_PANEL_1, &Label::ViewProjectPanel1, "view.projectPanels.1" },
    { MenuItemKind::Normal, IDM_VIEW_PROJECT_PANEL_2, &Label::ViewProjectPanel2, "view.projectPanels.2" },
    { MenuItemKind::Normal, IDM_VIEW_PROJECT_PANEL_3, &Label::ViewProjectPanel3, "view.projectPanels.3" },
};

static const MenuItemDef kViewMenuItems[] = {
    { MenuItemKind::Check, IDM_VIEW_ALWAYSONTOP,      &Label::ViewAlwaysOnTop,        "view.alwaysOnTop" },
    { MenuItemKind::Check, IDM_VIEW_FULLSCREENTOGGLE, &Label::ViewFullScreenToggle,   "view.fullScreenToggle" },
    { MenuItemKind::Check, IDM_VIEW_POSTIT,           &Label::ViewPostIt,             "view.postIt" },
    { MenuItemKind::Check, IDM_VIEW_DISTRACTIONFREE,  &Label::ViewDistractionFree,    "view.distractionFree" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::ViewViewCurrentFileIn, "view.viewCurrentFileIn",
      kViewCurrentFileInItems, WXSIZEOF(kViewCurrentFileInItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::ViewShowSymbol, "view.showSymbol",
      kViewShowSymbolItems, WXSIZEOF(kViewShowSymbolItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewZoom, "view.zoom",
      kViewZoomItems, WXSIZEOF(kViewZoomItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewMoveCloneCurrentDocument, "view.moveClone",
      kViewMoveCloneCurrentDocumentItems, WXSIZEOF(kViewMoveCloneCurrentDocumentItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewTab, "view.tab",
      kViewTabItems, WXSIZEOF(kViewTabItems) },
    { MenuItemKind::Check,  IDM_VIEW_WRAP,      &Label::ViewWordWrap,  "view.wordWrap" },
    { MenuItemKind::Normal, IDM_VIEW_HIDELINES, &Label::ViewHideLines, "view.hideLines" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_FOLDALL,        &Label::ViewFoldAll,        "view.foldAll" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLDALL,      &Label::ViewUnfoldAll,      "view.unfoldAll" },
    { MenuItemKind::Normal, IDM_VIEW_FOLD_CURRENT,   &Label::ViewFoldCurrent,    "view.foldCurrent" },
    { MenuItemKind::Normal, IDM_VIEW_UNFOLD_CURRENT, &Label::ViewUnfoldCurrent,  "view.unfoldCurrent" },
    { MenuItemKind::Submenu, 0, &Label::ViewFoldLevel, "view.foldLevel",
      kViewFoldLevelItems, WXSIZEOF(kViewFoldLevelItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewUnfoldLevel, "view.unfoldLevel",
      kViewUnfoldLevelItems, WXSIZEOF(kViewUnfoldLevelItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_VIEW_SUMMARY, &Label::ViewSummary, "view.summary" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::ViewProjectPanels, "view.projectPanels",
      kViewProjectPanelsItems, WXSIZEOF(kViewProjectPanelsItems) },
    { MenuItemKind::Normal, IDM_VIEW_FILEBROWSER, &Label::ViewFileBrowser, "view.fileBrowser" },
    { MenuItemKind::Normal, IDM_VIEW_DOC_MAP,     &Label::ViewDocMap,      "view.docMap" },
    { MenuItemKind::Normal, IDM_VIEW_DOCLIST,     &Label::ViewDocList,     "view.docList" },
    { MenuItemKind::Normal, IDM_VIEW_FUNC_LIST,   &Label::ViewFuncList,    "view.funcList" },
    { MenuItemKind::Check,  myID_VIEW_TERMINAL,   &Label::ViewTerminal,    "view.terminal" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_RTL, &Label::ViewEditRTL, "view.editRtl" },
    { MenuItemKind::Normal, IDM_EDIT_LTR, &Label::ViewEditLTR, "view.editLtr" },
    { MenuItemKind::Separator },
    { MenuItemKind::Check, IDM_VIEW_MONITORING, &Label::ViewMonitoring, "view.monitoring" },
};

static const MenuDef kViewMenu = { "menu.view", &Label::MenuView, kViewMenuItems, WXSIZEOF(kViewMenuItems) };
