#pragma once
// =====================================================================
// Notepad++ main menu - faithful 1:1 reproduction of IDR_M30_MENU from
// PowerEditor/src/Notepad_plus.rc (every top-level popup, item, submenu,
// separator and mnemonic). Built data-style into a wxMenuBar.
//
// The .rc menu text carries no shortcuts (Notepad++ injects them at runtime
// from its accelerator table); we add the standard defaults to the common
// commands here, so the keyboard shortcuts both WORK and SHOW in the menu,
// mirroring the real app. Items needing the full app are still listed (they
// report themselves via the dispatcher's default case when invoked).
//
// "&&" in a label is a literal ampersand (wx escaping), matching the .rc "&&".
// =====================================================================
#include <wx/menu.h>
#include "menuCmdID.h"

// darkModeId: our extra restart-to-apply "Dark Mode" toggle (myID_DARKMODE),
// added under Settings (real Notepad++ keeps dark mode under Settings too).
inline void buildNppMainMenu(wxMenuBar* mb, int darkModeId)
{
    // ----------------------------------------------------------------- File
    {
        auto* file = new wxMenu;
        file->Append(IDM_FILE_NEW, "&New\tCtrl+N");
        file->Append(IDM_FILE_OPEN, "&Open...\tCtrl+O");
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FILE_OPEN_FOLDER, "Explorer");
            sub->Append(IDM_FILE_OPEN_CMD, "cmd");
            sub->Append(IDM_FILE_OPEN_POWERSHELL, "PowerShell");
            sub->AppendSeparator();
            sub->Append(IDM_FILE_CONTAININGFOLDERASWORKSPACE, "Folder as Workspace");
            file->AppendSubMenu(sub, "Open Containing &Folder");
        }
        file->Append(IDM_FILE_OPEN_DEFAULT_VIEWER, "Open in &Default Viewer");
        file->Append(IDM_FILE_OPENFOLDERASWORKSPACE, "Open Folder as &Workspace...");
        file->Append(IDM_FILE_RELOAD, "Re&load from Disk");
        file->Append(IDM_FILE_SAVE, "&Save\tCtrl+S");
        file->Append(IDM_FILE_SAVEAS, "Save &As...\tCtrl+Alt+S");
        file->Append(IDM_FILE_SAVECOPYAS, "Save a Cop&y As...");
        file->Append(IDM_FILE_SAVEALL, "Sa&ve All\tCtrl+Shift+S");
        file->Append(IDM_FILE_RENAME, "&Rename...");
        file->Append(IDM_FILE_CLOSE, "&Close\tCtrl+W");
        file->Append(IDM_FILE_CLOSEALL, "Clos&e All\tCtrl+Shift+W");
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FILE_CLOSEALL_BUT_CURRENT, "Close All but Active Document");
            sub->Append(IDM_FILE_CLOSEALL_BUT_PINNED, "Close All but Pinned Documents");
            sub->Append(IDM_FILE_CLOSEALL_TOLEFT, "Close All to the Left");
            sub->Append(IDM_FILE_CLOSEALL_TORIGHT, "Close All to the Right");
            sub->Append(IDM_FILE_CLOSEALL_UNCHANGED, "Close All Unchanged");
            file->AppendSubMenu(sub, "Close &Multiple Documents");
        }
        file->Append(IDM_FILE_DELETE, "Move to Recycle &Bin");
        file->AppendSeparator();
        file->Append(IDM_FILE_LOADSESSION, "Load Sess&ion...");
        file->Append(IDM_FILE_SAVESESSION, "Save Sess&ion...");
        file->AppendSeparator();
        file->Append(IDM_FILE_PRINT, "&Print...\tCtrl+P");
        file->Append(IDM_FILE_PRINTNOW, "Print No&w");
        file->AppendSeparator();
        // The "Recent Files" submenu (wxFileHistory) is inserted here at runtime by buildMenuBar().
        file->Append(IDM_FILE_EXIT, "E&xit\tAlt+F4");
        mb->Append(file, "&File");
    }

    // ----------------------------------------------------------------- Edit
    {
        auto* edit = new wxMenu;
        edit->Append(IDM_EDIT_UNDO, "&Undo\tCtrl+Z");
        edit->Append(IDM_EDIT_REDO, "&Redo\tCtrl+Y");
        edit->AppendSeparator();
        edit->Append(IDM_EDIT_CUT, "Cu&t\tCtrl+X");
        edit->Append(IDM_EDIT_COPY, "&Copy\tCtrl+C");
        edit->Append(IDM_EDIT_PASTE, "&Paste\tCtrl+V");
        edit->Append(IDM_EDIT_DELETE, "&Delete\tDel");
        edit->Append(IDM_EDIT_SELECTALL, "&Select All\tCtrl+A");
        edit->Append(IDM_EDIT_BEGINENDSELECT, "Begin/End &Select");
        edit->Append(IDM_EDIT_BEGINENDSELECT_COLUMNMODE, "Begin/End Select in Column Mode");
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_INSERT_DATETIME_SHORT, "Date Time (short)");
            sub->Append(IDM_EDIT_INSERT_DATETIME_LONG, "Date Time (long)");
            sub->Append(IDM_EDIT_INSERT_DATETIME_CUSTOMIZED, "Date Time (customized)");
            edit->AppendSubMenu(sub, "Insert");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_FULLPATHTOCLIP, "Copy Current Full File path");
            sub->Append(IDM_EDIT_FILENAMETOCLIP, "Copy Current Filename");
            sub->Append(IDM_EDIT_CURRENTDIRTOCLIP, "Copy Current Dir. Path");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_COPY_ALL_NAMES, "Copy All Filenames");
            sub->Append(IDM_EDIT_COPY_ALL_PATHS, "Copy All File Paths");
            edit->AppendSubMenu(sub, "Cop&y to Clipboard");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_INS_TAB, "Increase Line Indent");
            sub->Append(IDM_EDIT_RMV_TAB, "Decrease Line Indent");
            edit->AppendSubMenu(sub, "&Indent");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_UPPERCASE, "&UPPERCASE\tCtrl+Shift+U");
            sub->Append(IDM_EDIT_LOWERCASE, "&lowercase\tCtrl+U");
            sub->Append(IDM_EDIT_PROPERCASE_FORCE, "&Proper Case");
            sub->Append(IDM_EDIT_PROPERCASE_BLEND, "Proper Case (blend)");
            sub->Append(IDM_EDIT_SENTENCECASE_FORCE, "&Sentence case");
            sub->Append(IDM_EDIT_SENTENCECASE_BLEND, "Sentence case (blend)");
            sub->Append(IDM_EDIT_INVERTCASE, "&iNVERT cASE");
            sub->Append(IDM_EDIT_RANDOMCASE, "&ranDOm CasE");
            edit->AppendSubMenu(sub, "Con&vert Case to");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_DUP_LINE, "Duplicate Current Line\tCtrl+D");
            sub->Append(IDM_EDIT_REMOVE_ANY_DUP_LINES, "Remove Duplicate Lines");
            sub->Append(IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES, "Remove Consecutive Duplicate Lines");
            sub->Append(IDM_EDIT_SPLIT_LINES, "Split Lines\tCtrl+I");
            sub->Append(IDM_EDIT_JOIN_LINES, "Join Lines\tCtrl+J");
            sub->Append(IDM_EDIT_LINE_UP, "Move Up Current Line\tCtrl+Shift+Up");
            sub->Append(IDM_EDIT_LINE_DOWN, "Move Down Current Line\tCtrl+Shift+Down");
            sub->Append(IDM_EDIT_REMOVEEMPTYLINES, "Remove Empty Lines");
            sub->Append(IDM_EDIT_REMOVEEMPTYLINESWITHBLANK, "Remove Empty Lines (Containing Blank characters)");
            sub->Append(IDM_EDIT_BLANKLINEABOVECURRENT, "Insert Blank Line Above Current");
            sub->Append(IDM_EDIT_BLANKLINEBELOWCURRENT, "Insert Blank Line Below Current");
            sub->Append(IDM_EDIT_SORTLINES_REVERSE_ORDER, "Reverse Line Order");
            sub->Append(IDM_EDIT_SORTLINES_RANDOMLY, "Randomize Line Order");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING, "Sort Lines Lexicographically Ascending");
            sub->Append(IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING, "Sort Lines Lex. Ascending Ignoring Case");
            sub->Append(IDM_EDIT_SORTLINES_LOCALE_ASCENDING, "Sort Lines In Locale Order Ascending");
            sub->Append(IDM_EDIT_SORTLINES_INTEGER_ASCENDING, "Sort Lines As Integers Ascending");
            sub->Append(IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING, "Sort Lines As Decimals (Comma) Ascending");
            sub->Append(IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING, "Sort Lines As Decimals (Dot) Ascending");
            sub->Append(IDM_EDIT_SORTLINES_LENGTH_ASCENDING, "Sort Lines By Length Ascending");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING, "Sort Lines Lexicographically Descending");
            sub->Append(IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING, "Sort Lines Lex. Descending Ignoring Case");
            sub->Append(IDM_EDIT_SORTLINES_LOCALE_DESCENDING, "Sort Lines In Locale Order Descending");
            sub->Append(IDM_EDIT_SORTLINES_INTEGER_DESCENDING, "Sort Lines As Integers Descending");
            sub->Append(IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING, "Sort Lines As Decimals (Comma) Descending");
            sub->Append(IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING, "Sort Lines As Decimals (Dot) Descending");
            sub->Append(IDM_EDIT_SORTLINES_LENGTH_DESCENDING, "Sort Lines By Length Descending");
            edit->AppendSubMenu(sub, "&Line Operations");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_BLOCK_COMMENT, "Toggle Single Line Comment\tCtrl+Q");
            sub->Append(IDM_EDIT_BLOCK_COMMENT_SET, "Single Line Comment");
            sub->Append(IDM_EDIT_BLOCK_UNCOMMENT, "Single Line Uncomment");
            sub->Append(IDM_EDIT_STREAM_COMMENT, "Block Comment\tCtrl+Shift+Q");
            sub->Append(IDM_EDIT_STREAM_UNCOMMENT, "Block Uncomment");
            edit->AppendSubMenu(sub, "Co&mment/Uncomment");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_AUTOCOMPLETE, "Function Completion\tCtrl+Space");
            sub->Append(IDM_EDIT_AUTOCOMPLETE_CURRENTFILE, "Word Completion");
            sub->Append(IDM_EDIT_FUNCCALLTIP, "Function Parameters Hint");
            sub->Append(IDM_EDIT_FUNCCALLTIP_PREVIOUS, "Function Parameters Previous Hint");
            sub->Append(IDM_EDIT_FUNCCALLTIP_NEXT, "Function Parameters Next Hint");
            sub->Append(IDM_EDIT_AUTOCOMPLETE_PATH, "Path Completion");
            edit->AppendSubMenu(sub, "&Auto-Completion");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_FORMAT_TODOS, "Windows (CR LF)");
            sub->Append(IDM_FORMAT_TOUNIX, "Unix (LF)");
            sub->Append(IDM_FORMAT_TOMAC, "Macintosh (CR)");
            edit->AppendSubMenu(sub, "&EOL Conversion");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_TRIMTRAILING, "Trim Trailing Space");
            sub->Append(IDM_EDIT_TRIMLINEHEAD, "Trim Leading Space");
            sub->Append(IDM_EDIT_TRIM_BOTH, "Trim Leading and Trailing Space");
            sub->Append(IDM_EDIT_EOL2WS, "EOL to Space");
            sub->Append(IDM_EDIT_TRIMALL, "Trim both and EOL to Space");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_TAB2SW, "TAB to Space");
            sub->Append(IDM_EDIT_SW2TAB_ALL, "Space to TAB (All)");
            sub->Append(IDM_EDIT_SW2TAB_LEADING, "Space to TAB (Leading)");
            edit->AppendSubMenu(sub, "&Blank Operations");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_PASTE_AS_HTML, "Paste HTML Content");
            sub->Append(IDM_EDIT_PASTE_AS_RTF, "Paste RTF Content");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_COPY_BINARY, "Copy Binary Content");
            sub->Append(IDM_EDIT_CUT_BINARY, "Cut Binary Content");
            sub->Append(IDM_EDIT_PASTE_BINARY, "Paste Binary Content");
            edit->AppendSubMenu(sub, "&Paste Special");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_OPENSELECTEDFILETOEDIT, "Open File");
            sub->Append(IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER, "Open Containing Folder in Explorer");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_REDACT_SELECTION, "&Redact Selection █ (Shift: ●)");
            sub->AppendSeparator();
            sub->Append(IDM_EDIT_SEARCHONINTERNET, "Search on Internet");
            sub->Append(IDM_EDIT_CHANGESEARCHENGINE, "Change Search Engine...");
            edit->AppendSubMenu(sub, "&On Selection");
        }
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_MULTISELECTALL, "Ignore Case && Whole Word");
            sub->Append(IDM_EDIT_MULTISELECTALLMATCHCASE, "Match Case Only");
            sub->Append(IDM_EDIT_MULTISELECTALLWHOLEWORD, "Match Whole Word Only");
            sub->Append(IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD, "Match Case && Whole Word");
            edit->AppendSubMenu(sub, "Multi-select All");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_MULTISELECTNEXT, "Ignore Case && Whole Word");
            sub->Append(IDM_EDIT_MULTISELECTNEXTMATCHCASE, "Match Case Only");
            sub->Append(IDM_EDIT_MULTISELECTNEXTWHOLEWORD, "Match Whole Word Only");
            sub->Append(IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD, "Match Case && Whole Word");
            edit->AppendSubMenu(sub, "Multi-select Next");
        }
        edit->Append(IDM_EDIT_MULTISELECTUNDO, "Undo the Latest Added Multi-Select");
        edit->Append(IDM_EDIT_MULTISELECTSSKIP, "Skip Current && Go to Next Multi-select");
        edit->AppendSeparator();
        edit->Append(IDM_EDIT_COLUMNMODETIP, "Column Mode...");
        edit->Append(IDM_EDIT_COLUMNMODE, "Colum&n Editor...\tAlt+C");
        edit->Append(IDM_EDIT_CHAR_PANEL, "Character &Panel");
        edit->Append(IDM_EDIT_CLIPBOARDHISTORY_PANEL, "Clipboard &History");
        edit->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_EDIT_TOGGLEREADONLY, "Read-Only on Current Document");
            sub->Append(IDM_EDIT_SETREADONLYFORALLDOCS, "Read-Only for All Documents");
            sub->Append(IDM_EDIT_CLEARREADONLYFORALLDOCS, "Clear Read-Only for All Documents");
            edit->AppendSubMenu(sub, "Read-&Only");
        }
        edit->Append(IDM_EDIT_TOGGLESYSTEMREADONLY, "Read-Only Attribute in Windows");
        mb->Append(edit, "&Edit");
    }

    // --------------------------------------------------------------- Search
    {
        auto* search = new wxMenu;
        search->Append(IDM_SEARCH_FIND, "&Find...\tCtrl+F");
        search->Append(IDM_SEARCH_FINDINFILES, "Find in Fi&les...\tCtrl+Shift+F");
        search->Append(IDM_SEARCH_FINDNEXT, "Find &Next\tF3");
        search->Append(IDM_SEARCH_FINDPREV, "Find &Previous\tShift+F3");
        search->Append(IDM_SEARCH_SETANDFINDNEXT, "&Select and Find Next\tCtrl+F3");
        search->Append(IDM_SEARCH_SETANDFINDPREV, "&Select and Find Previous\tCtrl+Shift+F3");
        search->Append(IDM_SEARCH_VOLATILE_FINDNEXT, "Find (&Volatile) Next");
        search->Append(IDM_SEARCH_VOLATILE_FINDPREV, "Find (&Volatile) Previous");
        search->Append(IDM_SEARCH_REPLACE, "&Replace...\tCtrl+H");
        search->Append(IDM_SEARCH_FINDINCREMENT, "&Incremental Search\tCtrl+Alt+I");
        search->Append(IDM_FOCUS_ON_FOUND_RESULTS, "Search Results &Window\tF7");
        search->Append(IDM_SEARCH_GOTONEXTFOUND, "Next Search Resul&t\tF4");
        search->Append(IDM_SEARCH_GOTOPREVFOUND, "Previous Search Resul&t\tShift+F4");
        search->Append(IDM_SEARCH_GOTOLINE, "&Go to...\tCtrl+G");
        search->Append(IDM_SEARCH_GOTOMATCHINGBRACE, "Go to &Matching Brace\tCtrl+B");
        search->Append(IDM_SEARCH_SELECTMATCHINGBRACES, "Select All In-betw&een {} [] or ()");
        search->Append(IDM_SEARCH_MARK, "Mar&k...");
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_CHANGED_NEXT, "Go to Next Change");
            sub->Append(IDM_SEARCH_CHANGED_PREV, "Go to Previous Change");
            sub->Append(IDM_SEARCH_CLEAR_CHANGE_HISTORY, "Clear Change History");
            search->AppendSubMenu(sub, "Change History");
        }
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_MARKALLEXT1, "Using 1st Style");
            sub->Append(IDM_SEARCH_MARKALLEXT2, "Using 2nd Style");
            sub->Append(IDM_SEARCH_MARKALLEXT3, "Using 3rd Style");
            sub->Append(IDM_SEARCH_MARKALLEXT4, "Using 4th Style");
            sub->Append(IDM_SEARCH_MARKALLEXT5, "Using 5th Style");
            search->AppendSubMenu(sub, "Style &All Occurrences of Token");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_MARKONEEXT1, "Using 1st Style");
            sub->Append(IDM_SEARCH_MARKONEEXT2, "Using 2nd Style");
            sub->Append(IDM_SEARCH_MARKONEEXT3, "Using 3rd Style");
            sub->Append(IDM_SEARCH_MARKONEEXT4, "Using 4th Style");
            sub->Append(IDM_SEARCH_MARKONEEXT5, "Using 5th Style");
            search->AppendSubMenu(sub, "Style &One Token");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_UNMARKALLEXT1, "Clear 1st Style");
            sub->Append(IDM_SEARCH_UNMARKALLEXT2, "Clear 2nd Style");
            sub->Append(IDM_SEARCH_UNMARKALLEXT3, "Clear 3rd Style");
            sub->Append(IDM_SEARCH_UNMARKALLEXT4, "Clear 4th Style");
            sub->Append(IDM_SEARCH_UNMARKALLEXT5, "Clear 5th Style");
            sub->Append(IDM_SEARCH_CLEARALLMARKS, "Clear all Styles");
            search->AppendSubMenu(sub, "Clear Style");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_GOPREVMARKER1, "1st Style");
            sub->Append(IDM_SEARCH_GOPREVMARKER2, "2nd Style");
            sub->Append(IDM_SEARCH_GOPREVMARKER3, "3rd Style");
            sub->Append(IDM_SEARCH_GOPREVMARKER4, "4th Style");
            sub->Append(IDM_SEARCH_GOPREVMARKER5, "5th Style");
            sub->Append(IDM_SEARCH_GOPREVMARKER_DEF, "Find Mark Style");
            search->AppendSubMenu(sub, "&Jump Up");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_GONEXTMARKER1, "1st Style");
            sub->Append(IDM_SEARCH_GONEXTMARKER2, "2nd Style");
            sub->Append(IDM_SEARCH_GONEXTMARKER3, "3rd Style");
            sub->Append(IDM_SEARCH_GONEXTMARKER4, "4th Style");
            sub->Append(IDM_SEARCH_GONEXTMARKER5, "5th Style");
            sub->Append(IDM_SEARCH_GONEXTMARKER_DEF, "Find Mark Style");
            search->AppendSubMenu(sub, "Jump &Down");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_STYLE1TOCLIP, "1st Style");
            sub->Append(IDM_SEARCH_STYLE2TOCLIP, "2nd Style");
            sub->Append(IDM_SEARCH_STYLE3TOCLIP, "3rd Style");
            sub->Append(IDM_SEARCH_STYLE4TOCLIP, "4th Style");
            sub->Append(IDM_SEARCH_STYLE5TOCLIP, "5th Style");
            sub->Append(IDM_SEARCH_ALLSTYLESTOCLIP, "All Styles");
            sub->Append(IDM_SEARCH_MARKEDTOCLIP, "Find Mark Style");
            search->AppendSubMenu(sub, "&Copy Styled Text");
        }
        search->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SEARCH_TOGGLE_BOOKMARK, "Toggle Bookmark\tCtrl+F2");
            sub->Append(IDM_SEARCH_NEXT_BOOKMARK, "Next Bookmark\tF2");
            sub->Append(IDM_SEARCH_PREV_BOOKMARK, "Previous Bookmark\tShift+F2");
            sub->Append(IDM_SEARCH_CLEAR_BOOKMARKS, "Clear All Bookmarks");
            sub->Append(IDM_SEARCH_CUTMARKEDLINES, "Cut Bookmarked Lines");
            sub->Append(IDM_SEARCH_COPYMARKEDLINES, "Copy Bookmarked Lines");
            sub->Append(IDM_SEARCH_PASTEMARKEDLINES, "Paste to (Replace) Bookmarked Lines");
            sub->Append(IDM_SEARCH_DELETEMARKEDLINES, "Remove Bookmarked Lines");
            sub->Append(IDM_SEARCH_DELETEUNMARKEDLINES, "Remove Non-Bookmarked Lines");
            sub->Append(IDM_SEARCH_INVERSEMARKS, "Inverse Bookmarks");
            search->AppendSubMenu(sub, "&Bookmark");
        }
        search->AppendSeparator();
        search->Append(IDM_SEARCH_FINDCHARINRANGE, "Find characters in rang&e...");
        mb->Append(search, "&Search");
    }

    // ----------------------------------------------------------------- View
    // (the restart-to-apply Dark Mode toggle is added under Settings, below)
    {
        auto* view = new wxMenu;
        view->AppendCheckItem(IDM_VIEW_ALWAYSONTOP, "Always on &Top");
        view->AppendCheckItem(IDM_VIEW_FULLSCREENTOGGLE, "To&ggle Full Screen Mode\tF11");
        view->AppendCheckItem(IDM_VIEW_POSTIT, "&Post-It\tF12");
        view->AppendCheckItem(IDM_VIEW_DISTRACTIONFREE, "D&istraction Free Mode");
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_IN_FIREFOX, "&Firefox");
            sub->Append(IDM_VIEW_IN_CHROME, "&Chrome");
            sub->Append(IDM_VIEW_IN_EDGE, "&Edge");
            sub->Append(IDM_VIEW_IN_IE, "&IE");
            view->AppendSubMenu(sub, "&View Current File in");
        }
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->AppendCheckItem(IDM_VIEW_TAB_SPACE, "Show Space and Tab");
            sub->AppendCheckItem(IDM_VIEW_EOL, "Show End of Line");
            sub->AppendCheckItem(IDM_VIEW_NPC, "Show Non-Printing Characters");
            sub->AppendCheckItem(IDM_VIEW_NPC_CCUNIEOL, "Show Control Characters && Unicode EOL");
            sub->AppendCheckItem(IDM_VIEW_ALL_CHARACTERS, "Show All Characters");
            sub->AppendSeparator();
            sub->AppendCheckItem(IDM_VIEW_INDENT_GUIDE, "Show Indent Guide");
            sub->AppendCheckItem(IDM_VIEW_WRAP_SYMBOL, "Show Wrap Symbol");
            view->AppendSubMenu(sub, "Show S&ymbol");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_ZOOMIN, "Zoom &In (Ctrl+Mouse Wheel Up)");
            sub->Append(IDM_VIEW_ZOOMOUT, "Zoom &Out (Ctrl+Mouse Wheel Down)");
            sub->Append(IDM_VIEW_ZOOMRESTORE, "&Restore Default Zoom");
            view->AppendSubMenu(sub, "&Zoom");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_GOTO_NEW_INSTANCE, "Mo&ve to New Instance");
            sub->Append(IDM_VIEW_LOAD_IN_NEW_INSTANCE, "&Open in New Instance");
            view->AppendSubMenu(sub, "&Move/Clone Current Document");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_TAB1, "1st Tab\tCtrl+1");
            sub->Append(IDM_VIEW_TAB2, "2nd Tab\tCtrl+2");
            sub->Append(IDM_VIEW_TAB3, "3rd Tab\tCtrl+3");
            sub->Append(IDM_VIEW_TAB4, "4th Tab\tCtrl+4");
            sub->Append(IDM_VIEW_TAB5, "5th Tab\tCtrl+5");
            sub->Append(IDM_VIEW_TAB6, "6th Tab\tCtrl+6");
            sub->Append(IDM_VIEW_TAB7, "7th Tab\tCtrl+7");
            sub->Append(IDM_VIEW_TAB8, "8th Tab\tCtrl+8");
            sub->Append(IDM_VIEW_TAB9, "9th Tab\tCtrl+9");
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_TAB_START, "First Tab");
            sub->Append(IDM_VIEW_TAB_END, "Last Tab");
            sub->Append(IDM_VIEW_TAB_NEXT, "Next Tab\tCtrl+Tab");
            sub->Append(IDM_VIEW_TAB_PREV, "Previous Tab\tCtrl+Shift+Tab");
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_GOTO_START, "Move to Start");
            sub->Append(IDM_VIEW_GOTO_END, "Move to End");
            sub->Append(IDM_VIEW_TAB_MOVEFORWARD, "Move Tab Forward");
            sub->Append(IDM_VIEW_TAB_MOVEBACKWARD, "Move Tab Backward");
            sub->AppendSeparator();
            sub->Append(IDM_VIEW_TAB_COLOUR_1, "Apply Color 1");
            sub->Append(IDM_VIEW_TAB_COLOUR_2, "Apply Color 2");
            sub->Append(IDM_VIEW_TAB_COLOUR_3, "Apply Color 3");
            sub->Append(IDM_VIEW_TAB_COLOUR_4, "Apply Color 4");
            sub->Append(IDM_VIEW_TAB_COLOUR_5, "Apply Color 5");
            sub->Append(IDM_VIEW_TAB_COLOUR_NONE, "Remove Color");
            view->AppendSubMenu(sub, "Ta&b");
        }
        view->AppendCheckItem(IDM_VIEW_WRAP, "&Word wrap");
        view->Append(IDM_VIEW_HIDELINES, "&Hide Lines");
        view->AppendSeparator();
        view->Append(IDM_VIEW_FOLDALL, "Fold All\tAlt+0");
        view->Append(IDM_VIEW_UNFOLDALL, "Unfold All\tAlt+Shift+0");
        view->Append(IDM_VIEW_FOLD_CURRENT, "Fold Current Level\tCtrl+Alt+F");
        view->Append(IDM_VIEW_UNFOLD_CURRENT, "Unfold Current Level\tCtrl+Alt+Shift+F");
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_FOLD_1, "1\tAlt+1");
            sub->Append(IDM_VIEW_FOLD_2, "2\tAlt+2");
            sub->Append(IDM_VIEW_FOLD_3, "3\tAlt+3");
            sub->Append(IDM_VIEW_FOLD_4, "4\tAlt+4");
            sub->Append(IDM_VIEW_FOLD_5, "5\tAlt+5");
            sub->Append(IDM_VIEW_FOLD_6, "6\tAlt+6");
            sub->Append(IDM_VIEW_FOLD_7, "7\tAlt+7");
            sub->Append(IDM_VIEW_FOLD_8, "8\tAlt+8");
            view->AppendSubMenu(sub, "Fold Level");
        }
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_UNFOLD_1, "1\tAlt+Shift+1");
            sub->Append(IDM_VIEW_UNFOLD_2, "2\tAlt+Shift+2");
            sub->Append(IDM_VIEW_UNFOLD_3, "3\tAlt+Shift+3");
            sub->Append(IDM_VIEW_UNFOLD_4, "4\tAlt+Shift+4");
            sub->Append(IDM_VIEW_UNFOLD_5, "5\tAlt+Shift+5");
            sub->Append(IDM_VIEW_UNFOLD_6, "6\tAlt+Shift+6");
            sub->Append(IDM_VIEW_UNFOLD_7, "7\tAlt+Shift+7");
            sub->Append(IDM_VIEW_UNFOLD_8, "8\tAlt+Shift+8");
            view->AppendSubMenu(sub, "Unfold Level");
        }
        view->AppendSeparator();
        view->Append(IDM_VIEW_SUMMARY, "&Summary...");
        view->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_VIEW_PROJECT_PANEL_1, "Project Panel &1");
            sub->Append(IDM_VIEW_PROJECT_PANEL_2, "Project Panel &2");
            sub->Append(IDM_VIEW_PROJECT_PANEL_3, "Project Panel &3");
            view->AppendSubMenu(sub, "Pro&ject Panels");
        }
        view->Append(IDM_VIEW_FILEBROWSER, "Folder as Wor&kspace");
        view->Append(IDM_VIEW_DOC_MAP, "&Document Map");
        view->Append(IDM_VIEW_DOCLIST, "D&ocument List");
        view->Append(IDM_VIEW_FUNC_LIST, "Function &List");
        view->AppendSeparator();
        view->Append(IDM_EDIT_RTL, "T&ext Direction RTL");
        view->Append(IDM_EDIT_LTR, "Te&xt Direction LTR");
        view->AppendSeparator();
        view->AppendCheckItem(IDM_VIEW_MONITORING, "Monito&ring (tail -f)");
        mb->Append(view, "&View");
    }

    // ------------------------------------------------------------- Encoding
    {
        auto* enc = new wxMenu;
        enc->AppendCheckItem(IDM_FORMAT_ANSI, "ANSI");
        enc->AppendCheckItem(IDM_FORMAT_AS_UTF_8, "UTF-8");
        enc->AppendCheckItem(IDM_FORMAT_UTF_8, "UTF-8-BOM");
        enc->AppendCheckItem(IDM_FORMAT_UTF_16BE, "UTF-16 BE BOM");
        enc->AppendCheckItem(IDM_FORMAT_UTF_16LE, "UTF-16 LE BOM");
        {
            auto* charsets = new wxMenu;
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_6, "ISO 8859-6");
              s->Append(IDM_FORMAT_DOS_720, "OEM 720");
              s->Append(IDM_FORMAT_WIN_1256, "Windows-1256");
              charsets->AppendSubMenu(s, "Arabic"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_4, "ISO 8859-4");
              s->Append(IDM_FORMAT_ISO_8859_13, "ISO 8859-13");
              s->Append(IDM_FORMAT_DOS_775, "OEM 775");
              s->Append(IDM_FORMAT_WIN_1257, "Windows-1257");
              charsets->AppendSubMenu(s, "Baltic"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_14, "ISO 8859-14");
              charsets->AppendSubMenu(s, "Celtic"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_5, "ISO 8859-5");
              s->Append(IDM_FORMAT_KOI8R_CYRILLIC, "KOI8-R");
              s->Append(IDM_FORMAT_KOI8U_CYRILLIC, "KOI8-U");
              s->Append(IDM_FORMAT_MAC_CYRILLIC, "Macintosh");
              s->Append(IDM_FORMAT_DOS_855, "OEM 855");
              s->Append(IDM_FORMAT_DOS_866, "OEM 866");
              s->Append(IDM_FORMAT_WIN_1251, "Windows-1251");
              charsets->AppendSubMenu(s, "Cyrillic"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_DOS_852, "OEM 852");
              s->Append(IDM_FORMAT_WIN_1250, "Windows-1250");
              charsets->AppendSubMenu(s, "Central European"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_BIG5, "Big5 (Traditional)");
              s->Append(IDM_FORMAT_GB2312, "GB2312 (Simplified)");
              charsets->AppendSubMenu(s, "Chinese"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_2, "ISO 8859-2");
              charsets->AppendSubMenu(s, "Eastern European"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_7, "ISO 8859-7");
              s->Append(IDM_FORMAT_DOS_737, "OEM 737");
              s->Append(IDM_FORMAT_DOS_869, "OEM 869");
              s->Append(IDM_FORMAT_WIN_1253, "Windows-1253");
              charsets->AppendSubMenu(s, "Greek"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_8, "ISO 8859-8");
              s->Append(IDM_FORMAT_DOS_862, "OEM 862");
              s->Append(IDM_FORMAT_WIN_1255, "Windows-1255");
              charsets->AppendSubMenu(s, "Hebrew"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_SHIFT_JIS, "Shift-JIS");
              charsets->AppendSubMenu(s, "Japanese"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_KOREAN_WIN, "Windows 949");
              s->Append(IDM_FORMAT_EUC_KR, "EUC-KR");
              charsets->AppendSubMenu(s, "Korean"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_DOS_861, "OEM 861 : Icelandic");
              s->Append(IDM_FORMAT_DOS_865, "OEM 865 : Nordic");
              charsets->AppendSubMenu(s, "North European"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_TIS_620, "TIS-620");
              charsets->AppendSubMenu(s, "Thai"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_3, "ISO 8859-3");
              s->Append(IDM_FORMAT_ISO_8859_9, "ISO 8859-9");
              s->Append(IDM_FORMAT_DOS_857, "OEM 857");
              s->Append(IDM_FORMAT_WIN_1254, "Windows-1254");
              charsets->AppendSubMenu(s, "Turkish"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_ISO_8859_1, "ISO 8859-1");
              s->Append(IDM_FORMAT_ISO_8859_15, "ISO 8859-15");
              s->Append(IDM_FORMAT_DOS_850, "OEM 850");
              s->Append(IDM_FORMAT_DOS_858, "OEM 858");
              s->Append(IDM_FORMAT_DOS_860, "OEM 860 : Portuguese");
              s->Append(IDM_FORMAT_DOS_863, "OEM 863 : French");
              s->Append(IDM_FORMAT_DOS_437, "OEM-US");
              s->Append(IDM_FORMAT_WIN_1252, "Windows-1252");
              charsets->AppendSubMenu(s, "Western European"); }
            { auto* s = new wxMenu;
              s->Append(IDM_FORMAT_WIN_1258, "Windows-1258");
              charsets->AppendSubMenu(s, "Vietnamese"); }
            enc->AppendSubMenu(charsets, "Character sets");
        }
        enc->AppendSeparator();
        enc->Append(IDM_FORMAT_CONV2_ANSI, "Convert to ANSI");
        enc->Append(IDM_FORMAT_CONV2_AS_UTF_8, "Convert to UTF-8");
        enc->Append(IDM_FORMAT_CONV2_UTF_8, "Convert to UTF-8-BOM");
        enc->Append(IDM_FORMAT_CONV2_UTF_16BE, "Convert to UTF-16 BE BOM");
        enc->Append(IDM_FORMAT_CONV2_UTF_16LE, "Convert to UTF-16 LE BOM");
        mb->Append(enc, "E&ncoding");
    }

    // ------------------------------------------------------------- Language
    // The 80+ language list (bucketed A/B/C... at runtime) is populated by the
    // full app; only the stable static tail items are reproduced here.
    {
        auto* lang = new wxMenu;
        lang->Append(IDM_LANG_TEXT, "None (Normal Text)");
        lang->AppendSeparator();
        lang->Append(0, "(languages populated at runtime)")->Enable(false);
        lang->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_LANG_USER_DLG, "Define your language...");
            sub->Append(IDM_LANG_OPENUDLDIR, "Open User Defined Language folder...");
            sub->Append(IDM_LANG_UDLCOLLECTION_PROJECT_SITE, "Notepad++ User Defined Languages Collection");
            lang->AppendSubMenu(sub, "User Defined Language");
        }
        lang->Append(IDM_LANG_USER, "User-Defined");
        mb->Append(lang, "&Language");
    }

    // ------------------------------------------------------------- Settings
    {
        auto* settings = new wxMenu;
        settings->Append(IDM_SETTING_PREFERENCE, "Preferences...");
        settings->Append(IDM_LANGSTYLE_CONFIG_DLG, "Style Configurator...");
        settings->Append(IDM_SETTING_SHORTCUT_MAPPER, "Shortcut Mapper...");
        settings->AppendSeparator();
        {
            auto* sub = new wxMenu;
            sub->Append(IDM_SETTING_IMPORTPLUGIN, "Import plugin(s)...");
            sub->Append(IDM_SETTING_IMPORTSTYLETHEMES, "Import style theme(s)...");
            settings->AppendSubMenu(sub, "Import");
        }
        settings->AppendSeparator();
        settings->Append(IDM_SETTING_EDITCONTEXTMENU, "Edit Popup ContextMenu");
        settings->AppendSeparator();
        settings->AppendCheckItem(darkModeId, "&Dark Mode");   // our restart-to-apply theme toggle
        mb->Append(settings, "Se&ttings");
    }

    // ---------------------------------------------------------------- Tools
    {
        auto* tools = new wxMenu;
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_MD5_GENERATE, "Generate...");
          sub->Append(IDM_TOOL_MD5_GENERATEFROMFILE, "Generate from files...");
          sub->Append(IDM_TOOL_MD5_GENERATEINTOCLIPBOARD, "Generate from selection into clipboard");
          tools->AppendSubMenu(sub, "MD5"); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA1_GENERATE, "Generate...");
          sub->Append(IDM_TOOL_SHA1_GENERATEFROMFILE, "Generate from files...");
          sub->Append(IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD, "Generate from selection into clipboard");
          tools->AppendSubMenu(sub, "SHA-1"); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA256_GENERATE, "Generate...");
          sub->Append(IDM_TOOL_SHA256_GENERATEFROMFILE, "Generate from files...");
          sub->Append(IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD, "Generate from selection into clipboard");
          tools->AppendSubMenu(sub, "SHA-256"); }
        { auto* sub = new wxMenu;
          sub->Append(IDM_TOOL_SHA512_GENERATE, "Generate...");
          sub->Append(IDM_TOOL_SHA512_GENERATEFROMFILE, "Generate from files...");
          sub->Append(IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD, "Generate from selection into clipboard");
          tools->AppendSubMenu(sub, "SHA-512"); }
        mb->Append(tools, "T&ools");
    }

    // ---------------------------------------------------------------- Macro
    {
        auto* macro = new wxMenu;
        macro->Append(IDM_MACRO_STARTRECORDINGMACRO, "Start Re&cording");
        macro->Append(IDM_MACRO_STOPRECORDINGMACRO, "S&top Recording");
        macro->Append(IDM_MACRO_PLAYBACKRECORDEDMACRO, "&Playback");
        macro->Append(IDM_MACRO_SAVECURRENTMACRO, "&Save Current Recorded Macro...");
        macro->Append(IDM_MACRO_RUNMULTIMACRODLG, "&Run a Macro Multiple Times...");
        mb->Append(macro, "&Macro");
    }

    // ------------------------------------------------------------------ Run
    {
        auto* run = new wxMenu;
        run->Append(IDM_EXECUTE, "&Run...\tF5");
        run->AppendSeparator();
        run->Append(IDM_EXECUTE_VALIDATE_SHORTCUTSXML, "Validate shortcuts.xml");
        mb->Append(run, "&Run");
    }

    // -------------------------------------------------------------- Plugins
    {
        auto* plugins = new wxMenu;
        plugins->Append(IDM_SETTING_OPENPLUGINSDIR, "Open Plugins Folder...");
        mb->Append(plugins, "&Plugins");
    }

    // --------------------------------------------------------------- Window
    {
        auto* window = new wxMenu;
        { auto* sub = new wxMenu;
          sub->Append(IDM_WINDOW_SORT_FN_ASC, "Name A to Z");
          sub->Append(IDM_WINDOW_SORT_FN_DSC, "Name Z to A");
          sub->Append(IDM_WINDOW_SORT_FP_ASC, "Path A to Z");
          sub->Append(IDM_WINDOW_SORT_FP_DSC, "Path Z to A");
          sub->Append(IDM_WINDOW_SORT_FT_ASC, "Type A to Z");
          sub->Append(IDM_WINDOW_SORT_FT_DSC, "Type Z to A");
          sub->Append(IDM_WINDOW_SORT_FS_ASC, "Content Length Ascending");
          sub->Append(IDM_WINDOW_SORT_FS_DSC, "Content Length Descending");
          sub->Append(IDM_WINDOW_SORT_FD_ASC, "Modified Time Ascending");
          sub->Append(IDM_WINDOW_SORT_FD_DSC, "Modified Time Descending");
          window->AppendSubMenu(sub, "Sort By"); }
        window->Append(IDM_WINDOW_WINDOWS, "&Windows...");
        window->AppendSeparator();
        window->Append(IDM_WINDOW_MRU_FIRST, "Recent Window")->Enable(false);
        mb->Append(window, "&Window");
    }

    // ----------------------------------------------------------- ? (Help)
    {
        auto* help = new wxMenu;
        help->Append(IDM_CMDLINEARGUMENTS, "Command Line Arguments...");
        help->AppendSeparator();
        help->Append(IDM_HOMESWEETHOME, "Notepad++ Home");
        help->Append(IDM_PROJECTPAGE, "Notepad++ Project Page");
        help->Append(IDM_ONLINEDOCUMENT, "Notepad++ Online User Manual");
        help->Append(IDM_FORUM, "Notepad++ Community (Forum)");
        help->AppendSeparator();
        help->Append(IDM_UPDATE_NPP, "Check for Updates");
        help->Append(IDM_CONFUPDATERPROXY, "Set Updater Proxy...");
        help->AppendSeparator();
        help->Append(IDM_DEBUGINFO, "Debug Info...");
        help->Append(IDM_ABOUT, "About wxNotepad++\tF1");
        mb->Append(help, "&About");
    }
}
