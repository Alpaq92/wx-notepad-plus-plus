#pragma once
#include "menu_model.h"
#include "menu_labels_view.h"
#include "command_ids.h"

// ----------------------------------------------------------------- View
// wxNote's View menu is organized by kind-of-thing, most-reached-for first:
// panels (the everyday "show me a panel" actions) lead, then display toggles,
// then folding, then tab/document placement, then window-state modes, then
// text direction, with the niche "View Current File In (browser)" submenu last.
// Within each cluster, canonical sequences (zoom in/out/restore, numbered
// fold/tab lists) are preserved as-is.

static const MenuItemDef kViewCurrentFileInItems[] = {
    { MenuItemKind::Normal, kCmdViewInFirefox, &Label::ViewInFirefox, "view.viewCurrentFileIn.firefox" },
    { MenuItemKind::Normal, kCmdViewInChrome,  &Label::ViewInChrome,  "view.viewCurrentFileIn.chrome" },
    { MenuItemKind::Normal, kCmdViewInEdge,    &Label::ViewInEdge,    "view.viewCurrentFileIn.edge" },
    { MenuItemKind::Normal, kCmdViewInIe,      &Label::ViewInIE,      "view.viewCurrentFileIn.ie" },
};

static const MenuItemDef kViewShowSymbolItems[] = {
    { MenuItemKind::Check, kCmdViewTabSpace,      &Label::ViewShowSpaceAndTab,   "view.showSymbol.spaceAndTab" },
    { MenuItemKind::Check, kCmdViewEol,            &Label::ViewShowEOL,           "view.showSymbol.eol" },
    { MenuItemKind::Check, kCmdViewNpc,            &Label::ViewShowNPC,           "view.showSymbol.npc" },
    { MenuItemKind::Check, kCmdViewNpcCcunieol,   &Label::ViewShowNpcCcUniEOL,   "view.showSymbol.npcCcUniEol" },
    { MenuItemKind::Check, kCmdViewAllCharacters, &Label::ViewShowAllCharacters, "view.showSymbol.allCharacters" },
    { MenuItemKind::Separator },
    { MenuItemKind::Check, kCmdViewIndentGuide,   &Label::ViewShowIndentGuide,   "view.showSymbol.indentGuide" },
    { MenuItemKind::Check, kCmdViewWrapSymbol,    &Label::ViewShowWrapSymbol,    "view.showSymbol.wrapSymbol" },
};

static const MenuItemDef kViewZoomItems[] = {
    { MenuItemKind::Normal, kCmdViewZoomin,      &Label::ViewZoomIn,      "view.zoom.in" },
    { MenuItemKind::Normal, kCmdViewZoomout,     &Label::ViewZoomOut,     "view.zoom.out" },
    { MenuItemKind::Normal, kCmdViewZoomrestore, &Label::ViewZoomRestore, "view.zoom.restore" },
};

static const MenuItemDef kViewMoveCloneCurrentDocumentItems[] = {
    { MenuItemKind::Normal, kCmdViewGotoAnotherView,     &Label::ViewGotoAnotherView,     "view.moveClone.gotoAnotherView" },
    { MenuItemKind::Normal, kCmdViewCloneToAnotherView, &Label::ViewCloneToAnotherView,  "view.moveClone.cloneToAnotherView" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdViewGotoNewInstance,     &Label::ViewGotoNewInstance,     "view.moveClone.gotoNewInstance" },
    { MenuItemKind::Normal, kCmdViewLoadInNewInstance,  &Label::ViewLoadInNewInstance,   "view.moveClone.loadInNewInstance" },
};

