#pragma once
#include "menu_model.h"
#include "menu_labels_edit.h"
#include "menuCmdID.h"

// ----------------------------------------------------------------- Edit
// Mechanical, zero-behavior-change port of the Edit menu from the old
// inline buildNppMainMenu() (see src/npp_menu.h). Same items, same order,
// same IDM_* ids, same labels, same shortcuts.

static const MenuItemDef kEditInsertItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_INSERT_DATETIME_SHORT,      &Label::EditInsertDateTimeShort,      "edit.insert.dateTimeShort" },
    { MenuItemKind::Normal, IDM_EDIT_INSERT_DATETIME_LONG,       &Label::EditInsertDateTimeLong,       "edit.insert.dateTimeLong" },
    { MenuItemKind::Normal, IDM_EDIT_INSERT_DATETIME_CUSTOMIZED, &Label::EditInsertDateTimeCustomized, "edit.insert.dateTimeCustomized" },
};

static const MenuItemDef kEditCopyToClipboardItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_FULLPATHTOCLIP,    &Label::EditCopyFullPathToClip,    "edit.copyToClipboard.fullPath" },
    { MenuItemKind::Normal, IDM_EDIT_FILENAMETOCLIP,    &Label::EditCopyFilenameToClip,    "edit.copyToClipboard.filename" },
    { MenuItemKind::Normal, IDM_EDIT_CURRENTDIRTOCLIP,  &Label::EditCopyCurrentDirToClip,  "edit.copyToClipboard.currentDir" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_COPY_ALL_NAMES,    &Label::EditCopyAllNames,          "edit.copyToClipboard.allNames" },
    { MenuItemKind::Normal, IDM_EDIT_COPY_ALL_PATHS,    &Label::EditCopyAllPaths,          "edit.copyToClipboard.allPaths" },
};

static const MenuItemDef kEditIndentItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_INS_TAB, &Label::EditIndentIncrease, "edit.indent.increase" },
    { MenuItemKind::Normal, IDM_EDIT_RMV_TAB, &Label::EditIndentDecrease, "edit.indent.decrease" },
};

static const MenuItemDef kEditConvertCaseToItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_UPPERCASE,          &Label::EditConvertCaseUppercase,     "edit.convertCaseTo.uppercase" },
    { MenuItemKind::Normal, IDM_EDIT_LOWERCASE,          &Label::EditConvertCaseLowercase,     "edit.convertCaseTo.lowercase" },
    { MenuItemKind::Normal, IDM_EDIT_PROPERCASE_FORCE,   &Label::EditConvertCaseProperForce,   "edit.convertCaseTo.properForce" },
    { MenuItemKind::Normal, IDM_EDIT_PROPERCASE_BLEND,   &Label::EditConvertCaseProperBlend,   "edit.convertCaseTo.properBlend" },
    { MenuItemKind::Normal, IDM_EDIT_SENTENCECASE_FORCE, &Label::EditConvertCaseSentenceForce, "edit.convertCaseTo.sentenceForce" },
    { MenuItemKind::Normal, IDM_EDIT_SENTENCECASE_BLEND, &Label::EditConvertCaseSentenceBlend, "edit.convertCaseTo.sentenceBlend" },
    { MenuItemKind::Normal, IDM_EDIT_INVERTCASE,         &Label::EditConvertCaseInvert,        "edit.convertCaseTo.invert" },
    { MenuItemKind::Normal, IDM_EDIT_RANDOMCASE,         &Label::EditConvertCaseRandom,        "edit.convertCaseTo.random" },
};

