# Customizing Shortcuts

Every menu accelerator listed in [Keyboard Shortcuts](shortcuts.md) — and a curated set of the
editor's own Scintilla keys — can be remapped, cleared or reset. Three pieces work together:

- the **Shortcut Mapper** dialog (**Settings&nbsp;&rsaquo; Shortcut Mapper…**),
- a hand-editable **`shortcuts.json`** file in the per-user data directory,
- named, switchable **keymap schemes**, including an optional Notepad++ `shortcuts.xml` importer.

Edits apply immediately and are saved immediately — there is no Apply step, and nothing waits for
exit.

## The Shortcut Mapper

**Settings&nbsp;&rsaquo; Shortcut Mapper…** opens a searchable grid over every remappable command.
Each row shows the **Command** name (in the UI language), its effective **Shortcut**, its **Scope**
(*Global*, *Editor* or *Terminal*) and its **Source** — *Default* for a stock binding, *Scheme* when
the active keymap scheme supplies it, *User* for your own override.

- The **filter field** narrows the grid as you type; space-separated terms all have to match the
  command name, the shortcut or the scope, so `line ctrl` finds the line operations bound to a
  <kbd>Ctrl</kbd> key.
- **Find by shortcut** is the reverse lookup: arm the toggle, press a key combination, and the grid
  lists the menu commands that combination is bound to.
- **Modify…** (or a double-click) opens a capture dialog: just press the new combination whole —
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> arrives as <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> —
  or compose it from the modifier checkboxes and a key. On macOS the <kbd>Ctrl</kbd> token means
  **Command**; a separate *Control (physical)* checkbox picks the real Control key instead.
- **Clear** is an explicit, persisted unbind — the command stays key-less even if a later wxNote
  version ships a new default for it. **Reset to default** removes your override and returns the
  stock binding.

Changes take effect live (menu labels, the accelerator table and the editor keymap all refresh) and
are written to `shortcuts.json` at once.

### Conflict detection

A live conflict engine checks every binding against every other — menu accelerators, the editor's
built-in Scintilla keys and the terminal's fixed chords — and tints conflicted rows red, with a
detail line under the grid. Assigning a key that is already taken prompts, and the prompt depends on
what kind of collision it is:

| Collision | What happens |
| --- | --- |
| **Hard conflict** — the key already runs a *different* command in a scope that can be active at the same time | A prompt offers **Reassign** (steal the key — only the colliding key is removed from the other command, any other keys it holds survive), **Keep both**, or **Cancel**. For editor-command rows the choice is keep-both or cancel — there is no steal on that tier. |
| **Shadow** — a menu accelerator masks a *different* built-in editor key (e.g. putting <kbd>Ctrl</kbd>+<kbd>L</kbd> over the editor's *line cut*) | A warning names the shadowed editor command; you can assign anyway. |
| **Same action** — a menu accelerator over the Scintilla key that does the same thing (<kbd>Ctrl</kbd>+<kbd>C</kbd> over the editor's copy) | Silent — nothing is wrong. |
| **Separate scopes** — the same key in scopes that are never active together (a terminal chord vs. an editor command) | Silent — this coexistence is intentional. |

### Editor commands

Besides the menu accelerators, the Mapper lists a **curated set of 24 Scintilla editor commands**
with Scope *Editor*: word and paragraph movement (and their selection-extending variants), word-part
movement, document start/end, line scrolling, display-line home/end, line cut/copy/delete, word and
line deletion, and overtype toggle. These are applied through the editor's own keymap rather than the
menu, and persist in their own `"editor"` section of `shortcuts.json`.

The set is deliberately curated, not the whole Scintilla table: commands that already have a menu row
(Copy, Undo, Select All, …) appear only once, as that menu row; and the bare caret keys (unmodified
arrows, Home, End) are excluded so a stray rebind can never strand you without basic editing keys.

## The `shortcuts.json` file

All shortcut customisation persists in one file, `shortcuts.json`, in the per-user data directory —
the same place that holds recovery backups and `contextMenu.xml` (see
[FAQ&nbsp;&rsaquo; Where does it keep my settings?](faq.md#where-does-it-keep-my-settings)).

The format is a **delta file** in the VS&nbsp;Code style: it stores only your *changes*, never the
whole keymap. Defaults are compiled into the application, so a missing or empty file simply means
"stock keys", and deleting the file resets everything.

```json
{
  "version": 1,
  "activeScheme": "wxnote.default",
  "userKeybindings": [
    { "command": "file.saveAll", "key": "Ctrl+Shift+A" },
    { "command": "-view.tab.tab9" }
  ],
  "editor": [
    { "command": "editor.lineCut", "key": "Ctrl+Shift+X" },
    { "command": "-editor.toggleOvertype" }
  ]
}
```

- **Commands are stable symbolic names** (`file.saveAll`, `edit.lineOperations.duplicateLine`,
  `editor.lineCut`), independent of the UI language. The quickest way to discover a name is to make
  the change in the Mapper and read the file it writes.
- **A leading `-` unbinds**: `{ "command": "-view.tab.tab9" }` removes every key from that command;
  add a `"key"` to remove only that one binding and keep the rest.
- **Key spellings are invariant English** (`Ctrl+Shift+K`, `Alt+F4`) regardless of the UI language,
  so a rebind survives a language switch. On macOS `Ctrl` means Command; `RawCtrl` is the physical
  Control key.
- An optional `"when"` field scopes an entry — `"global"` (the default), `"editor"` or `"terminal"`.
- The `"editor"` section holds the curated editor-command overrides, same entry shape, `editor.*`
  names only.

The file is meant to be **hand-edited** — changes you make by hand are picked up on the next start.
It is also built to be safe: writes are atomic, a damaged file degrades to the defaults instead of
breaking startup, entries this build doesn't recognise are kept rather than deleted, and a file
written by a *newer* wxNote (a higher `"version"`) is opened read-only — the Mapper shows a
*Read-Only* marker and disables editing rather than clobbering it.

## Keymap schemes

A **scheme** is a named, switchable set of divergences from wxNote's own defaults — the model
JetBrains IDEs use for their keymaps. The picker at the top of the Shortcut Mapper switches schemes;
the choice persists as `"activeScheme"`.

One bundled, read-only preset ships: **wxNote default** — the stock keys; an empty delta. The stock
keys follow the consensus of today's mainstream editors (VS&nbsp;Code, JetBrains, Sublime&nbsp;Text,
…) on the classic CUA base Notepad++ shares. There is deliberately no bundled Notepad++ preset: the
headline bindings (Save, Find, Undo, …) already match — and rather than shipping a guess at the
remaining divergences, wxNote imports your *actual* Notepad++ keys from a `shortcuts.xml` via the
optional `npp-shortcuts-compat` plugin (see
[Importing Notepad++ shortcuts](#importing-notepad-shortcuts)). If your `shortcuts.json` still
selects a scheme that no longer exists, the stock keys apply and the selection is migrated back to
*wxNote default* the next time the file is written.

Your own Mapper edits live in a **user layer above the active scheme**: they win over whatever the
scheme says, and switching schemes never discards them. Bundled presets themselves can never be
edited in place.

User-defined schemes are plain JSON in the same file — an `id`, a display `name`, an optional
`parent` to delta against, and the same entry shape (`"keybindings"` for menu commands, `"editor"`
for editor commands):

```json
"schemes": [
  { "id": "my.keys", "name": "My keys", "parent": "wxnote.default",
    "keybindings": [ { "command": "edit.commentUncomment.setSingle", "key": "Ctrl+K" } ] }
]
```

## Importing Notepad++ shortcuts

The optional GPL module **`npp-shortcuts-compat`** imports a Notepad++ **`shortcuts.xml`** and
re-adds those keys as a **"Notepad++ (imported)"** scheme. Like the other compatibility modules it is
a separate plugin — the core never learns the `shortcuts.xml` format (see [Plugins](plugins.md)).

Run **Automation&nbsp;&rsaquo; Run&nbsp;&rsaquo; Validate shortcuts.xml**, or the plugin's own
**Import Notepad++ shortcuts.xml…** command in the **Extensions** menu — both are the same command.
It looks for a `shortcuts.xml` in wxNote's per-user data directory (the folder that holds
`shortcuts.json`); on Windows an installed Notepad++'s `%APPDATA%\Notepad++\shortcuts.xml` is also
found automatically. The import then:

- registers the bindings as the **"Notepad++ (imported)"** scheme. It does **not** switch to it —
  pick it in the Shortcut Mapper's scheme picker to apply it. Imported Scintilla editor keys are
  stored with the scheme and take effect the same way, on activation.
- shows a **validation report**: how many bindings were imported, how many keys had no portable
  equivalent, and how many targets this build doesn't know.

The imported scheme is written to `shortcuts.json` and **outlives the plugin** — import once and the
keys survive even if the importer is later removed. If the plugin is not installed, the *Validate
shortcuts.xml* menu item says so in the status bar and does nothing else.

Two safety properties are worth knowing. A Notepad++ `shortcuts.xml` can contain
`UserDefinedCommands` (shell command lines) and `Macros`; the importer parses them **as data only**,
lists them in the report for review, and **never executes anything** — it has no execution path at
all. And the XML parser is hardened: no DTD or external-entity expansion, bounded reads, untrusted
values clamped.

## For plugin authors

The import path is not special-cased: any Nib plugin can register keymap schemes through the
**`nib.keymap/1`** capability — begin a scheme, add bindings by command id, symbolic name or
Scintilla editor key, then commit it (optionally activating it). See [Plugins](plugins.md).