static const MenuItemDef kViewTabItems[] = {
    { MenuItemKind::Normal, kCmdViewTab1, &Label::ViewTab1, "view.tab.tab1" },
    { MenuItemKind::Normal, kCmdViewTab2, &Label::ViewTab2, "view.tab.tab2" },
    { MenuItemKind::Normal, kCmdViewTab3, &Label::ViewTab3, "view.tab.tab3" },
    { MenuItemKind::Normal, kCmdViewTab4, &Label::ViewTab4, "view.tab.tab4" },
    { MenuItemKind::Normal, kCmdViewTab5, &Label::ViewTab5, "view.tab.tab5" },
    { MenuItemKind::Normal, kCmdViewTab6, &Label::ViewTab6, "view.tab.tab6" },
    { MenuItemKind::Normal, kCmdViewTab7, &Label::ViewTab7, "view.tab.tab7" },
    { MenuItemKind::Normal, kCmdViewTab8, &Label::ViewTab8, "view.tab.tab8" },
    { MenuItemKind::Normal, kCmdViewTab9, &Label::ViewTab9, "view.tab.tab9" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdViewTabStart, &Label::ViewTabFirst, "view.tab.first" },
    { MenuItemKind::Normal, kCmdViewTabEnd,   &Label::ViewTabLast,  "view.tab.last" },
    { MenuItemKind::Normal, kCmdViewTabNext,  &Label::ViewTabNext,  "view.tab.next" },
    { MenuItemKind::Normal, kCmdViewTabPrev,  &Label::ViewTabPrev,  "view.tab.prev" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdViewGotoStart,          &Label::ViewMoveToStart,      "view.tab.moveToStart" },
    { MenuItemKind::Normal, kCmdViewGotoEnd,            &Label::ViewMoveToEnd,        "view.tab.moveToEnd" },
    { MenuItemKind::Normal, kCmdViewTabMoveforward,     &Label::ViewTabMoveForward,   "view.tab.moveForward" },
    { MenuItemKind::Normal, kCmdViewTabMoveBackward,    &Label::ViewTabMoveBackward,  "view.tab.moveBackward" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdViewTabColour1,    &Label::ViewTabApplyColour1, "view.tab.colour1" },
    { MenuItemKind::Normal, kCmdViewTabColour2,    &Label::ViewTabApplyColour2, "view.tab.colour2" },
    { MenuItemKind::Normal, kCmdViewTabColour3,    &Label::ViewTabApplyColour3, "view.tab.colour3" },
    { MenuItemKind::Normal, kCmdViewTabColour4,    &Label::ViewTabApplyColour4, "view.tab.colour4" },
    { MenuItemKind::Normal, kCmdViewTabColour5,    &Label::ViewTabApplyColour5, "view.tab.colour5" },
    { MenuItemKind::Normal, kCmdViewTabColourNone, &Label::ViewTabRemoveColour, "view.tab.colourNone" },
};

static const MenuItemDef kViewFoldLevelItems[] = {
    { MenuItemKind::Normal, kCmdViewFold1, &Label::ViewFold1, "view.foldLevel.1" },
    { MenuItemKind::Normal, kCmdViewFold2, &Label::ViewFold2, "view.foldLevel.2" },
    { MenuItemKind::Normal, kCmdViewFold3, &Label::ViewFold3, "view.foldLevel.3" },
    { MenuItemKind::Normal, kCmdViewFold4, &Label::ViewFold4, "view.foldLevel.4" },
    { MenuItemKind::Normal, kCmdViewFold5, &Label::ViewFold5, "view.foldLevel.5" },
    { MenuItemKind::Normal, kCmdViewFold6, &Label::ViewFold6, "view.foldLevel.6" },
    { MenuItemKind::Normal, kCmdViewFold7, &Label::ViewFold7, "view.foldLevel.7" },
    { MenuItemKind::Normal, kCmdViewFold8, &Label::ViewFold8, "view.foldLevel.8" },
};

static const MenuItemDef kViewUnfoldLevelItems[] = {
    { MenuItemKind::Normal, kCmdViewUnfold1, &Label::ViewUnfold1, "view.unfoldLevel.1" },
    { MenuItemKind::Normal, kCmdViewUnfold2, &Label::ViewUnfold2, "view.unfoldLevel.2" },
    { MenuItemKind::Normal, kCmdViewUnfold3, &Label::ViewUnfold3, "view.unfoldLevel.3" },
    { MenuItemKind::Normal, kCmdViewUnfold4, &Label::ViewUnfold4, "view.unfoldLevel.4" },
    { MenuItemKind::Normal, kCmdViewUnfold5, &Label::ViewUnfold5, "view.unfoldLevel.5" },
    { MenuItemKind::Normal, kCmdViewUnfold6, &Label::ViewUnfold6, "view.unfoldLevel.6" },
    { MenuItemKind::Normal, kCmdViewUnfold7, &Label::ViewUnfold7, "view.unfoldLevel.7" },
    { MenuItemKind::Normal, kCmdViewUnfold8, &Label::ViewUnfold8, "view.unfoldLevel.8" },
};

static const MenuItemDef kViewProjectPanelsItems[] = {
    { MenuItemKind::Normal, kCmdViewProjectPanel1, &Label::ViewProjectPanel1, "view.projectPanels.1" },
    { MenuItemKind::Normal, kCmdViewProjectPanel2, &Label::ViewProjectPanel2, "view.projectPanels.2" },
    { MenuItemKind::Normal, kCmdViewProjectPanel3, &Label::ViewProjectPanel3, "view.projectPanels.3" },
};