static const MenuItemDef kEditLineOperationsItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_DUP_LINE,                                 &Label::EditLineOpsDuplicateLine,                  "edit.lineOperations.duplicateLine" },
    { MenuItemKind::Normal, IDM_EDIT_REMOVE_ANY_DUP_LINES,                     &Label::EditLineOpsRemoveDupLines,                 "edit.lineOperations.removeDupLines" },
    { MenuItemKind::Normal, IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES,             &Label::EditLineOpsRemoveConsecutiveDupLines,      "edit.lineOperations.removeConsecutiveDupLines" },
    { MenuItemKind::Normal, IDM_EDIT_SPLIT_LINES,                              &Label::EditLineOpsSplitLines,                     "edit.lineOperations.splitLines" },
    { MenuItemKind::Normal, IDM_EDIT_JOIN_LINES,                               &Label::EditLineOpsJoinLines,                      "edit.lineOperations.joinLines" },
    { MenuItemKind::Normal, IDM_EDIT_LINE_UP,                                  &Label::EditLineOpsMoveUp,                         "edit.lineOperations.moveUp" },
    { MenuItemKind::Normal, IDM_EDIT_LINE_DOWN,                                &Label::EditLineOpsMoveDown,                       "edit.lineOperations.moveDown" },
    { MenuItemKind::Normal, IDM_EDIT_REMOVEEMPTYLINES,                         &Label::EditLineOpsRemoveEmptyLines,               "edit.lineOperations.removeEmptyLines" },
    { MenuItemKind::Normal, IDM_EDIT_REMOVEEMPTYLINESWITHBLANK,                &Label::EditLineOpsRemoveEmptyLinesWithBlank,      "edit.lineOperations.removeEmptyLinesWithBlank" },
    { MenuItemKind::Normal, IDM_EDIT_BLANKLINEABOVECURRENT,                    &Label::EditLineOpsBlankLineAboveCurrent,          "edit.lineOperations.blankLineAboveCurrent" },
    { MenuItemKind::Normal, IDM_EDIT_BLANKLINEBELOWCURRENT,                    &Label::EditLineOpsBlankLineBelowCurrent,          "edit.lineOperations.blankLineBelowCurrent" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_REVERSE_ORDER,                  &Label::EditLineOpsReverseOrder,                   "edit.lineOperations.reverseOrder" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_RANDOMLY,                       &Label::EditLineOpsRandomizeOrder,                 "edit.lineOperations.randomizeOrder" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING,        &Label::EditLineOpsSortLexicographicAscending,        "edit.lineOperations.sortLexicographicAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING,   &Label::EditLineOpsSortLexicoCaseInsensAscending,     "edit.lineOperations.sortLexicoCaseInsensAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LOCALE_ASCENDING,               &Label::EditLineOpsSortLocaleAscending,               "edit.lineOperations.sortLocaleAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_INTEGER_ASCENDING,              &Label::EditLineOpsSortIntegerAscending,              "edit.lineOperations.sortIntegerAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING,         &Label::EditLineOpsSortDecimalCommaAscending,         "edit.lineOperations.sortDecimalCommaAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING,           &Label::EditLineOpsSortDecimalDotAscending,           "edit.lineOperations.sortDecimalDotAscending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LENGTH_ASCENDING,               &Label::EditLineOpsSortLengthAscending,               "edit.lineOperations.sortLengthAscending" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING,       &Label::EditLineOpsSortLexicographicDescending,       "edit.lineOperations.sortLexicographicDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING,  &Label::EditLineOpsSortLexicoCaseInsensDescending,    "edit.lineOperations.sortLexicoCaseInsensDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LOCALE_DESCENDING,              &Label::EditLineOpsSortLocaleDescending,              "edit.lineOperations.sortLocaleDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_INTEGER_DESCENDING,             &Label::EditLineOpsSortIntegerDescending,             "edit.lineOperations.sortIntegerDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING,        &Label::EditLineOpsSortDecimalCommaDescending,        "edit.lineOperations.sortDecimalCommaDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING,          &Label::EditLineOpsSortDecimalDotDescending,          "edit.lineOperations.sortDecimalDotDescending" },
    { MenuItemKind::Normal, IDM_EDIT_SORTLINES_LENGTH_DESCENDING,              &Label::EditLineOpsSortLengthDescending,              "edit.lineOperations.sortLengthDescending" },
};

