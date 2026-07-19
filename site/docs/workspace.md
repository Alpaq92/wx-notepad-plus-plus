# Folder as Workspace

Folder as Workspace is a docked directory tree — the lightweight way to work on a project without any
project file, configuration or indexing step.

## Opening it

| How | Behaviour |
| --- | --- |
| **View&nbsp;&rsaquo; Folder as Workspace** | toggles the panel. On first use it opens rooted at the current file's folder (or the working directory if the document is untitled) |
| **File&nbsp;&rsaquo; Open Folder as Workspace…** | opens a folder picker — pre-selecting the current file's folder — then roots the panel there and reveals it |

The panel docks to the left edge by default, at about 240&nbsp;px wide. Being a wxAUI pane, it can be
dragged to another edge, floated, resized or closed.

## Using it

- **Double-click a file** to open it in a tab.
- The tree includes a **filter selector**, so you can narrow the listing to particular file types.
- Folder and file icons are themed to match the editor, and are applied lazily as branches expand.
- Re-rooting the panel (via **Open Folder as Workspace…**) rebuilds the tree but keeps the pane where
  you put it — dock position, size and float state survive the change.

When the panel is re-rooted, the status bar confirms it with `Workspace: <path>`.

## Related

- **File&nbsp;&rsaquo; Open Containing Folder ▸** opens the current file's directory in the system file
  manager, or in a terminal or shell — the submenu lists what is actually installed on the machine
  rather than a fixed guess.
- **Project Panels 1–3** (**View&nbsp;&rsaquo; Project Panels**) are three separate project panes, kept
  independent of the workspace tree.
- The [Integrated Terminal](terminal.md) is the natural companion: browse in the tree, run commands in
  the terminal, edit in the tabs, without leaving the window.

## Scope, honestly stated

Folder as Workspace is a **file browser**, not an IDE project system. There is no build configuration,
no per-project settings file and no background indexing. If you want a persistent set of open documents
instead, use **File&nbsp;&rsaquo; Save Session…** — see
[Getting Started](getting-started.md#sessions).