static const MenuItemDef kViewMenuItems[] = {
    // Panels -- the everyday "show me a panel" actions come first.
    { MenuItemKind::Normal, kCmdViewFuncList,   &Label::ViewFuncList,    "view.funcList" },
    { MenuItemKind::Normal, kCmdViewDocMap,     &Label::ViewDocMap,      "view.docMap" },
    { MenuItemKind::Normal, kCmdViewDoclist,     &Label::ViewDocList,     "view.docList" },
    { MenuItemKind::Normal, kCmdViewFilebrowser, &Label::ViewFileBrowser, "view.fileBrowser" },
    { MenuItemKind::Submenu, 0, &Label::ViewProjectPanels, "view.projectPanels",
      kViewProjectPanelsItems, WXSIZEOF(kViewProjectPanelsItems) },
    { MenuItemKind::Check,  myID_VIEW_TERMINAL,   &Label::ViewTerminal,    "view.terminal" },
    { MenuItemKind::Check, kCmdViewMonitoring, &Label::ViewMonitoring, "view.monitoring" },
    { MenuItemKind::Separator },
    // Display toggles -- how the current buffer is rendered.
    { MenuItemKind::Check,  kCmdViewWrap,      &Label::ViewWordWrap,  "view.wordWrap" },
    { MenuItemKind::Submenu, 0, &Label::ViewShowSymbol, "view.showSymbol",
      kViewShowSymbolItems, WXSIZEOF(kViewShowSymbolItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewZoom, "view.zoom",
      kViewZoomItems, WXSIZEOF(kViewZoomItems) },
    { MenuItemKind::Normal, kCmdViewSummary, &Label::ViewSummary, "view.summary" },
    { MenuItemKind::Separator },
    // Folding -- collapse/expand structure.
    { MenuItemKind::Normal, kCmdViewFoldall,        &Label::ViewFoldAll,        "view.foldAll" },
    { MenuItemKind::Normal, kCmdViewUnfoldall,      &Label::ViewUnfoldAll,      "view.unfoldAll" },
    { MenuItemKind::Normal, kCmdViewFoldCurrent,   &Label::ViewFoldCurrent,    "view.foldCurrent" },
    { MenuItemKind::Normal, kCmdViewUnfoldCurrent, &Label::ViewUnfoldCurrent,  "view.unfoldCurrent" },
    { MenuItemKind::Submenu, 0, &Label::ViewFoldLevel, "view.foldLevel",
      kViewFoldLevelItems, WXSIZEOF(kViewFoldLevelItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewUnfoldLevel, "view.unfoldLevel",
      kViewUnfoldLevelItems, WXSIZEOF(kViewUnfoldLevelItems) },
    { MenuItemKind::Normal, kCmdViewHidelines, &Label::ViewHideLines, "view.hideLines" },
    { MenuItemKind::Separator },
    // Tabs / document placement -- where the document lives.
    { MenuItemKind::Submenu, 0, &Label::ViewTab, "view.tab",
      kViewTabItems, WXSIZEOF(kViewTabItems) },
    { MenuItemKind::Submenu, 0, &Label::ViewMoveCloneCurrentDocument, "view.moveClone",
      kViewMoveCloneCurrentDocumentItems, WXSIZEOF(kViewMoveCloneCurrentDocumentItems) },
    { MenuItemKind::Separator },
    // Window-state modes.
    { MenuItemKind::Check, kCmdViewFullScreenToggle, &Label::ViewFullScreenToggle,   "view.fullScreenToggle" },
    { MenuItemKind::Check, kCmdViewAlwaysontop,      &Label::ViewAlwaysOnTop,        "view.alwaysOnTop" },
    { MenuItemKind::Check, kCmdViewPostit,           &Label::ViewPostIt,             "view.postIt" },
    { MenuItemKind::Check, kCmdViewDistractionFree,  &Label::ViewDistractionFree,    "view.distractionFree" },
    { MenuItemKind::Separator },
    // Text direction.
    { MenuItemKind::Normal, kCmdEditRtl, &Label::ViewEditRTL, "view.editRtl" },
    { MenuItemKind::Normal, kCmdEditLtr, &Label::ViewEditLTR, "view.editLtr" },
    { MenuItemKind::Separator },
    // Niche: open the current file in an external browser.
    { MenuItemKind::Submenu, 0, &Label::ViewViewCurrentFileIn, "view.viewCurrentFileIn",
      kViewCurrentFileInItems, WXSIZEOF(kViewCurrentFileInItems) },
};

static const MenuDef kViewMenu = { "menu.view", &Label::MenuView, kViewMenuItems, WXSIZEOF(kViewMenuItems) };