static const MenuItemDef kEditCommentUncommentItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_BLOCK_COMMENT,     &Label::EditCommentToggleSingle,  "edit.commentUncomment.toggleSingle" },
    { MenuItemKind::Normal, IDM_EDIT_BLOCK_COMMENT_SET, &Label::EditCommentSetSingle,     "edit.commentUncomment.setSingle" },
    { MenuItemKind::Normal, IDM_EDIT_BLOCK_UNCOMMENT,   &Label::EditCommentUnsetSingle,   "edit.commentUncomment.unsetSingle" },
    { MenuItemKind::Normal, IDM_EDIT_STREAM_COMMENT,    &Label::EditCommentBlockComment,  "edit.commentUncomment.blockComment" },
    { MenuItemKind::Normal, IDM_EDIT_STREAM_UNCOMMENT,  &Label::EditCommentBlockUncomment,"edit.commentUncomment.blockUncomment" },
};

static const MenuItemDef kEditAutoCompletionItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_AUTOCOMPLETE,             &Label::EditAutoCompleteFunction,   "edit.autoCompletion.function" },
    { MenuItemKind::Normal, IDM_EDIT_AUTOCOMPLETE_CURRENTFILE, &Label::EditAutoCompleteWord,       "edit.autoCompletion.word" },
    { MenuItemKind::Normal, IDM_EDIT_FUNCCALLTIP,              &Label::EditFuncCallTip,            "edit.autoCompletion.funcCallTip" },
    { MenuItemKind::Normal, IDM_EDIT_FUNCCALLTIP_PREVIOUS,     &Label::EditFuncCallTipPrevious,    "edit.autoCompletion.funcCallTipPrevious" },
    { MenuItemKind::Normal, IDM_EDIT_FUNCCALLTIP_NEXT,         &Label::EditFuncCallTipNext,        "edit.autoCompletion.funcCallTipNext" },
    { MenuItemKind::Normal, IDM_EDIT_AUTOCOMPLETE_PATH,        &Label::EditAutoCompletePath,       "edit.autoCompletion.path" },
};

static const MenuItemDef kEditEolConversionItems[] = {
    { MenuItemKind::Normal, IDM_FORMAT_TODOS,  &Label::EditEolToWindows, "edit.eolConversion.windows" },
    { MenuItemKind::Normal, IDM_FORMAT_TOUNIX, &Label::EditEolToUnix,   "edit.eolConversion.unix" },
    { MenuItemKind::Normal, IDM_FORMAT_TOMAC,  &Label::EditEolToMac,    "edit.eolConversion.mac" },
};

static const MenuItemDef kEditBlankOperationsItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_TRIMTRAILING,   &Label::EditBlankTrimTrailing,     "edit.blankOperations.trimTrailing" },
    { MenuItemKind::Normal, IDM_EDIT_TRIMLINEHEAD,   &Label::EditBlankTrimLineHead,     "edit.blankOperations.trimLineHead" },
    { MenuItemKind::Normal, IDM_EDIT_TRIM_BOTH,      &Label::EditBlankTrimBoth,         "edit.blankOperations.trimBoth" },
    { MenuItemKind::Normal, IDM_EDIT_EOL2WS,         &Label::EditBlankEolToSpace,       "edit.blankOperations.eolToSpace" },
    { MenuItemKind::Normal, IDM_EDIT_TRIMALL,        &Label::EditBlankTrimAll,          "edit.blankOperations.trimAll" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_TAB2SW,         &Label::EditBlankTabToSpace,       "edit.blankOperations.tabToSpace" },
    { MenuItemKind::Normal, IDM_EDIT_SW2TAB_ALL,     &Label::EditBlankSpaceToTabAll,    "edit.blankOperations.spaceToTabAll" },
    { MenuItemKind::Normal, IDM_EDIT_SW2TAB_LEADING, &Label::EditBlankSpaceToTabLeading,"edit.blankOperations.spaceToTabLeading" },
};

static const MenuItemDef kEditPasteSpecialItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_PASTE_AS_HTML, &Label::EditPasteAsHtml, "edit.pasteSpecial.asHtml" },
    { MenuItemKind::Normal, IDM_EDIT_PASTE_AS_RTF,  &Label::EditPasteAsRtf,  "edit.pasteSpecial.asRtf" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_COPY_BINARY,   &Label::EditCopyBinary,  "edit.pasteSpecial.copyBinary" },
    { MenuItemKind::Normal, IDM_EDIT_CUT_BINARY,    &Label::EditCutBinary,   "edit.pasteSpecial.cutBinary" },
    { MenuItemKind::Normal, IDM_EDIT_PASTE_BINARY,  &Label::EditPasteBinary, "edit.pasteSpecial.pasteBinary" },
};

