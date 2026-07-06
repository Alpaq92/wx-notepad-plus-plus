#pragma once
#include "menu_model.h"
#include "menu_labels_search.h"
#include "menuCmdID.h"

// --------------------------------------------------------------- Search
// Mechanical, zero-behavior-change port of the Search menu from the old
// inline buildNppMainMenu() (see src/npp_menu.h). Same items, same order,
// same IDM_* ids, same labels, same shortcuts, same nesting.

static const MenuItemDef kSearchChangeHistoryItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_CHANGED_NEXT,         &Label::SearchChangedNext,         "search.changeHistory.next" },
    { MenuItemKind::Normal, IDM_SEARCH_CHANGED_PREV,         &Label::SearchChangedPrev,         "search.changeHistory.prev" },
    { MenuItemKind::Normal, IDM_SEARCH_CLEAR_CHANGE_HISTORY, &Label::SearchClearChangeHistory,  "search.changeHistory.clear" },
};

static const MenuItemDef kSearchStyleAllOccurrencesItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_MARKALLEXT1, &Label::SearchMarkStyleUsing1st, "search.styleAllOccurrences.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKALLEXT2, &Label::SearchMarkStyleUsing2nd, "search.styleAllOccurrences.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKALLEXT3, &Label::SearchMarkStyleUsing3rd, "search.styleAllOccurrences.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKALLEXT4, &Label::SearchMarkStyleUsing4th, "search.styleAllOccurrences.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKALLEXT5, &Label::SearchMarkStyleUsing5th, "search.styleAllOccurrences.5th" },
};

static const MenuItemDef kSearchStyleOneTokenItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_MARKONEEXT1, &Label::SearchMarkStyleUsing1st, "search.styleOneToken.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKONEEXT2, &Label::SearchMarkStyleUsing2nd, "search.styleOneToken.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKONEEXT3, &Label::SearchMarkStyleUsing3rd, "search.styleOneToken.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKONEEXT4, &Label::SearchMarkStyleUsing4th, "search.styleOneToken.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKONEEXT5, &Label::SearchMarkStyleUsing5th, "search.styleOneToken.5th" },
};

static const MenuItemDef kSearchClearStyleItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_UNMARKALLEXT1, &Label::SearchClearStyle1st,  "search.clearStyle.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_UNMARKALLEXT2, &Label::SearchClearStyle2nd,  "search.clearStyle.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_UNMARKALLEXT3, &Label::SearchClearStyle3rd,  "search.clearStyle.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_UNMARKALLEXT4, &Label::SearchClearStyle4th,  "search.clearStyle.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_UNMARKALLEXT5, &Label::SearchClearStyle5th,  "search.clearStyle.5th" },
    { MenuItemKind::Normal, IDM_SEARCH_CLEARALLMARKS,  &Label::SearchClearAllStyles, "search.clearStyle.all" },
};

static const MenuItemDef kSearchJumpUpItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER1,    &Label::SearchMarkStyle1st,    "search.jumpUp.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER2,    &Label::SearchMarkStyle2nd,    "search.jumpUp.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER3,    &Label::SearchMarkStyle3rd,    "search.jumpUp.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER4,    &Label::SearchMarkStyle4th,    "search.jumpUp.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER5,    &Label::SearchMarkStyle5th,    "search.jumpUp.5th" },
    { MenuItemKind::Normal, IDM_SEARCH_GOPREVMARKER_DEF, &Label::SearchFindMarkStyle,   "search.jumpUp.findMarkStyle" },
};

static const MenuItemDef kSearchJumpDownItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER1,    &Label::SearchMarkStyle1st,    "search.jumpDown.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER2,    &Label::SearchMarkStyle2nd,    "search.jumpDown.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER3,    &Label::SearchMarkStyle3rd,    "search.jumpDown.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER4,    &Label::SearchMarkStyle4th,    "search.jumpDown.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER5,    &Label::SearchMarkStyle5th,    "search.jumpDown.5th" },
    { MenuItemKind::Normal, IDM_SEARCH_GONEXTMARKER_DEF, &Label::SearchFindMarkStyle,   "search.jumpDown.findMarkStyle" },
};

