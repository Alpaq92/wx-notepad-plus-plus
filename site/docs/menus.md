# Menu Reference

wxNote has **eleven** top-level menus, in this order:

**File · Edit · Selection · Go · View · Document · Automation · Extensions · Settings · Window · Help**

A few notes before the list:

- The search menu is called **Go**, not "Search".
- **Language** and **Encoding** are submenus of **Document**, not top-level menus.
- **Macro**, **Run** and the hash generators live under **Automation**.
- Items marked *(dynamic)* are filled in at runtime rather than being fixed entries.
- Accelerators are listed in [Keyboard Shortcuts](shortcuts.md); only the most common are repeated here.

---

## File

**Open**

- **New** — <kbd>Ctrl</kbd>+<kbd>N</kbd>
- **Open…** — <kbd>Ctrl</kbd>+<kbd>O</kbd>
- **Open Containing Folder ▸** — the contents depend on what is actually installed on the machine.
  On Windows the fixed entries are *Explorer*, *cmd* and *PowerShell*, with anything else detected
  (pwsh, Cygwin, WSL) appended. On macOS the entry is *Finder*, on Linux *File Manager*, in both cases
  followed by the terminal applications found on the system.
- **Open in Default Viewer** — hands the file to the OS's default application for its type. The
  document must be saved first.
- **Open Folder as Workspace…** — see [Folder as Workspace](workspace.md)
- **Reload from Disk**

**Save**

- **Save** — <kbd>Ctrl</kbd>+<kbd>S</kbd>
- **Save As…** — <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>
- **Save a Copy As…**
- **Save All**

**Close**

- **Close** — <kbd>Ctrl</kbd>+<kbd>W</kbd>
- **Close All** — <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>W</kbd>
- **Close Multiple Documents ▸** — Close All but Active Document · Close All but Pinned Documents ·
  Close All to the Left · Close All to the Right · Close All Unchanged
- **Restore Recent Closed File** — <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd>

**The file on disk**

- **Rename…** — needs a saved file
- **Move to Recycle Bin** — needs a saved file

**Sessions, printing, exit**

- **Load Session…** · **Save Session…**
- **Print…** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) · **Print Now**
- **Recent Files** *(dynamic)* — length configured in **Preferences&nbsp;&rsaquo; Recent Files History**
- **Exit** — <kbd>Alt</kbd>+<kbd>F4</kbd>

---

## Edit

**Basics:** Undo · Redo · Cut · Copy · Paste · Delete.

**Transform submenus**

| Submenu | Contents |
| --- | --- |
| **Convert Case to ▸** | UPPERCASE · lowercase · Proper Case · Proper Case (blend) · Sentence case · Sentence case (blend) · iNVERT cASE · ranDOm CasE |
| **Comment/Uncomment ▸** | Toggle Single Line Comment · Single Line Comment · Single Line Uncomment · Block Comment · Block Uncomment |
| **Indent ▸** | Increase Line Indent · Decrease Line Indent |
| **Line Operations ▸** | Duplicate / Move Up / Move Down / Split / Join · Remove Duplicate Lines · Remove Consecutive Duplicate Lines · Remove Empty Lines (with or without blank characters) · Insert Blank Line Above / Below · Reverse Line Order · Randomize Line Order · and fourteen sort variants — lexicographic, lexicographic ignoring case, locale order, integer, decimal (comma), decimal (dot) and length, each ascending and descending |
| **Blank Operations ▸** | Trim Trailing / Leading / Both · EOL to Space · Trim both and EOL to Space · TAB to Space · Space to TAB (All) · Space to TAB (Leading) |
| **Paste Special ▸** | Copy as HTML · Copy as RTF · Paste HTML Content · Paste RTF Content · Copy / Cut / Paste Binary Content |
| **Auto-Completion ▸** | Function Completion · Word Completion · Function Parameters Hint (and Previous / Next Hint) · Path Completion |
| **EOL Conversion ▸** | Windows (CR LF) · Unix (LF) · Macintosh (CR) |
| **Insert ▸** | Date Time (short) · Date Time (long) · Date Time (customized) |
| **Copy to Clipboard ▸** | Copy Current Full File path · Copy Current Filename · Copy Current Dir. Path · Copy All Filenames · Copy All File Paths |

**Column editing**

- **Column Mode…** — an explainer dialog: rectangular selection is Alt+drag, Alt+Shift+arrows or
  Alt+Shift+click, and typing or pasting then applies to every line of the block at once.
- **Column Editor…** — <kbd>Alt</kbd>+<kbd>C</kbd> — insert text or an incrementing number series
  (decimal, hex, octal or binary, with optional leading zeros) down a rectangular block.