static const MenuItemDef kEditOnSelectionItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_OPENSELECTEDFILETOEDIT,                 &Label::EditOpenSelectedFileToEdit,               "edit.onSelection.openFile" },
    { MenuItemKind::Normal, IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER,       &Label::EditOpenSelectedFileFolderInExplorer,     "edit.onSelection.openContainingFolder" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_REDACT_SELECTION,                      &Label::EditRedactSelection,                      "edit.onSelection.redactSelection" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, IDM_EDIT_SEARCHONINTERNET,                      &Label::EditSearchOnInternet,                     "edit.onSelection.searchOnInternet" },
    { MenuItemKind::Normal, IDM_EDIT_CHANGESEARCHENGINE,                    &Label::EditChangeSearchEngine,                   "edit.onSelection.changeSearchEngine" },
};

static const MenuItemDef kEditMultiselectAllItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTALL,                   &Label::EditMultiselectIgnoreCaseWholeWord,  "edit.multiselectAll.ignoreCaseWholeWord" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTALLMATCHCASE,          &Label::EditMultiselectMatchCaseOnly,        "edit.multiselectAll.matchCaseOnly" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTALLWHOLEWORD,          &Label::EditMultiselectMatchWholeWordOnly,   "edit.multiselectAll.matchWholeWordOnly" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD, &Label::EditMultiselectMatchCaseWholeWord,   "edit.multiselectAll.matchCaseWholeWord" },
};

static const MenuItemDef kEditMultiselectNextItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTNEXT,                   &Label::EditMultiselectIgnoreCaseWholeWord,  "edit.multiselectNext.ignoreCaseWholeWord" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTNEXTMATCHCASE,          &Label::EditMultiselectMatchCaseOnly,        "edit.multiselectNext.matchCaseOnly" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTNEXTWHOLEWORD,          &Label::EditMultiselectMatchWholeWordOnly,   "edit.multiselectNext.matchWholeWordOnly" },
    { MenuItemKind::Normal, IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD, &Label::EditMultiselectMatchCaseWholeWord,   "edit.multiselectNext.matchCaseWholeWord" },
};

static const MenuItemDef kEditReadOnlyItems[] = {
    { MenuItemKind::Normal, IDM_EDIT_TOGGLEREADONLY,          &Label::EditToggleReadOnly,          "edit.readOnly.toggleCurrent" },
    { MenuItemKind::Normal, IDM_EDIT_SETREADONLYFORALLDOCS,   &Label::EditSetReadOnlyForAllDocs,   "edit.readOnly.setForAllDocs" },
    { MenuItemKind::Normal, IDM_EDIT_CLEARREADONLYFORALLDOCS,&Label::EditClearReadOnlyForAllDocs, "edit.readOnly.clearForAllDocs" },
};