static const MenuItemDef kSearchCopyStyledTextItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_STYLE1TOCLIP,     &Label::SearchMarkStyle1st,   "search.copyStyledText.1st" },
    { MenuItemKind::Normal, IDM_SEARCH_STYLE2TOCLIP,     &Label::SearchMarkStyle2nd,   "search.copyStyledText.2nd" },
    { MenuItemKind::Normal, IDM_SEARCH_STYLE3TOCLIP,     &Label::SearchMarkStyle3rd,   "search.copyStyledText.3rd" },
    { MenuItemKind::Normal, IDM_SEARCH_STYLE4TOCLIP,     &Label::SearchMarkStyle4th,   "search.copyStyledText.4th" },
    { MenuItemKind::Normal, IDM_SEARCH_STYLE5TOCLIP,     &Label::SearchMarkStyle5th,   "search.copyStyledText.5th" },
    { MenuItemKind::Normal, IDM_SEARCH_ALLSTYLESTOCLIP,  &Label::SearchAllStyles,      "search.copyStyledText.all" },
    { MenuItemKind::Normal, IDM_SEARCH_MARKEDTOCLIP,     &Label::SearchFindMarkStyle,  "search.copyStyledText.findMarkStyle" },
};

static const MenuItemDef kSearchBookmarkItems[] = {
    { MenuItemKind::Normal, IDM_SEARCH_TOGGLE_BOOKMARK,     &Label::SearchToggleBookmark,      "search.bookmark.toggle" },
    { MenuItemKind::Normal, IDM_SEARCH_NEXT_BOOKMARK,       &Label::SearchNextBookmark,        "search.bookmark.next" },
    { MenuItemKind::Normal, IDM_SEARCH_PREV_BOOKMARK,       &Label::SearchPrevBookmark,        "search.bookmark.prev" },
    { MenuItemKind::Normal, IDM_SEARCH_CLEAR_BOOKMARKS,     &Label::SearchClearBookmarks,      "search.bookmark.clearAll" },
    { MenuItemKind::Normal, IDM_SEARCH_CUTMARKEDLINES,      &Label::SearchCutMarkedLines,      "search.bookmark.cutLines" },
    { MenuItemKind::Normal, IDM_SEARCH_COPYMARKEDLINES,     &Label::SearchCopyMarkedLines,     "search.bookmark.copyLines" },
    { MenuItemKind::Normal, IDM_SEARCH_PASTEMARKEDLINES,    &Label::SearchPasteMarkedLines,    "search.bookmark.pasteLines" },
    { MenuItemKind::Normal, IDM_SEARCH_DELETEMARKEDLINES,   &Label::SearchDeleteMarkedLines,   "search.bookmark.deleteLines" },
    { MenuItemKind::Normal, IDM_SEARCH_DELETEUNMARKEDLINES, &Label::SearchDeleteUnmarkedLines, "search.bookmark.deleteUnmarkedLines" },
    { MenuItemKind::Normal, IDM_SEARCH_INVERSEMARKS,        &Label::SearchInverseMarks,        "search.bookmark.inverse" },
};

