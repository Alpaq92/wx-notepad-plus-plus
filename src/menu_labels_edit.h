#pragma once
#include <wx/intl.h>

namespace Label
{
    inline const wxString MenuEdit() { return _("&Edit"); }

    inline const wxString EditUndo() { return _("&Undo\tCtrl+Z"); }
    inline const wxString EditRedo() { return _("&Redo\tCtrl+Y"); }
    inline const wxString EditCut() { return _("Cu&t\tCtrl+X"); }
    inline const wxString EditCopy() { return _("&Copy\tCtrl+C"); }
    inline const wxString EditPaste() { return _("&Paste\tCtrl+V"); }
    inline const wxString EditDelete() { return _("&Delete\tDel"); }
    inline const wxString EditSelectAll() { return _("&Select All\tCtrl+A"); }
    inline const wxString EditBeginEndSelect() { return _("Begin/End &Select"); }
    inline const wxString EditBeginEndSelectColumnMode() { return _("Begin/End Select in Column Mode"); }

    inline const wxString EditInsert() { return _("Insert"); }
    inline const wxString EditInsertDateTimeShort() { return _("Date Time (short)"); }
    inline const wxString EditInsertDateTimeLong() { return _("Date Time (long)"); }
    inline const wxString EditInsertDateTimeCustomized() { return _("Date Time (customized)"); }

    inline const wxString EditCopyToClipboard() { return _("Cop&y to Clipboard"); }
    inline const wxString EditCopyFullPathToClip() { return _("Copy Current Full File path"); }
    inline const wxString EditCopyFilenameToClip() { return _("Copy Current Filename"); }
    inline const wxString EditCopyCurrentDirToClip() { return _("Copy Current Dir. Path"); }
    inline const wxString EditCopyAllNames() { return _("Copy All Filenames"); }
    inline const wxString EditCopyAllPaths() { return _("Copy All File Paths"); }

    inline const wxString EditIndent() { return _("&Indent"); }
    inline const wxString EditIndentIncrease() { return _("Increase Line Indent"); }
    inline const wxString EditIndentDecrease() { return _("Decrease Line Indent"); }

    inline const wxString EditConvertCaseTo() { return _("Con&vert Case to"); }
    inline const wxString EditConvertCaseUppercase() { return _("&UPPERCASE\tCtrl+Shift+U"); }
    inline const wxString EditConvertCaseLowercase() { return _("&lowercase\tCtrl+U"); }
    inline const wxString EditConvertCaseProperForce() { return _("&Proper Case"); }
    inline const wxString EditConvertCaseProperBlend() { return _("Proper Case (blend)"); }
    inline const wxString EditConvertCaseSentenceForce() { return _("&Sentence case"); }
    inline const wxString EditConvertCaseSentenceBlend() { return _("Sentence case (blend)"); }
    inline const wxString EditConvertCaseInvert() { return _("&iNVERT cASE"); }
    inline const wxString EditConvertCaseRandom() { return _("&ranDOm CasE"); }

    inline const wxString EditLineOperations() { return _("&Line Operations"); }
    inline const wxString EditLineOpsDuplicateLine() { return _("Duplicate Current Line\tCtrl+D"); }
    inline const wxString EditLineOpsRemoveDupLines() { return _("Remove Duplicate Lines"); }
    inline const wxString EditLineOpsRemoveConsecutiveDupLines() { return _("Remove Consecutive Duplicate Lines"); }
    inline const wxString EditLineOpsSplitLines() { return _("Split Lines\tCtrl+I"); }
    inline const wxString EditLineOpsJoinLines() { return _("Join Lines\tCtrl+J"); }
    inline const wxString EditLineOpsMoveUp() { return _("Move Up Current Line\tCtrl+Shift+Up"); }
    inline const wxString EditLineOpsMoveDown() { return _("Move Down Current Line\tCtrl+Shift+Down"); }
    inline const wxString EditLineOpsRemoveEmptyLines() { return _("Remove Empty Lines"); }
    inline const wxString EditLineOpsRemoveEmptyLinesWithBlank() { return _("Remove Empty Lines (Containing Blank characters)"); }
    inline const wxString EditLineOpsBlankLineAboveCurrent() { return _("Insert Blank Line Above Current"); }
    inline const wxString EditLineOpsBlankLineBelowCurrent() { return _("Insert Blank Line Below Current"); }
    inline const wxString EditLineOpsReverseOrder() { return _("Reverse Line Order"); }
    inline const wxString EditLineOpsRandomizeOrder() { return _("Randomize Line Order"); }
    inline const wxString EditLineOpsSortLexicographicAscending() { return _("Sort Lines Lexicographically Ascending"); }
    inline const wxString EditLineOpsSortLexicoCaseInsensAscending() { return _("Sort Lines Lex. Ascending Ignoring Case"); }
    inline const wxString EditLineOpsSortLocaleAscending() { return _("Sort Lines In Locale Order Ascending"); }
    inline const wxString EditLineOpsSortIntegerAscending() { return _("Sort Lines As Integers Ascending"); }
    inline const wxString EditLineOpsSortDecimalCommaAscending() { return _("Sort Lines As Decimals (Comma) Ascending"); }
    inline const wxString EditLineOpsSortDecimalDotAscending() { return _("Sort Lines As Decimals (Dot) Ascending"); }
    inline const wxString EditLineOpsSortLengthAscending() { return _("Sort Lines By Length Ascending"); }
    inline const wxString EditLineOpsSortLexicographicDescending() { return _("Sort Lines Lexicographically Descending"); }
    inline const wxString EditLineOpsSortLexicoCaseInsensDescending() { return _("Sort Lines Lex. Descending Ignoring Case"); }
    inline const wxString EditLineOpsSortLocaleDescending() { return _("Sort Lines In Locale Order Descending"); }
    inline const wxString EditLineOpsSortIntegerDescending() { return _("Sort Lines As Integers Descending"); }
    inline const wxString EditLineOpsSortDecimalCommaDescending() { return _("Sort Lines As Decimals (Comma) Descending"); }
    inline const wxString EditLineOpsSortDecimalDotDescending() { return _("Sort Lines As Decimals (Dot) Descending"); }
    inline const wxString EditLineOpsSortLengthDescending() { return _("Sort Lines By Length Descending"); }

