#pragma once
#include "menu_model.h"
#include "menu_labels_edit.h"
#include "command_ids.h"

// ----------------------------------------------------------------- Edit
// wxNote's Edit menu, grouped by editing frequency and shared intent.
// The transform submenus lead in everyday-use order: Convert Case To,
// Comment/Uncomment and Indent come first, then line- and blank-reshaping,
// paste and completion helpers, with EOL / date-insert / clipboard utilities
// trailing. Column Editor and the Read-Only controls form the final clusters.
// Within each sub-array, canonical sequences (case, EOL, sort asc/desc) keep
// their standard order; non-canonical lists are affinity-clustered by object.

static const MenuItemDef kEditInsertItems[] = {
    { MenuItemKind::Normal, kCmdEditInsertDatetimeShort,      &Label::EditInsertDateTimeShort,      "edit.insert.dateTimeShort" },
    { MenuItemKind::Normal, kCmdEditInsertDatetimeLong,       &Label::EditInsertDateTimeLong,       "edit.insert.dateTimeLong" },
    { MenuItemKind::Normal, kCmdEditInsertDatetimeCustomized, &Label::EditInsertDateTimeCustomized, "edit.insert.dateTimeCustomized" },
};

static const MenuItemDef kEditCopyToClipboardItems[] = {
    { MenuItemKind::Normal, kCmdEditFullPathToClip,    &Label::EditCopyFullPathToClip,    "edit.copyToClipboard.fullPath" },
    { MenuItemKind::Normal, kCmdEditFileNameToClip,    &Label::EditCopyFilenameToClip,    "edit.copyToClipboard.filename" },
    { MenuItemKind::Normal, kCmdEditCurrentDirToClip,  &Label::EditCopyCurrentDirToClip,  "edit.copyToClipboard.currentDir" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditCopyAllNames,    &Label::EditCopyAllNames,          "edit.copyToClipboard.allNames" },
    { MenuItemKind::Normal, kCmdEditCopyAllPaths,    &Label::EditCopyAllPaths,          "edit.copyToClipboard.allPaths" },
};

// Ctrl+] / Ctrl+[ are the strong-4 consensus indent/outdent chords (VSCode/TextMate/Sublime/Pulsar);
// Tab / Shift+Tab keep working regardless (Scintilla's own SCI_TAB/SCI_BACKTAB). These menu accels
// shadow Scintilla's stock paragraph-up/down defaults, so the curated editor rows editor.paragraphUp/
// paragraphDown are unbound-by-default now (shortcut_labels.h; both stay remappable).
static const MenuItemDef kEditIndentItems[] = {
    { MenuItemKind::Normal, kCmdEditInsTab, &Label::EditIndentIncrease, "edit.indent.increase", nullptr, 0, false, "Ctrl+]" },
    { MenuItemKind::Normal, kCmdEditRmvTab, &Label::EditIndentDecrease, "edit.indent.decrease", nullptr, 0, false, "Ctrl+[" },
};

// Uppercase/lowercase lead (CUA); the proper/sentence/invert/random group follows.
static const MenuItemDef kEditConvertCaseToItems[] = {
    { MenuItemKind::Normal, kCmdEditUppercase,          &Label::EditConvertCaseUppercase,     "edit.convertCaseTo.uppercase", nullptr, 0, false, "Ctrl+Shift+U" },
    { MenuItemKind::Normal, kCmdEditLowercase,          &Label::EditConvertCaseLowercase,     "edit.convertCaseTo.lowercase", nullptr, 0, false, "Ctrl+U" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditPropercaseForce,   &Label::EditConvertCaseProperForce,   "edit.convertCaseTo.properForce" },
    { MenuItemKind::Normal, kCmdEditPropercaseBlend,   &Label::EditConvertCaseProperBlend,   "edit.convertCaseTo.properBlend" },
    { MenuItemKind::Normal, kCmdEditSentenceCaseForce, &Label::EditConvertCaseSentenceForce, "edit.convertCaseTo.sentenceForce" },
    { MenuItemKind::Normal, kCmdEditSentenceCaseBlend, &Label::EditConvertCaseSentenceBlend, "edit.convertCaseTo.sentenceBlend" },
    { MenuItemKind::Normal, kCmdEditInvertcase,         &Label::EditConvertCaseInvert,        "edit.convertCaseTo.invert" },
    { MenuItemKind::Normal, kCmdEditRandomcase,         &Label::EditConvertCaseRandom,        "edit.convertCaseTo.random" },
};