static const MenuItemDef kSearchMenuItems[] = {
    { MenuItemKind::Normal,  IDM_SEARCH_FIND,                 &Label::SearchFind,                 "search.find" },
    { MenuItemKind::Normal,  IDM_SEARCH_FINDINFILES,           &Label::SearchFindInFiles,          "search.findInFiles" },
    { MenuItemKind::Normal,  IDM_SEARCH_FINDNEXT,              &Label::SearchFindNext,             "search.findNext" },
    { MenuItemKind::Normal,  IDM_SEARCH_FINDPREV,              &Label::SearchFindPrev,             "search.findPrev" },
    { MenuItemKind::Normal,  IDM_SEARCH_SETANDFINDNEXT,        &Label::SearchSetAndFindNext,       "search.setAndFindNext" },
    { MenuItemKind::Normal,  IDM_SEARCH_SETANDFINDPREV,        &Label::SearchSetAndFindPrev,       "search.setAndFindPrev" },
    { MenuItemKind::Normal,  IDM_SEARCH_VOLATILE_FINDNEXT,     &Label::SearchVolatileFindNext,     "search.volatileFindNext" },
    { MenuItemKind::Normal,  IDM_SEARCH_VOLATILE_FINDPREV,     &Label::SearchVolatileFindPrev,     "search.volatileFindPrev" },
    { MenuItemKind::Normal,  IDM_SEARCH_REPLACE,               &Label::SearchReplace,              "search.replace" },
    { MenuItemKind::Normal,  IDM_SEARCH_FINDINCREMENT,         &Label::SearchFindIncrement,        "search.findIncrement" },
    { MenuItemKind::Normal,  IDM_FOCUS_ON_FOUND_RESULTS,       &Label::SearchFocusOnFoundResults,  "search.focusOnFoundResults" },
    { MenuItemKind::Normal,  IDM_SEARCH_GOTONEXTFOUND,         &Label::SearchGotoNextFound,        "search.gotoNextFound" },
    { MenuItemKind::Normal,  IDM_SEARCH_GOTOPREVFOUND,         &Label::SearchGotoPrevFound,        "search.gotoPrevFound" },
    { MenuItemKind::Normal,  IDM_SEARCH_GOTOLINE,              &Label::SearchGotoLine,             "search.gotoLine" },
    { MenuItemKind::Normal,  IDM_SEARCH_GOTOMATCHINGBRACE,     &Label::SearchGotoMatchingBrace,    "search.gotoMatchingBrace" },
    { MenuItemKind::Normal,  IDM_SEARCH_SELECTMATCHINGBRACES,  &Label::SearchSelectMatchingBraces, "search.selectMatchingBraces" },
    { MenuItemKind::Normal,  IDM_SEARCH_MARK,                  &Label::SearchMark,                 "search.mark" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SearchChangeHistory, "search.changeHistory",
      kSearchChangeHistoryItems, WXSIZEOF(kSearchChangeHistoryItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SearchStyleAllOccurrences, "search.styleAllOccurrences",
      kSearchStyleAllOccurrencesItems, WXSIZEOF(kSearchStyleAllOccurrencesItems) },
    { MenuItemKind::Submenu, 0, &Label::SearchStyleOneToken, "search.styleOneToken",
      kSearchStyleOneTokenItems, WXSIZEOF(kSearchStyleOneTokenItems) },
    { MenuItemKind::Submenu, 0, &Label::SearchClearStyle, "search.clearStyle",
      kSearchClearStyleItems, WXSIZEOF(kSearchClearStyleItems) },
    { MenuItemKind::Submenu, 0, &Label::SearchJumpUp, "search.jumpUp",
      kSearchJumpUpItems, WXSIZEOF(kSearchJumpUpItems) },
    { MenuItemKind::Submenu, 0, &Label::SearchJumpDown, "search.jumpDown",
      kSearchJumpDownItems, WXSIZEOF(kSearchJumpDownItems) },
    { MenuItemKind::Submenu, 0, &Label::SearchCopyStyledText, "search.copyStyledText",
      kSearchCopyStyledTextItems, WXSIZEOF(kSearchCopyStyledTextItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::SearchBookmark, "search.bookmark",
      kSearchBookmarkItems, WXSIZEOF(kSearchBookmarkItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_SEARCH_FINDCHARINRANGE, &Label::SearchFindCharInRange, "search.findCharInRange" },
};

static const MenuDef kSearchMenu = { "menu.search", &Label::MenuSearch, kSearchMenuItems, WXSIZEOF(kSearchMenuItems) };
