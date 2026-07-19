#pragma once
#include "menu_model.h"
#include "menu_labels_search.h"
#include "command_ids.h"

// --------------------------------------------------------------- Search ("Go")
// wxNote's own layout: the top flat list is clustered by intent, most-frequent
// first — Find/Replace core, then Find navigation, then found-results navigation,
// then Go-to targets, then Mark. Submenus follow with the everyday Bookmark menu
// ahead of the niche style/mark-occurrence submenus; Change History sits last as
// the rarest. Canonical pairings (Next before Prev, numbered 1st..5th style slots)
// are preserved within each cluster.

static const MenuItemDef kSearchChangeHistoryItems[] = {
    { MenuItemKind::Normal, kCmdSearchChangedNext,         &Label::SearchChangedNext,         "search.changeHistory.next" },
    { MenuItemKind::Normal, kCmdSearchChangedPrev,         &Label::SearchChangedPrev,         "search.changeHistory.prev" },
    { MenuItemKind::Normal, kCmdSearchClearChangeHistory, &Label::SearchClearChangeHistory,  "search.changeHistory.clear" },
};

static const MenuItemDef kSearchStyleAllOccurrencesItems[] = {
    { MenuItemKind::Normal, kCmdSearchMarkallext1, &Label::SearchMarkStyleUsing1st, "search.styleAllOccurrences.1st" },
    { MenuItemKind::Normal, kCmdSearchMarkallext2, &Label::SearchMarkStyleUsing2nd, "search.styleAllOccurrences.2nd" },
    { MenuItemKind::Normal, kCmdSearchMarkallext3, &Label::SearchMarkStyleUsing3rd, "search.styleAllOccurrences.3rd" },
    { MenuItemKind::Normal, kCmdSearchMarkallext4, &Label::SearchMarkStyleUsing4th, "search.styleAllOccurrences.4th" },
    { MenuItemKind::Normal, kCmdSearchMarkallext5, &Label::SearchMarkStyleUsing5th, "search.styleAllOccurrences.5th" },
};

static const MenuItemDef kSearchStyleOneTokenItems[] = {
    { MenuItemKind::Normal, kCmdSearchMarkoneext1, &Label::SearchMarkStyleUsing1st, "search.styleOneToken.1st" },
    { MenuItemKind::Normal, kCmdSearchMarkoneext2, &Label::SearchMarkStyleUsing2nd, "search.styleOneToken.2nd" },
    { MenuItemKind::Normal, kCmdSearchMarkoneext3, &Label::SearchMarkStyleUsing3rd, "search.styleOneToken.3rd" },
    { MenuItemKind::Normal, kCmdSearchMarkoneext4, &Label::SearchMarkStyleUsing4th, "search.styleOneToken.4th" },
    { MenuItemKind::Normal, kCmdSearchMarkoneext5, &Label::SearchMarkStyleUsing5th, "search.styleOneToken.5th" },
};

static const MenuItemDef kSearchClearStyleItems[] = {
    { MenuItemKind::Normal, kCmdSearchUnmarkAllExt1, &Label::SearchClearStyle1st,  "search.clearStyle.1st" },
    { MenuItemKind::Normal, kCmdSearchUnmarkAllExt2, &Label::SearchClearStyle2nd,  "search.clearStyle.2nd" },
    { MenuItemKind::Normal, kCmdSearchUnmarkAllExt3, &Label::SearchClearStyle3rd,  "search.clearStyle.3rd" },
    { MenuItemKind::Normal, kCmdSearchUnmarkAllExt4, &Label::SearchClearStyle4th,  "search.clearStyle.4th" },
    { MenuItemKind::Normal, kCmdSearchUnmarkAllExt5, &Label::SearchClearStyle5th,  "search.clearStyle.5th" },
    { MenuItemKind::Normal, kCmdSearchClearAllMarks,  &Label::SearchClearAllStyles, "search.clearStyle.all" },
};