// Clustered by intent: move/duplicate, then remove, then blank-line insertion,
// then whole-list reordering, then the sort ascending and sort descending blocks
// (each block keeps its canonical lexicographic->length order).
static const MenuItemDef kEditLineOperationsItems[] = {
    // The consensus Ctrl+D fork's modal resolution: Ctrl+D = add-next-occurrence multi-select (see
    // kEditMultiselectNextItems below - the VSCode/Sublime/Pulsar convention), duplicate-line moves to
    // Ctrl+Shift+D (TextMate/Sublime/Pulsar - the camp that reserves Ctrl+D for multi-cursor).
    { MenuItemKind::Normal, kCmdEditDupLine,                                 &Label::EditLineOpsDuplicateLine,                  "edit.lineOperations.duplicateLine", nullptr, 0, false, "Ctrl+Shift+D" },
    { MenuItemKind::Normal, kCmdEditLineUp,                                  &Label::EditLineOpsMoveUp,                         "edit.lineOperations.moveUp", nullptr, 0, false, "Ctrl+Shift+Up" },
    { MenuItemKind::Normal, kCmdEditLineDown,                                &Label::EditLineOpsMoveDown,                       "edit.lineOperations.moveDown", nullptr, 0, false, "Ctrl+Shift+Down" },
    { MenuItemKind::Normal, kCmdEditSplitLines,                              &Label::EditLineOpsSplitLines,                     "edit.lineOperations.splitLines", nullptr, 0, false, "Ctrl+I" },
    { MenuItemKind::Normal, kCmdEditJoinLines,                               &Label::EditLineOpsJoinLines,                      "edit.lineOperations.joinLines", nullptr, 0, false, "Ctrl+J" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditRemoveAnyDupLines,                     &Label::EditLineOpsRemoveDupLines,                 "edit.lineOperations.removeDupLines" },
    { MenuItemKind::Normal, kCmdEditRemoveConsecutiveDupLines,             &Label::EditLineOpsRemoveConsecutiveDupLines,      "edit.lineOperations.removeConsecutiveDupLines" },
    { MenuItemKind::Normal, kCmdEditRemoveEmptyLines,                         &Label::EditLineOpsRemoveEmptyLines,               "edit.lineOperations.removeEmptyLines" },
    { MenuItemKind::Normal, kCmdEditRemoveEmptyLinesWithBlank,                &Label::EditLineOpsRemoveEmptyLinesWithBlank,      "edit.lineOperations.removeEmptyLinesWithBlank" },
    { MenuItemKind::Separator },
    // Insert-line-below is Ctrl+Enter in 4 of 6 surveyed editors (VSCode/TextMate/Sublime/Pulsar) -
    // adopted on the nearest wxNote command. Insert-line-above stays keyless: its consensus is split
    // (Ctrl+Shift+Enter 3 vs Ctrl+Alt+Enter 2).
    { MenuItemKind::Normal, kCmdEditBlankLineAboveCurrent,                    &Label::EditLineOpsBlankLineAboveCurrent,          "edit.lineOperations.blankLineAboveCurrent" },
    { MenuItemKind::Normal, kCmdEditBlankLineBelowCurrent,                    &Label::EditLineOpsBlankLineBelowCurrent,          "edit.lineOperations.blankLineBelowCurrent", nullptr, 0, false, "Ctrl+Enter" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditSortlinesReverseOrder,                  &Label::EditLineOpsReverseOrder,                   "edit.lineOperations.reverseOrder" },
    { MenuItemKind::Normal, kCmdEditSortlinesRandomly,                       &Label::EditLineOpsRandomizeOrder,                 "edit.lineOperations.randomizeOrder" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditSortlinesLexicographicAscending,        &Label::EditLineOpsSortLexicographicAscending,        "edit.lineOperations.sortLexicographicAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLexicoCaseInsensAscending,   &Label::EditLineOpsSortLexicoCaseInsensAscending,     "edit.lineOperations.sortLexicoCaseInsensAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLocaleAscending,               &Label::EditLineOpsSortLocaleAscending,               "edit.lineOperations.sortLocaleAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesIntegerAscending,              &Label::EditLineOpsSortIntegerAscending,              "edit.lineOperations.sortIntegerAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesDecimalCommaAscending,         &Label::EditLineOpsSortDecimalCommaAscending,         "edit.lineOperations.sortDecimalCommaAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesDecimaldotAscending,           &Label::EditLineOpsSortDecimalDotAscending,           "edit.lineOperations.sortDecimalDotAscending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLengthAscending,               &Label::EditLineOpsSortLengthAscending,               "edit.lineOperations.sortLengthAscending" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditSortlinesLexicographicDescending,       &Label::EditLineOpsSortLexicographicDescending,       "edit.lineOperations.sortLexicographicDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLexicoCaseInsensDescending,  &Label::EditLineOpsSortLexicoCaseInsensDescending,    "edit.lineOperations.sortLexicoCaseInsensDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLocaleDescending,              &Label::EditLineOpsSortLocaleDescending,              "edit.lineOperations.sortLocaleDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesIntegerDescending,             &Label::EditLineOpsSortIntegerDescending,             "edit.lineOperations.sortIntegerDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesDecimalCommaDescending,        &Label::EditLineOpsSortDecimalCommaDescending,        "edit.lineOperations.sortDecimalCommaDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesDecimaldotDescending,          &Label::EditLineOpsSortDecimalDotDescending,          "edit.lineOperations.sortDecimalDotDescending" },
    { MenuItemKind::Normal, kCmdEditSortlinesLengthDescending,              &Label::EditLineOpsSortLengthDescending,              "edit.lineOperations.sortLengthDescending" },
};

static const MenuItemDef kEditCommentUncommentItems[] = {
    // Ctrl+/ is the ONE non-CUA editing chord all 6 surveyed editors agree on (VSCode/IntelliJ/TextMate/
    // Notepad4/Sublime/Pulsar) - adopted over the N++-lineage Ctrl+Q. The menu accel shadows Scintilla's
    // stock Ctrl+/ word-part-left, so editor.wordPartLeft is unbound-by-default now (shortcut_labels.h).
    { MenuItemKind::Normal, kCmdEditBlockComment,     &Label::EditCommentToggleSingle,  "edit.commentUncomment.toggleSingle", nullptr, 0, false, "Ctrl+/" },
    { MenuItemKind::Normal, kCmdEditBlockCommentSet, &Label::EditCommentSetSingle,     "edit.commentUncomment.setSingle" },
    { MenuItemKind::Normal, kCmdEditBlockUncomment,   &Label::EditCommentUnsetSingle,   "edit.commentUncomment.unsetSingle" },
    { MenuItemKind::Normal, kCmdEditStreamComment,    &Label::EditCommentBlockComment,  "edit.commentUncomment.blockComment", nullptr, 0, false, "Ctrl+Shift+Q" },
    { MenuItemKind::Normal, kCmdEditStreamUncomment,  &Label::EditCommentBlockUncomment,"edit.commentUncomment.blockUncomment" },
};

static const MenuItemDef kEditAutoCompletionItems[] = {
    { MenuItemKind::Normal, kCmdEditAutoComplete,             &Label::EditAutoCompleteFunction,   "edit.autoCompletion.function", nullptr, 0, false, "Ctrl+Space" },
    { MenuItemKind::Normal, kCmdEditAutoCompleteCurrentfile, &Label::EditAutoCompleteWord,       "edit.autoCompletion.word" },
    { MenuItemKind::Normal, kCmdEditFunccalltip,              &Label::EditFuncCallTip,            "edit.autoCompletion.funcCallTip" },
    { MenuItemKind::Normal, kCmdEditFunccalltipPrevious,     &Label::EditFuncCallTipPrevious,    "edit.autoCompletion.funcCallTipPrevious" },
    { MenuItemKind::Normal, kCmdEditFunccalltipNext,         &Label::EditFuncCallTipNext,        "edit.autoCompletion.funcCallTipNext" },
    { MenuItemKind::Normal, kCmdEditAutoCompletePath,        &Label::EditAutoCompletePath,       "edit.autoCompletion.path" },
};

static const MenuItemDef kEditEolConversionItems[] = {
    { MenuItemKind::Normal, kCmdFormatTodos,  &Label::EditEolToWindows, "edit.eolConversion.windows" },
    { MenuItemKind::Normal, kCmdFormatTounix, &Label::EditEolToUnix,   "edit.eolConversion.unix" },
    { MenuItemKind::Normal, kCmdFormatTomac,  &Label::EditEolToMac,    "edit.eolConversion.mac" },
};

static const MenuItemDef kEditBlankOperationsItems[] = {
    { MenuItemKind::Normal, kCmdEditTrimTrailing,   &Label::EditBlankTrimTrailing,     "edit.blankOperations.trimTrailing" },
    { MenuItemKind::Normal, kCmdEditTrimLineHead,   &Label::EditBlankTrimLineHead,     "edit.blankOperations.trimLineHead" },
    { MenuItemKind::Normal, kCmdEditTrimBoth,      &Label::EditBlankTrimBoth,         "edit.blankOperations.trimBoth" },
    { MenuItemKind::Normal, kCmdEditEol2ws,         &Label::EditBlankEolToSpace,       "edit.blankOperations.eolToSpace" },
    { MenuItemKind::Normal, kCmdEditTrimall,        &Label::EditBlankTrimAll,          "edit.blankOperations.trimAll" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditTab2sw,         &Label::EditBlankTabToSpace,       "edit.blankOperations.tabToSpace" },
    { MenuItemKind::Normal, kCmdEditSw2tabAll,     &Label::EditBlankSpaceToTabAll,    "edit.blankOperations.spaceToTabAll" },
    { MenuItemKind::Normal, kCmdEditSw2tabLeading, &Label::EditBlankSpaceToTabLeading,"edit.blankOperations.spaceToTabLeading" },
};

static const MenuItemDef kEditPasteSpecialItems[] = {
    { MenuItemKind::Normal, kCmdEditPasteAsHtml, &Label::EditPasteAsHtml, "edit.pasteSpecial.asHtml" },
    { MenuItemKind::Normal, kCmdEditPasteAsRtf,  &Label::EditPasteAsRtf,  "edit.pasteSpecial.asRtf" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditCopyBinary,   &Label::EditCopyBinary,  "edit.pasteSpecial.copyBinary" },
    { MenuItemKind::Normal, kCmdEditCutBinary,    &Label::EditCutBinary,   "edit.pasteSpecial.cutBinary" },
    { MenuItemKind::Normal, kCmdEditPasteBinary,  &Label::EditPasteBinary, "edit.pasteSpecial.pasteBinary" },
};

static const MenuItemDef kEditOnSelectionItems[] = {
    { MenuItemKind::Normal, kCmdEditOpenSelectedFileToEdit,                 &Label::EditOpenSelectedFileToEdit,               "edit.onSelection.openFile" },
    { MenuItemKind::Normal, kCmdEditOpenSelectedFileFolderInExplorer,       &Label::EditOpenSelectedFileFolderInExplorer,     "edit.onSelection.openContainingFolder" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditRedactSelection,                      &Label::EditRedactSelection,                      "edit.onSelection.redactSelection" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal, kCmdEditSearchOnInternet,                      &Label::EditSearchOnInternet,                     "edit.onSelection.searchOnInternet" },
    { MenuItemKind::Normal, kCmdEditChangeSearchEngine,                    &Label::EditChangeSearchEngine,                   "edit.onSelection.changeSearchEngine" },
};

static const MenuItemDef kEditMultiselectAllItems[] = {
    { MenuItemKind::Normal, kCmdEditMultiSelectAll,                   &Label::EditMultiselectIgnoreCaseWholeWord,  "edit.multiselectAll.ignoreCaseWholeWord" },
    { MenuItemKind::Normal, kCmdEditMultiSelectAllMatchCase,          &Label::EditMultiselectMatchCaseOnly,        "edit.multiselectAll.matchCaseOnly" },
    { MenuItemKind::Normal, kCmdEditMultiSelectAllWholeWord,          &Label::EditMultiselectMatchWholeWordOnly,   "edit.multiselectAll.matchWholeWordOnly" },
    { MenuItemKind::Normal, kCmdEditMultiSelectAllMatchCaseWholeWord, &Label::EditMultiselectMatchCaseWholeWord,   "edit.multiselectAll.matchCaseWholeWord" },
};

static const MenuItemDef kEditMultiselectNextItems[] = {
    // Ctrl+D = add-next-occurrence-to-selection (VSCode/Sublime/Pulsar - the modern-trio convention; the
    // other half of the duplicate-line fork, see kEditLineOperationsItems). Bound on the plain
    // ignoring-case-and-whole-word variant, the closest match to those editors' default find state.
    { MenuItemKind::Normal, kCmdEditMultiSelectNext,                   &Label::EditMultiselectIgnoreCaseWholeWord,  "edit.multiselectNext.ignoreCaseWholeWord", nullptr, 0, false, "Ctrl+D" },
    { MenuItemKind::Normal, kCmdEditMultiSelectNextMatchCase,          &Label::EditMultiselectMatchCaseOnly,        "edit.multiselectNext.matchCaseOnly" },
    { MenuItemKind::Normal, kCmdEditMultiSelectNextWholeWord,          &Label::EditMultiselectMatchWholeWordOnly,   "edit.multiselectNext.matchWholeWordOnly" },
    { MenuItemKind::Normal, kCmdEditMultiSelectNextMatchCaseWholeWord, &Label::EditMultiselectMatchCaseWholeWord,   "edit.multiselectNext.matchCaseWholeWord" },
};

static const MenuItemDef kEditReadOnlyItems[] = {
    { MenuItemKind::Normal, kCmdEditToggleReadOnly,          &Label::EditToggleReadOnly,          "edit.readOnly.toggleCurrent" },
    { MenuItemKind::Normal, kCmdEditSetReadOnlyForAllDocs,   &Label::EditSetReadOnlyForAllDocs,   "edit.readOnly.setForAllDocs" },
    { MenuItemKind::Normal, kCmdEditClearReadOnlyForAllDocs,&Label::EditClearReadOnlyForAllDocs, "edit.readOnly.clearForAllDocs" },
};

static const MenuItemDef kEditMenuItems[] = {
    { MenuItemKind::Normal,  kCmdEditUndo,                     &Label::EditUndo,                     "edit.undo", nullptr, 0, false, "Ctrl+Z" },
    // Redo also answers to Ctrl+Shift+Z as a SECONDARY default - the only redo chord bound in all 6
    // surveyed editors (seeded by menu_builder.h's kSecondaryDefaults; the label shows Ctrl+Y).
    { MenuItemKind::Normal,  kCmdEditRedo,                     &Label::EditRedo,                     "edit.redo", nullptr, 0, false, "Ctrl+Y" },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  kCmdEditCut,                      &Label::EditCut,                      "edit.cut", nullptr, 0, false, "Ctrl+X" },
    { MenuItemKind::Normal,  kCmdEditCopy,                     &Label::EditCopy,                     "edit.copy", nullptr, 0, false, "Ctrl+C" },
    { MenuItemKind::Normal,  kCmdEditPaste,                    &Label::EditPaste,                    "edit.paste", nullptr, 0, false, "Ctrl+V" },
    { MenuItemKind::Normal,  kCmdEditDelete,                   &Label::EditDelete,                   "edit.delete", nullptr, 0, false, "Del" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditConvertCaseTo, "edit.convertCaseTo",
      kEditConvertCaseToItems, WXSIZEOF(kEditConvertCaseToItems) },
    { MenuItemKind::Submenu, 0, &Label::EditCommentUncomment, "edit.commentUncomment",
      kEditCommentUncommentItems, WXSIZEOF(kEditCommentUncommentItems) },
    { MenuItemKind::Submenu, 0, &Label::EditIndent, "edit.indent",
      kEditIndentItems, WXSIZEOF(kEditIndentItems) },
    { MenuItemKind::Submenu, 0, &Label::EditLineOperations, "edit.lineOperations",
      kEditLineOperationsItems, WXSIZEOF(kEditLineOperationsItems) },
    { MenuItemKind::Submenu, 0, &Label::EditBlankOperations, "edit.blankOperations",
      kEditBlankOperationsItems, WXSIZEOF(kEditBlankOperationsItems) },
    { MenuItemKind::Submenu, 0, &Label::EditPasteSpecial, "edit.pasteSpecial",
      kEditPasteSpecialItems, WXSIZEOF(kEditPasteSpecialItems) },
    { MenuItemKind::Submenu, 0, &Label::EditAutoCompletion, "edit.autoCompletion",
      kEditAutoCompletionItems, WXSIZEOF(kEditAutoCompletionItems) },
    { MenuItemKind::Submenu, 0, &Label::EditEolConversion, "edit.eolConversion",
      kEditEolConversionItems, WXSIZEOF(kEditEolConversionItems) },
    { MenuItemKind::Submenu, 0, &Label::EditInsert, "edit.insert",
      kEditInsertItems, WXSIZEOF(kEditInsertItems) },
    { MenuItemKind::Submenu, 0, &Label::EditCopyToClipboard, "edit.copyToClipboard",
      kEditCopyToClipboardItems, WXSIZEOF(kEditCopyToClipboardItems) },
    { MenuItemKind::Separator },
    { MenuItemKind::Normal,  kCmdEditColumnModeTip,            &Label::EditColumnModeTip,             "edit.columnModeTip" },
    { MenuItemKind::Normal,  kCmdEditColumnmode,               &Label::EditColumnMode,                "edit.columnMode", nullptr, 0, false, "Alt+C" },
    { MenuItemKind::Separator },
    { MenuItemKind::Submenu, 0, &Label::EditReadOnly, "edit.readOnly",
      kEditReadOnlyItems, WXSIZEOF(kEditReadOnlyItems) },
    { MenuItemKind::Normal,  kCmdEditToggleSystemReadOnly,     &Label::EditToggleSystemReadOnly,      "edit.toggleSystemReadOnly" },
};

static const MenuDef kEditMenu = { "menu.edit", &Label::MenuEdit, kEditMenuItems, WXSIZEOF(kEditMenuItems) };