**Read-only**

- **Read-Only ▸** — Read-Only on Current Document · Read-Only for All Documents · Clear Read-Only for
  All Documents
- **Read-Only Attribute** — toggles the file's on-disk read-only attribute (the DOS
  read-only bit on Windows; the owner write permission on Linux/macOS)

---

## Selection

- **Select All** — <kbd>Ctrl</kbd>+<kbd>A</kbd>
- **Begin/End Select** · **Begin/End Select in Column Mode**
- **Multi-select All ▸** and **Multi-select Next ▸** — each with four matching modes: Ignore Case &amp;
  Whole Word · Match Case Only · Match Whole Word Only · Match Case &amp; Whole Word
- **Undo the Latest Added Multi-Select** · **Skip Current &amp; Go to Next Multi-select**
- **Character Panel** · **Clipboard History**
- **On Selection ▸** — Open File · Open Containing Folder (in Explorer / Finder / your file manager,
  depending on platform) · Redact Selection · Search on Internet · Change Search Engine…

---

## Go

**Find and replace**

- **Find…** (<kbd>Ctrl</kbd>+<kbd>F</kbd>) · **Replace…** (<kbd>Ctrl</kbd>+<kbd>H</kbd>) ·
  **Find in Files…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>) ·
  **Incremental Search** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd>)

The first three open one tabbed dialog with **Find**, **Replace**, **Find in Files** and **Mark**
tabs. Shared options: Match whole word only, Match case, Wrap around, plus Backward direction (Find and
Replace) and In selection (Replace and Mark). The Search Mode radio group offers **Normal**,
**Extended** (`\n`, `\r`, `\t`, `\0`, `\x…`) and **Regular expression**. Find in Files adds *Filters*
and *Directory* fields and a **Replace in Files** button.

The incremental search bar is a slim find-as-you-type strip with its own case, whole-word and regex
toggles and a live match count. <kbd>Enter</kbd> jumps to the next match, <kbd>Esc</kbd> closes it.

**Navigation**

- **Find Next** (<kbd>F3</kbd>) · **Find Previous** (<kbd>Shift</kbd>+<kbd>F3</kbd>)
- **Select and Find Next / Previous** (<kbd>Ctrl</kbd>+<kbd>F3</kbd> /
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F3</kbd>)
- **Find (Volatile) Next / Previous**
- **Search Results Window** (<kbd>F7</kbd>) · **Next / Previous Search Result** (<kbd>F4</kbd> /
  <kbd>Shift</kbd>+<kbd>F4</kbd>)
- **Go to…** (<kbd>Ctrl</kbd>+<kbd>G</kbd>) · **Go to Matching Brace** (<kbd>Ctrl</kbd>+<kbd>B</kbd>) ·
  **Select All In-between {} [] or ()** · **Find characters in range…**

**Marks and bookmarks**

- **Mark…** opens the Mark tab of the find dialog.
- **Bookmark ▸** — Toggle (<kbd>Ctrl</kbd>+<kbd>F2</kbd>) · Next (<kbd>F2</kbd>) · Previous
  (<kbd>Shift</kbd>+<kbd>F2</kbd>) · Clear All · Cut / Copy / Paste-to-replace / Remove bookmarked
  lines · Remove Non-Bookmarked Lines · Inverse Bookmarks
- **Style All Occurrences of Token ▸** and **Style One Token ▸** — five independent highlight styles
- **Clear Style ▸** — clear any one of the five, or all
- **Jump Up ▸** / **Jump Down ▸** — navigate between marks of a given style
- **Copy Styled Text ▸** — copy all text carrying a given style
- **Change History ▸** — Go to Next / Previous Change · Clear Change History. **Not functional in this
  build**: all three report *"not yet implemented"*, because change history needs a newer Scintilla
  than the one wxWidgets vendors. See the [FAQ](faq.md).

---

## View

**Panels:** Function List · Document Map · Document List · Folder as Workspace · Project Panels ▸ ·
Show Terminal · Monitoring (tail -f).

**Compare ▸** — a side-by-side diff of the current document against a file (**Compare with File…**) or
the clipboard (**Compare with Clipboard**), shown in the split view. Changed / added / removed lines are
colour-highlighted, the changed part *within* a line is underlined, blank filler keeps the two sides
row-aligned, and the panes scroll together. **Next / Previous Difference** jump between changes; **Clear
Compare** removes the highlighting. The two sides open as read-only scratch tabs, so your real files are
never modified.

