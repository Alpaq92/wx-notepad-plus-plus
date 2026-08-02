# Quick Open &amp; Command Palette

Two keyboard-first pickers that share the same control: one finds **files**, the other finds
**commands**. Both filter as you type, both are fuzzy, and both are driven entirely from the keyboard.

| | Shortcut | Finds |
| --- | --- | --- |
| **Quick Open** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd> | files, and symbols in the current file |
| **Command Palette** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> | every command in the app |

Both are also on the **Search** menu, and both can be rebound in
[Customizing Shortcuts](custom-shortcuts.md).

> <kbd>Ctrl</kbd>+<kbd>P</kbd> is **Print**, as in Notepad++, and stays that way. Quick Open is the
> newcomer, so it takes a free chord rather than displacing a binding people already have in muscle
> memory.

## Typing in either one

| Key | Does |
| --- | --- |
| any character | filters the list and re-ranks it |
| <kbd>↑</kbd> / <kbd>↓</kbd> | move the selection |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> / <kbd>Ctrl</kbd>+<kbd>N</kbd> | move the selection, for the muscle memory of everyone who has used a picker like this elsewhere |
| <kbd>Page&nbsp;Up</kbd> / <kbd>Page&nbsp;Down</kbd> | move ten at a time |
| <kbd>Enter</kbd> | open the file, or run the command |
| <kbd>Esc</kbd> | close, changing nothing |

The characters your query matched are highlighted in each row, so you can see *why* something ranked
where it did.

## How the matching works

You do not have to type a substring. The query only has to appear **in order**, so initials work:

- `fmh` finds `fuzzy_match.h`
- `cp` finds **C**ommand **P**alette
- `otv` finds `OpenTheVault`

Ranking favours, in rough order of weight:

- **runs of consecutive characters** over the same letters spread apart
- **word boundaries** — the start of a word, after `_` or `-`, and camelCase humps
- **the file name** over the directories above it
- **an exact-case hit** over a case-insensitive one

Case is handled the way you would expect: an all-lowercase query ignores case, but as soon as you type
a capital letter the query becomes case-sensitive.

## Quick Open

The list is assembled from three places, in this order:

1. **Files already open**, in either editor view — marked `open`
2. **Recently opened files** — marked `recent`
3. **Every file in the current workspace**

Picking a file that is already open **activates its tab** rather than opening a second copy of it.

If you have text selected when you press the shortcut, that text is already typed into the query — so
selecting a file name in the source and hitting <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd> takes you
straight there.

### The workspace list

The third group comes from the folder you opened with
[**File &rsaquo; Open Folder as Workspace…**](workspace.md). Without a workspace open, Quick Open still
lists your open and recent files — it just has no project to search.

The list is built in the background, a few milliseconds at a time, so opening a large project never
stalls the editor. While it is still building, the title bar of the dialog says `(indexing…)`; you can
type and pick from what has been found so far. The scan works outward from the workspace root, so the
files nearest the top are found first.

These directories are skipped, because nothing in them is what you meant:

`.git` `.hg` `.svn` `.bzr` `CVS` `node_modules` `bower_components` `vendor` `.venv` `venv`
`__pycache__` `.mypy_cache` `.pytest_cache` `.tox` `.gradle` `.idea` `.vs` `.vscode` `build`
`cmake-build-debug` `cmake-build-release` `out` `dist` `target` `bin` `obj` `.cache` `.next` `.nuxt`
`.terraform` `deps` `_deps`

Binary and generated files — executables, archives, images, media, fonts, compiled catalogs — are left
out too. Files without an extension are kept.

Very large trees stop at 50,000 files.

### Jumping to a symbol: `@`

Type **`@`** as the first character and the same list switches to the **symbols in the current file** —
the functions, classes and sections the [Function List](interface.md) panel shows — with their line
numbers. Everything after the `@` filters that list:

```
@parse     →  parseFuncList
```

Delete the `@` and you are back to files. Symbols come from the same engine as the Function List, so
whatever it recognises for a language, this does too.

## Command Palette

The palette lists **every command in the application**, with its menu path underneath and its current
keyboard shortcut on the right — which makes it a fast way to *discover* a shortcut, not just to run
something.

It is read from the live menu bar rather than a separate list, so it always includes:

- **translated labels**, in whatever language the UI is running in
- **your own remapped shortcuts**, not the defaults
- the **language list**, **saved macros**, and any **plugin commands**

Commands that are not available right now — Paste with an empty clipboard, Save with nothing modified —
are shown **greyed out** rather than hidden, so you can see they exist and why nothing happened.
Pressing <kbd>Enter</kbd> on one does nothing.

## See also

- [Folder as Workspace](workspace.md) — the directory tree Quick Open indexes
- [Keyboard Shortcuts](shortcuts.md) — the full reference
- [Customizing Shortcuts](custom-shortcuts.md) — rebinding either picker
