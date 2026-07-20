# The Interface

The window is a wxAUI layout: a menu bar, an optional toolbar, a tabbed editing area (optionally split
into two views), any number of dockable panels, and a status bar.

## Toolbar

The toolbar is optional (**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Show toolbar**) and its icons
are SVG, so they stay crisp at any size. Four icon sets ship:

- **Tabler icons (line)** — the theme-adaptive default,
- **Solar icons (green)**,
- **IconPark icons (teal/lime)**,
- **Streamline icons (green/teal)**.

Both the set and the icon size (16, 20, 24 or 32&nbsp;px) are chosen in
**Preferences&nbsp;&rsaquo; General** and applied on restart. Buttons that do not apply right now are
greyed out — Save with no changes, Undo/Redo with no history, Cut/Copy/Delete with no selection, Paste
with an empty clipboard.

The toolbar can be set to hide itself in full-screen mode
(**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Auto-hide toolbar in full screen**, off by default).

## Tabs

Every open document is a tab. Beyond switching, tabs support:

- **Direct access** — <kbd>Ctrl</kbd>+<kbd>1</kbd>…<kbd>Ctrl</kbd>+<kbd>9</kbd> for the first nine tabs,
  <kbd>Ctrl</kbd>+<kbd>Tab</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> for next/previous,
  plus First/Last under **View&nbsp;&rsaquo; Tab**.
- **Reordering** — Move Tab Forward / Backward, Move to Start / End.
- **Colour tags** — five palette colours plus Remove Color, under **View&nbsp;&rsaquo; Tab**.
- **Pinning**, and a **Close All but Pinned Documents** command under
  **File&nbsp;&rsaquo; Close Multiple Documents**.
- **A close button per tab**, toggled in **Preferences&nbsp;&rsaquo; Tab Bar** (applied on restart).

An asterisk in the tab title marks unsaved changes.

## Split view

wxNote has two editor views. **View&nbsp;&rsaquo; Move/Clone Current Document** offers:

| Command | Effect |
| --- | --- |
| Move to Other View | move the document to the other view |
| Clone to Other View | show the same document in both views |
| Move to New Instance | move it into a separate wxNote process |
| Open in New Instance | open a second copy of it in a separate process |

## Panels

All panels are dockable wxAUI panes — drag them to another edge, or float them.

| Panel | Where | What it does |
| --- | --- | --- |
| Function List | View&nbsp;&rsaquo; Function List | parsed symbol list for the active document |
| Document Map | View&nbsp;&rsaquo; Document Map | zoomed-out overview of the whole file |
| Document List | View&nbsp;&rsaquo; Document List | flat list of every open document |
| Folder as Workspace | View&nbsp;&rsaquo; Folder as Workspace | a project tree — see [Folder as Workspace](workspace.md) |
| Project Panels 1–3 | View&nbsp;&rsaquo; Project Panels | three independent project panes |
| Terminal | View&nbsp;&rsaquo; Show Terminal | shell tabs — see [Integrated Terminal](terminal.md) |
| Character Panel | Selection&nbsp;&rsaquo; Character Panel | character/codepoint inserter |
| Clipboard History | Selection&nbsp;&rsaquo; Clipboard History | recent clipboard entries |
| Search results | Go&nbsp;&rsaquo; Search Results Window (<kbd>F7</kbd>) | Find in Files / Find All hits |

## Window modes

| Mode | Menu | Key |
| --- | --- | --- |
| Full screen | View&nbsp;&rsaquo; Toggle Full Screen Mode | <kbd>F11</kbd> |
| Post-It (chrome-less sticky window) | View&nbsp;&rsaquo; Post-It | <kbd>F12</kbd> |
| Distraction Free | View&nbsp;&rsaquo; Distraction Free Mode | — |
| Always on Top | View&nbsp;&rsaquo; Always on Top | — |

## Monitoring (tail -f)

**View&nbsp;&rsaquo; Monitoring (tail -f)** watches the active document on disk and appends new content
as it is written — useful for log files. The document must be saved to disk first; monitoring an
untitled buffer reports *"Monitoring needs a file on disk - save the document first"*.

## Status bar

Eight fields, left to right:

| # | Content |
| --- | --- |
| 0 | the document's language (or a transient status message) |
| 1 | `length : … lines : …` |
| 2 | `Ln : … Col : … Pos : …` |
| 3 | `Sel : … \| …` — selected characters and lines |
| 4 | line-ending mode |
| 5 | encoding |
| 6 | zoom (an editable percentage combo) |
| 7 | `INS` / `OVR` typing mode |

The bar is **interactive** — double-click a field to act on it:

- field 2 (Ln:Col:Pos) opens **Go to…**,
- field 4 opens the line-ending conversion popup,
- field 5 opens the encoding re-interpret / convert popup,
- field 7 toggles insert / overtype.

Field 6 is different: it holds a small child control rather than text, so it is not double-clickable.
The zoom field accepts a typed percentage, has a ▼ that opens a preset list upward, and responds to the
mouse wheel. It is **off by default** — enable it with
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Show zoom control in status bar**, which applies
immediately and collapses the field to nothing when switched back off. Hiding it does not affect
zooming itself (<kbd>Ctrl</kbd>+mouse wheel and **View&nbsp;&rsaquo; Zoom** keep working).

Because Scintilla zooms by whole **points** rather than by percent, the field snaps to the value it
could actually apply — type `125%` at the usual 10&nbsp;pt base and it will answer `130%`. The full
explanation, including the reachable range, is in
[Preferences&nbsp;&rsaquo; The zoom control](preferences.md#the-zoom-control).

With a split view, each editor keeps its own zoom level, and the field re-reads the pane you switch to.

The whole status bar can be hidden with
**Preferences&nbsp;&rsaquo; General&nbsp;&rsaquo; Show status bar**.

## Document summary

**View&nbsp;&rsaquo; Summary…** reports counts for the current document, including characters (counted
as Unicode codepoints, not bytes) and words.