    inline const wxString EditCommentUncomment() { return _("Co&mment/Uncomment"); }
    inline const wxString EditCommentToggleSingle() { return _("Toggle Single Line Comment\tCtrl+Q"); }
    inline const wxString EditCommentSetSingle() { return _("Single Line Comment"); }
    inline const wxString EditCommentUnsetSingle() { return _("Single Line Uncomment"); }
    inline const wxString EditCommentBlockComment() { return _("Block Comment\tCtrl+Shift+Q"); }
    inline const wxString EditCommentBlockUncomment() { return _("Block Uncomment"); }

    inline const wxString EditAutoCompletion() { return _("&Auto-Completion"); }
    inline const wxString EditAutoCompleteFunction() { return _("Function Completion\tCtrl+Space"); }
    inline const wxString EditAutoCompleteWord() { return _("Word Completion"); }
    inline const wxString EditFuncCallTip() { return _("Function Parameters Hint"); }
    inline const wxString EditFuncCallTipPrevious() { return _("Function Parameters Previous Hint"); }
    inline const wxString EditFuncCallTipNext() { return _("Function Parameters Next Hint"); }
    inline const wxString EditAutoCompletePath() { return _("Path Completion"); }

    inline const wxString EditEolConversion() { return _("&EOL Conversion"); }
    inline const wxString EditEolToWindows() { return _("Windows (CR LF)"); }
    inline const wxString EditEolToUnix() { return _("Unix (LF)"); }
    inline const wxString EditEolToMac() { return _("Macintosh (CR)"); }

    inline const wxString EditBlankOperations() { return _("&Blank Operations"); }
    inline const wxString EditBlankTrimTrailing() { return _("Trim Trailing Space"); }
    inline const wxString EditBlankTrimLineHead() { return _("Trim Leading Space"); }
    inline const wxString EditBlankTrimBoth() { return _("Trim Leading and Trailing Space"); }
    inline const wxString EditBlankEolToSpace() { return _("EOL to Space"); }
    inline const wxString EditBlankTrimAll() { return _("Trim both and EOL to Space"); }
    inline const wxString EditBlankTabToSpace() { return _("TAB to Space"); }
    inline const wxString EditBlankSpaceToTabAll() { return _("Space to TAB (All)"); }
    inline const wxString EditBlankSpaceToTabLeading() { return _("Space to TAB (Leading)"); }

    inline const wxString EditPasteSpecial() { return _("&Paste Special"); }
    inline const wxString EditPasteAsHtml() { return _("Paste HTML Content"); }
    inline const wxString EditPasteAsRtf() { return _("Paste RTF Content"); }
    inline const wxString EditCopyBinary() { return _("Copy Binary Content"); }
    inline const wxString EditCutBinary() { return _("Cut Binary Content"); }
    inline const wxString EditPasteBinary() { return _("Paste Binary Content"); }

    inline const wxString EditOnSelection() { return _("&On Selection"); }
    inline const wxString EditOpenSelectedFileToEdit() { return _("Open File"); }
    inline const wxString EditOpenSelectedFileFolderInExplorer() { return _("Open Containing Folder in Explorer"); }
    inline const wxString EditRedactSelection() { return _("&Redact Selection █ (Shift: ●)"); }
    inline const wxString EditSearchOnInternet() { return _("Search on Internet"); }
    inline const wxString EditChangeSearchEngine() { return _("Change Search Engine..."); }

    inline const wxString EditMultiselectAll() { return _("Multi-select All"); }
    inline const wxString EditMultiselectNext() { return _("Multi-select Next"); }

    // Shared by both the "Multi-select All" and "Multi-select Next" submenus - same 4
    // msgids in the original source, so gettext would dedupe them as identical strings.
    inline const wxString EditMultiselectIgnoreCaseWholeWord() { return _("Ignore Case && Whole Word"); }
    inline const wxString EditMultiselectMatchCaseOnly() { return _("Match Case Only"); }
    inline const wxString EditMultiselectMatchWholeWordOnly() { return _("Match Whole Word Only"); }
    inline const wxString EditMultiselectMatchCaseWholeWord() { return _("Match Case && Whole Word"); }

    inline const wxString EditMultiselectUndo() { return _("Undo the Latest Added Multi-Select"); }
    inline const wxString EditMultiselectSkip() { return _("Skip Current && Go to Next Multi-select"); }

    inline const wxString EditColumnModeTip() { return _("Column Mode..."); }
    inline const wxString EditColumnMode() { return _("Colum&n Editor...\tAlt+C"); }
    inline const wxString EditCharPanel() { return _("Character &Panel"); }
    inline const wxString EditClipboardHistoryPanel() { return _("Clipboard &History"); }

    inline const wxString EditReadOnly() { return _("Read-&Only"); }
    inline const wxString EditToggleReadOnly() { return _("Read-Only on Current Document"); }
    inline const wxString EditSetReadOnlyForAllDocs() { return _("Read-Only for All Documents"); }
    inline const wxString EditClearReadOnlyForAllDocs() { return _("Clear Read-Only for All Documents"); }
    inline const wxString EditToggleSystemReadOnly() { return _("Read-Only Attribute in Windows"); }
}
