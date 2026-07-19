# Keyboard Shortcuts

Every shortcut below is a real menu accelerator taken from the application's menu definitions — the
**defaults**, i.e. what the bundled *wxNote default* keymap scheme gives you. All of them can be
remapped, cleared or reset in **Settings&nbsp;&rsaquo; Shortcut Mapper…**, along with a curated set of
the editor's own Scintilla keys (word-wise movement, line deletion, and so on) that are not menu
accelerators and therefore not listed here. See [Customizing Shortcuts](custom-shortcuts.md).

The defaults follow the consensus of today's mainstream editors on the classic CUA base. Where two
keys are listed below (Redo, Close), **both are active by default** — the menu label shows the first;
remapping the command in the Shortcut Mapper replaces both.

> On macOS, wxWidgets maps the <kbd>Ctrl</kbd> in a menu accelerator to the **Command** key.

## Files

| Action | Shortcut |
| --- | --- |
| New | <kbd>Ctrl</kbd>+<kbd>N</kbd> |
| Open… | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save | <kbd>Ctrl</kbd>+<kbd>S</kbd> |
| Save As… | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Close | <kbd>Ctrl</kbd>+<kbd>W</kbd> or <kbd>Ctrl</kbd>+<kbd>F4</kbd> |
| Close All | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>W</kbd> |
| Restore Recent Closed File | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> |
| Print… | <kbd>Ctrl</kbd>+<kbd>P</kbd> |
| Exit | <kbd>Alt</kbd>+<kbd>F4</kbd> |

**Save All** ships without a key of its own — it is **File&nbsp;&rsaquo; Save All**, or give it one in
the Shortcut Mapper.

## Editing

| Action | Shortcut |
| --- | --- |
| Undo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> |
| Redo | <kbd>Ctrl</kbd>+<kbd>Y</kbd> or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> |
| Cut | <kbd>Ctrl</kbd>+<kbd>X</kbd> |
| Copy | <kbd>Ctrl</kbd>+<kbd>C</kbd> |
| Paste | <kbd>Ctrl</kbd>+<kbd>V</kbd> |
| Delete | <kbd>Del</kbd> |
| Select All | <kbd>Ctrl</kbd>+<kbd>A</kbd> |
| Multi-select Next (Ignore Case &amp; Whole Word) | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| UPPERCASE | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>U</kbd> |
| lowercase | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Duplicate Current Line | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd> |
| Split Lines | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Join Lines | <kbd>Ctrl</kbd>+<kbd>J</kbd> |
| Move Up Current Line | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Up</kbd> |
| Move Down Current Line | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Down</kbd> |
| Insert Blank Line Below Current | <kbd>Ctrl</kbd>+<kbd>Enter</kbd> |
| Increase Line Indent | <kbd>Ctrl</kbd>+<kbd>]</kbd> |
| Decrease Line Indent | <kbd>Ctrl</kbd>+<kbd>[</kbd> |
| Toggle Single Line Comment | <kbd>Ctrl</kbd>+<kbd>/</kbd> |
| Block Comment | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Q</kbd> |
| Function Completion | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Column Editor… | <kbd>Alt</kbd>+<kbd>C</kbd> |

## Search and navigation

| Action | Shortcut |
| --- | --- |
| Find… | <kbd>Ctrl</kbd>+<kbd>F</kbd> |
| Replace… | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Find in Files… | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> |
| Incremental Search | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd> |
| Find Next | <kbd>F3</kbd> |
| Find Previous | <kbd>Shift</kbd>+<kbd>F3</kbd> |
| Select and Find Next | <kbd>Ctrl</kbd>+<kbd>F3</kbd> |
| Select and Find Previous | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F3</kbd> |
| Search Results Window | <kbd>F7</kbd> |
| Next Search Result | <kbd>F4</kbd> |
| Previous Search Result | <kbd>Shift</kbd>+<kbd>F4</kbd> |
| Go to… | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Go to Matching Brace | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Toggle Bookmark | <kbd>Ctrl</kbd>+<kbd>F2</kbd> |
| Next Bookmark | <kbd>F2</kbd> |
| Previous Bookmark | <kbd>Shift</kbd>+<kbd>F2</kbd> |

## View, tabs and folding

| Action | Shortcut |
| --- | --- |
| Toggle Full Screen Mode | <kbd>F11</kbd> |
| Post-It | <kbd>F12</kbd> |
| Show Terminal | <kbd>Ctrl</kbd>+<kbd>`</kbd> |
| Zoom In | <kbd>Ctrl</kbd>+<kbd>=</kbd> |
| Zoom Out | <kbd>Ctrl</kbd>+<kbd>-</kbd> |
| 1st…9th Tab | <kbd>Ctrl</kbd>+<kbd>1</kbd> … <kbd>Ctrl</kbd>+<kbd>9</kbd> |
| Next Tab | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Previous Tab | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Fold All | <kbd>Alt</kbd>+<kbd>0</kbd> |
| Unfold All | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>0</kbd> |
| Fold Current Level | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> |
| Unfold Current Level | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> |
| Fold to level 1…8 | <kbd>Alt</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>8</kbd> |
| Unfold level 1…8 | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>8</kbd> |

<kbd>Ctrl</kbd>+<kbd>=</kbd> is the unshifted <kbd>+</kbd> key, and <kbd>Ctrl</kbd>+mouse wheel zooms
as well. **Restore Default Zoom** has no accelerator of its own — it is
**View&nbsp;&rsaquo; Zoom&nbsp;&rsaquo; Restore Default Zoom**.

## Other

| Action | Shortcut |
| --- | --- |
| Run… | <kbd>F5</kbd> |
| About wxNote | <kbd>F1</kbd> |

## Mouse and modifier gestures

| Gesture | Effect |
| --- | --- |
| <kbd>Alt</kbd>+drag | rectangular (column) selection |
| <kbd>Alt</kbd>+<kbd>Shift</kbd>+arrows | extend a rectangular selection by keyboard |
| <kbd>Alt</kbd>+<kbd>Shift</kbd>+click | rectangular selection to the clicked point |
| <kbd>Ctrl</kbd>+mouse wheel | zoom in / out |
| <kbd>Insert</kbd> | toggle insert / overtype (mirrored in status-bar field 7) |
| double-click a status-bar field | act on it — see [The Interface](interface.md#status-bar) |

With a rectangular selection active, typing or pasting applies to every line of the block at once.