static const MenuItemDef kSearchJumpUpItems[] = {
    { MenuItemKind::Normal, kCmdSearchGoPrevMarker1,    &Label::SearchMarkStyle1st,    "search.jumpUp.1st" },
    { MenuItemKind::Normal, kCmdSearchGoPrevMarker2,    &Label::SearchMarkStyle2nd,    "search.jumpUp.2nd" },
    { MenuItemKind::Normal, kCmdSearchGoPrevMarker3,    &Label::SearchMarkStyle3rd,    "search.jumpUp.3rd" },
    { MenuItemKind::Normal, kCmdSearchGoPrevMarker4,    &Label::SearchMarkStyle4th,    "search.jumpUp.4th" },
    { MenuItemKind::Normal, kCmdSearchGoPrevMarker5,    &Label::SearchMarkStyle5th,    "search.jumpUp.5th" },
    { MenuItemKind::Normal, kCmdSearchGoPrevMarkerDef, &Label::SearchFindMarkStyle,   "search.jumpUp.findMarkStyle" },
};

static const MenuItemDef kSearchJumpDownItems[] = {
    { MenuItemKind::Normal, kCmdSearchGoNextMarker1,    &Label::SearchMarkStyle1st,    "search.jumpDown.1st" },
    { MenuItemKind::Normal, kCmdSearchGoNextMarker2,    &Label::SearchMarkStyle2nd,    "search.jumpDown.2nd" },
    { MenuItemKind::Normal, kCmdSearchGoNextMarker3,    &Label::SearchMarkStyle3rd,    "search.jumpDown.3rd" },
    { MenuItemKind::Normal, kCmdSearchGoNextMarker4,    &Label::SearchMarkStyle4th,    "search.jumpDown.4th" },
    { MenuItemKind::Normal, kCmdSearchGoNextMarker5,    &Label::SearchMarkStyle5th,    "search.jumpDown.5th" },
    { MenuItemKind::Normal, kCmdSearchGoNextMarkerDef, &Label::SearchFindMarkStyle,   "search.jumpDown.findMarkStyle" },
};

static const MenuItemDef kSearchCopyStyledTextItems[] = {
    { MenuItemKind::Normal, kCmdSearchStyle1ToClip,     &Label::SearchMarkStyle1st,   "search.copyStyledText.1st" },
    { MenuItemKind::Normal, kCmdSearchStyle2ToClip,     &Label::SearchMarkStyle2nd,   "search.copyStyledText.2nd" },
    { MenuItemKind::Normal, kCmdSearchStyle3ToClip,     &Label::SearchMarkStyle3rd,   "search.copyStyledText.3rd" },
    { MenuItemKind::Normal, kCmdSearchStyle4ToClip,     &Label::SearchMarkStyle4th,   "search.copyStyledText.4th" },
    { MenuItemKind::Normal, kCmdSearchStyle5ToClip,     &Label::SearchMarkStyle5th,   "search.copyStyledText.5th" },
    { MenuItemKind::Normal, kCmdSearchAllStylesToClip,  &Label::SearchAllStyles,      "search.copyStyledText.all" },
    { MenuItemKind::Normal, kCmdSearchMarkedToClip,     &Label::SearchFindMarkStyle,  "search.copyStyledText.findMarkStyle" },
};

static const MenuItemDef kSearchBookmarkItems[] = {
    { MenuItemKind::Normal, kCmdSearchToggleBookmark,     &Label::SearchToggleBookmark,      "search.bookmark.toggle", nullptr, 0, false, "Ctrl+F2" },
    { MenuItemKind::Normal, kCmdSearchNextBookmark,       &Label::SearchNextBookmark,        "search.bookmark.next", nullptr, 0, false, "F2" },
    { MenuItemKind::Normal, kCmdSearchPrevBookmark,       &Label::SearchPrevBookmark,        "search.bookmark.prev", nullptr, 0, false, "Shift+F2" },
    { MenuItemKind::Normal, kCmdSearchClearBookmarks,     &Label::SearchClearBookmarks,      "search.bookmark.clearAll" },
    { MenuItemKind::Normal, kCmdSearchCutMarkedLines,      &Label::SearchCutMarkedLines,      "search.bookmark.cutLines" },
    { MenuItemKind::Normal, kCmdSearchCopyMarkedLines,     &Label::SearchCopyMarkedLines,     "search.bookmark.copyLines" },
    { MenuItemKind::Normal, kCmdSearchPasteMarkedLines,    &Label::SearchPasteMarkedLines,    "search.bookmark.pasteLines" },
    { MenuItemKind::Normal, kCmdSearchDeleteMarkedLines,   &Label::SearchDeleteMarkedLines,   "search.bookmark.deleteLines" },
    { MenuItemKind::Normal, kCmdSearchDeleteUnmarkedLines, &Label::SearchDeleteUnmarkedLines, "search.bookmark.deleteUnmarkedLines" },
    { MenuItemKind::Normal, kCmdSearchInverseMarks,        &Label::SearchInverseMarks,        "search.bookmark.inverse" },
};