static const MenuItemDef kEditMenuItems[] = {
    { MenuItemKind::Normal,  IDM_EDIT_UNDO,                     &Label::EditUndo,                     "edit.undo" },
    { MenuItemKind::Normal,  IDM_EDIT_REDO,                     &Label::EditRedo,                     "edit.redo" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_EDIT_CUT,                      &Label::EditCut,                      "edit.cut" },
    { MenuItemKind::Normal,  IDM_EDIT_COPY,                     &Label::EditCopy,                     "edit.copy" },
    { MenuItemKind::Normal,  IDM_EDIT_PASTE,                    &Label::EditPaste,                    "edit.paste" },
    { MenuItemKind::Normal,  IDM_EDIT_DELETE,                   &Label::EditDelete,                   "edit.delete" },
    { MenuItemKind::Normal,  IDM_EDIT_SELECTALL,                &Label::EditSelectAll,                "edit.selectAll" },
    { MenuItemKind::Normal,  IDM_EDIT_BEGINENDSELECT,           &Label::EditBeginEndSelect,            "edit.beginEndSelect" },
    { MenuItemKind::Normal,  IDM_EDIT_BEGINENDSELECT_COLUMNMODE,&Label::EditBeginEndSelectColumnMode,  "edit.beginEndSelectColumnMode" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditInsert, "edit.insert",
      kEditInsertItems, WXSIZEOF(kEditInsertItems) },
    { MenuItemKind::Submenu, 0, &Label::EditCopyToClipboard, "edit.copyToClipboard",
      kEditCopyToClipboardItems, WXSIZEOF(kEditCopyToClipboardItems) },
    { MenuItemKind::Submenu, 0, &Label::EditIndent, "edit.indent",
      kEditIndentItems, WXSIZEOF(kEditIndentItems) },
    { MenuItemKind::Submenu, 0, &Label::EditConvertCaseTo, "edit.convertCaseTo",
      kEditConvertCaseToItems, WXSIZEOF(kEditConvertCaseToItems) },
    { MenuItemKind::Submenu, 0, &Label::EditLineOperations, "edit.lineOperations",
      kEditLineOperationsItems, WXSIZEOF(kEditLineOperationsItems) },
    { MenuItemKind::Submenu, 0, &Label::EditCommentUncomment, "edit.commentUncomment",
      kEditCommentUncommentItems, WXSIZEOF(kEditCommentUncommentItems) },
    { MenuItemKind::Submenu, 0, &Label::EditAutoCompletion, "edit.autoCompletion",
      kEditAutoCompletionItems, WXSIZEOF(kEditAutoCompletionItems) },
    { MenuItemKind::Submenu, 0, &Label::EditEolConversion, "edit.eolConversion",
      kEditEolConversionItems, WXSIZEOF(kEditEolConversionItems) },
    { MenuItemKind::Submenu, 0, &Label::EditBlankOperations, "edit.blankOperations",
      kEditBlankOperationsItems, WXSIZEOF(kEditBlankOperationsItems) },
    { MenuItemKind::Submenu, 0, &Label::EditPasteSpecial, "edit.pasteSpecial",
      kEditPasteSpecialItems, WXSIZEOF(kEditPasteSpecialItems) },
    { MenuItemKind::Submenu, 0, &Label::EditOnSelection, "edit.onSelection",
      kEditOnSelectionItems, WXSIZEOF(kEditOnSelectionItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectAll, "edit.multiselectAll",
      kEditMultiselectAllItems, WXSIZEOF(kEditMultiselectAllItems) },
    { MenuItemKind::Submenu, 0, &Label::EditMultiselectNext, "edit.multiselectNext",
      kEditMultiselectNextItems, WXSIZEOF(kEditMultiselectNextItems) },
    { MenuItemKind::Normal,  IDM_EDIT_MULTISELECTUNDO,          &Label::EditMultiselectUndo,           "edit.multiselectUndo" },
    { MenuItemKind::Normal,  IDM_EDIT_MULTISELECTSSKIP,         &Label::EditMultiselectSkip,           "edit.multiselectSkip" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  IDM_EDIT_COLUMNMODETIP,            &Label::EditColumnModeTip,             "edit.columnModeTip" },
    { MenuItemKind::Normal,  IDM_EDIT_COLUMNMODE,               &Label::EditColumnMode,                "edit.columnMode" },
    { MenuItemKind::Normal,  IDM_EDIT_CHAR_PANEL,               &Label::EditCharPanel,                 "edit.charPanel" },
    { MenuItemKind::Normal,  IDM_EDIT_CLIPBOARDHISTORY_PANEL,   &Label::EditClipboardHistoryPanel,     "edit.clipboardHistoryPanel" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditReadOnly, "edit.readOnly",
      kEditReadOnlyItems, WXSIZEOF(kEditReadOnlyItems) },
    { MenuItemKind::Normal,  IDM_EDIT_TOGGLESYSTEMREADONLY,     &Label::EditToggleSystemReadOnly,      "edit.toggleSystemReadOnly" },
};

static const MenuDef kEditMenu = { "menu.edit", &Label::MenuEdit, kEditMenuItems, WXSIZEOF(kEditMenuItems) };