**Spell Check** — toggles red squiggles under misspelled words (in the visible text), using the operating
system's own spell checker and dictionaries (Windows and macOS today; Linux is planned). Identifiers are
split on camelCase / snake_case boundaries before checking, so code isn't a sea of red.

**Display:** Word wrap · **Show Symbol ▸** (space and tab, end of line, non-printing characters,
control characters &amp; Unicode EOL, all characters, indent guide, wrap symbol) · **Zoom ▸** (In / Out /
Restore Default Zoom) · **Summary…**

**Folding:** Fold All (<kbd>Alt</kbd>+<kbd>0</kbd>) · Unfold All
(<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>0</kbd>) · Fold / Unfold Current Level · **Fold Level ▸** and
**Unfold Level ▸** (levels 1–8) · Hide Lines.

**Tabs and placement:** **Tab ▸** (direct access, first/last/next/previous, move within the bar, five
colour tags) · **Move/Clone Current Document ▸**.

**Window state:** Toggle Full Screen Mode (<kbd>F11</kbd>) · Always on Top · Post-It (<kbd>F12</kbd>) ·
Distraction Free Mode.

**Text direction:** Text Direction RTL · Text Direction LTR.

**View Current File in ▸** — Firefox · Chrome · Edge · IE.

---

## Document

- **Language ▸** *(dynamic)* — see [Languages &amp; Syntax](languages.md)
- **Encoding ▸** — an *interpret as* group (ANSI, UTF-8, UTF-8-BOM, UTF-16 BE BOM, UTF-16 LE BOM, plus
  **Character sets ▸** grouped by script: Arabic, Baltic, Celtic, Cyrillic, Central European, Chinese,
  Eastern European, Greek, Hebrew, Japanese, Korean, North European, Thai, Turkish, Western European,
  Vietnamese) and a **Convert to…** group that re-encodes the document on save.

The distinction matters: the top group *re-decodes* the bytes already on disk (use it when a file opened
as mojibake); the Convert to group *changes* the encoding the file will be written in.

---

## Automation

- **Macro ▸** — Start Recording · Stop Recording · Playback · Save Current Recorded Macro… · Run a Macro
  Multiple Times… Saved macros are appended to this menu.
- **Run ▸** — **Run…** (<kbd>F5</kbd>) and **Validate shortcuts.xml**, which forwards to the optional
  Notepad++ shortcut importer (see
  [Customizing Shortcuts](custom-shortcuts.md#importing-notepad-shortcuts)). The Run dialog takes a command line
  and substitutes variables such as `$(FULL_CURRENT_PATH)`, `$(CURRENT_DIRECTORY)`, `$(FILE_NAME)`,
  `$(CURRENT_WORD)` and `$(CURRENT_LINE)`. The last command is remembered.
- **SHA-256 ▸ · SHA-512 ▸ · SHA-1 ▸ · MD5 ▸** — each with *Generate…*, *Generate from files…* and
  *Generate from selection into clipboard*. The order is deliberate: the recommended digests lead and
  the legacy ones trail.

---

## Extensions

- **Open Plugins Folder…**
- Commands registered by loaded plugins are appended here at runtime.

See [Plugins](plugins.md).

---

## Settings

- **Preferences…** — see [Preferences](preferences.md)
- **Style Configurator…** · **Shortcut Mapper…** — the latter remaps keyboard shortcuts and switches
  keymap schemes; see [Customizing Shortcuts](custom-shortcuts.md)
- **Import ▸** — Import plugin(s)… · Import style theme(s)…
- **Edit Popup ContextMenu** — opens `contextMenu.xml` for editing, so the editor's right-click menu can
  be customised
- **Localization** *(dynamic)* — the UI-language picker, the same list as
  **Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Localization**

---

## Window

- **Sort By ▸** — Name, Path, Type, Content Length and Modified Time, each ascending and descending
- **Windows…** — the document list dialog
- **Recent Window** — an MRU entry; it starts disabled and becomes active once there is history

This menu is window management only. Configuration lives under **Settings**.

---

## Help

- **wxNote Homepage** · **wxNote on GitHub** · **wxNote Releases** · **wxNote Documentation** · **Report an Issue**
- **Check for Updates** — opens the Releases page
- **Command Line Arguments…** — a summary dialog. It is **abridged**: it lists only `-g`, `-e`, `-n`
  and `-r`. For `-w/--wait`, `--safe`, `-v` and `-h` see [Command Line](command-line.md)
- **Debug Info…** — wxWidgets version, OS description and the executable's path, useful when filing a
  bug report
- **About wxNote** — <kbd>F1</kbd>