static const MenuItemDef kSearchMenuItems[] = {
    // Find / Replace core
    { MenuItemKind::Normal,  kCmdSearchFind,                  &Label::SearchFind,                 "search.find", nullptr, 0, false, "Ctrl+F" },
    { MenuItemKind::Normal,  kCmdSearchReplace,               &Label::SearchReplace,              "search.replace", nullptr, 0, false, "Ctrl+H" },
    { MenuItemKind::Normal,  kCmdSearchFindinfiles,           &Label::SearchFindInFiles,          "search.findInFiles", nullptr, 0, false, "Ctrl+Shift+F" },
    { MenuItemKind::Normal,  kCmdSearchFindIncrement,         &Label::SearchFindIncrement,        "search.findIncrement", nullptr, 0, false, "Ctrl+Alt+I" },
    { MenuItemKind::Separator },
    // Find navigation
    { MenuItemKind::Normal,  kCmdSearchFindnext,              &Label::SearchFindNext,             "search.findNext", nullptr, 0, false, "F3" },
    { MenuItemKind::Normal,  kCmdSearchFindprev,              &Label::SearchFindPrev,             "search.findPrev", nullptr, 0, false, "Shift+F3" },
    { MenuItemKind::Normal,  kCmdSearchSetAndFindNext,        &Label::SearchSetAndFindNext,       "search.setAndFindNext", nullptr, 0, false, "Ctrl+F3" },
    { MenuItemKind::Normal,  kCmdSearchSetAndFindPrev,        &Label::SearchSetAndFindPrev,       "search.setAndFindPrev", nullptr, 0, false, "Ctrl+Shift+F3" },
    { MenuItemKind::Normal,  kCmdSearchVolatileFindnext,      &Label::SearchVolatileFindNext,     "search.volatileFindNext" },
    { MenuItemKind::Normal,  kCmdSearchVolatileFindprev,      &Label::SearchVolatileFindPrev,     "search.volatileFindPrev" },
    { MenuItemKind::Separator },
    // Found-results navigation
    { MenuItemKind::Normal,  kCmdFocusOnFoundResults,         &Label::SearchFocusOnFoundResults,  "search.focusOnFoundResults", nullptr, 0, false, "F7" },
    { MenuItemKind::Normal,  kCmdSearchGotoNextFound,         &Label::SearchGotoNextFound,        "search.gotoNextFound", nullptr, 0, false, "F4" },
    { MenuItemKind::Normal,  kCmdSearchGotoPrevFound,         &Label::SearchGotoPrevFound,        "search.gotoPrevFound", nullptr, 0, false, "Shift+F4" },
    { MenuItemKind::Separator },
    // Go to
    { MenuItemKind::Normal,  kCmdSearchGotoline,              &Label::SearchGotoLine,             "search.gotoLine", nullptr, 0, false, "Ctrl+G" },
    { MenuItemKind::Normal,  kCmdSearchGotoMatchingBrace,     &Label::SearchGotoMatchingBrace,    "search.gotoMatchingBrace", nullptr, 0, false, "Ctrl+B" },
    { MenuItemKind::Normal,  kCmdSearchSelectMatchingBraces,  &Label::SearchSelectMatchingBraces, "search.selectMatchingBraces" },
    { MenuItemKind::Normal,  kCmdSearchFindCharInRange,       &Label::SearchFindCharInRange,      "search.findCharInRange" },
    { MenuItemKind::Separator },
    // Mark
    { MenuItemKind::Normal,  kCmdSearchMark,                  &Label::SearchMark,                 "search.mark" },
    { MenuItemKind::Separator },
    // Bookmarks (everyday)
    { MenuItemKind::Submenu, 0, &Label::SearchBookmark, "search.bookmark",
      kSearchBookmarkItems, WXSIZEOF(kSearchBookmarkItems) },
    { MenuItemKind::Separator },
    // Occurrence styling / mark navigation (niche)
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
    // Change history (rarest)
    { MenuItemKind::Submenu, 0, &Label::SearchChangeHistory, "search.changeHistory",
      kSearchChangeHistoryItems, WXSIZEOF(kSearchChangeHistoryItems) },
};

static const MenuDef kGoMenu = { "menu.go", &Label::MenuGo, kSearchMenuItems, WXSIZEOF(kSearchMenuItems) };
