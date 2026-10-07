# Settings, key bindings and wxNote's other files — design

**Status: implemented on `feature/yaml-settings`; not yet released.** This document is the
reference for every file wxNote reads or writes for itself: what each holds, who writes it, and how
it is read back. It replaces the mix that came before — the registry or an INI file (wxConfig),
`shortcuts.json`, Notepad++-shaped XML (themes, sessions, workspaces, the context menu) and line
formats of our own (`functionList.conf`, `snippets.txt`, `macros.dat`, `runcommands.dat`,
`plugins.dat`) — with one.

## Decisions

1. **One format: YAML.** Everything wxNote writes for itself is YAML. It is the smallest of the three
   candidates for hand-written files (preferences: YAML 0.8 KB, JSON 1.1 KB, XML 1.7 KB), it allows
   comments, multi-line text needs no escaping (snippet bodies, regexes), and its block style is what
   people expect from a settings file.
2. **One library: rapidyaml** (MIT), the release's single header, fetched and pinned by hash at
   configure time (`CMakeLists.txt`). It also reads the one JSON document wxNote consumes — the signed
   Plugins Admin catalog, which stays JSON because it is published, not ours to reshape. The
   hand-written JSON reader (`src/json_value.h`) and every use of `wxXmlDocument` leave the core;
   `wx::xml` is no longer linked.
3. **`src/yaml_io.h` is the only door.** No store talks to rapidyaml directly. It fixes the rules
   once: errors are reported with a line and column and never thrown or fatal; values are read as
   the caller expects ("4" is an integer when an integer is asked for); aliases are never expanded;
   what is written reads back unchanged in any YAML parser. `tests/yaml_io_test.cpp` pins all of it.
4. **External formats stay what they are.** TextMate grammars, the Plugins Admin catalog, Notepad++
   user-defined languages, SVG icons and the Windows manifest are other people's formats.
5. **Notepad++'s formats live only in GPL packages.** The core reads and writes YAML alone. Reading
   Notepad++'s `shortcuts.xml`, `config.xml`, themes, sessions, workspaces and `contextMenu.xml` is
   the job of `packages/npp-compat` (and `npp-bridge`, for the session messages Notepad++ plugins
   send), as reading `userDefineLang.xml` already is the job of `udl-compat`. See *Notepad++* below.

## What the other editors do

Researched for this design: VS Code, Sublime Text, Pulsar (Atom), TextMate 2, Notepad4 and the
JetBrains IDEs. Notepad++ itself, the one wxNote's file names came from, is the outlier: one
`config.xml` holding preferences, window position, recent files and find history together, rewritten
on every exit.

| | Settings file | Holds | Per-language | Key bindings |
| --- | --- | --- | --- | --- |
| **VS Code** | `settings.json` (JSON + comments) | only what differs from the defaults | `"[python]": {…}` in the same file | `keybindings.json`: a list of `{key, command, when}`, applied over the defaults; `-command` removes |
| **Sublime Text** | `Preferences.sublime-settings` (JSON + comments) | user values over a read-only default file | a file per syntax | `.sublime-keymap`: a list of `{keys, command, args, context}` |
| **Pulsar** | `config.cson` | user values; schema defaults in code | scope selectors (`'.source.python':`) | `keymap.cson`: selector → key → command; `unset!` removes |
| **TextMate 2** | `.tm_properties` (INI-like, per folder) | overrides by folder, glob and scope | scope sections | per bundle item; Cocoa `KeyBindings.dict` |
| **Notepad4** | `Notepad4.ini` | only non-default values | a section per scheme | not customisable |
| **JetBrains** | `options/*.xml` per component | only non-default values | code-style per language | `keymaps/*.xml`: a delta over a `parent` keymap |

What they agree on, and wxNote adopts:

- **Defaults live in the program; the user's file holds only differences.** A missing file is "all
  defaults", deleting a line resets one setting, and a new default in a later version reaches every
  user who never changed it.
- **Settings have namespaced, stable IDs** (`editor.tabSize`), independent of the UI language.
- **Per-language values sit in the same file**, under the language.
- **What the app remembers is not a setting.** Window position, recent files, the session and find
  history change constantly; VS Code, Sublime, Pulsar and JetBrains all keep them apart from the file
  people edit, so the app never rewrites the user's settings just because a window moved.
- **A key binding is a rule** — key, command, optional condition — applied over a base keymap, later
  rules winning, with an explicit way to remove a default. wxNote's keymap already worked this way
  (VS Code's rules, JetBrains' parent schemes); only its file format changes.

## Files

All in the per-user data directory (`%APPDATA%\wxNote`, `~/.wxNote`, `~/Library/Application
Support/wxNote`; a throwaway directory under `--sandbox`), except the read-only defaults installed
next to the executable.

| File | Owner | Holds | Was |
| --- | --- | --- | --- |
| `settings.yaml` | **you** (wxNote edits single lines) | preferences that differ from the defaults | wxConfig: registry / INI |
| `state.yaml` | wxNote | window, zoom, recent files, last session, recovery index, plugin on/off | wxConfig, `plugins.dat` |
| `keybindings.yaml` | you + Shortcut Mapper | key-binding rules and your schemes | `shortcuts.json` |
| `themes/<name>.yaml` | you + Style Configurator | a colour theme | `themes/*.xml`, `stylers.model.xml` |
| `contextmenu.yaml` | you | the editor's right-click menu | `contextMenu.xml` |
| `languages.yaml` | you + Style Configurator | your changes to each language: file names, comment tokens, keyword lists | — (built in) |
| `functionlist.yaml` | you | Function List rules for more languages | `functionList.conf` |
| `snippets.yaml` | you | your snippets | `snippets.txt` |
| `macros.yaml` | wxNote | saved macros | `macros.dat` |
| `runcommands.yaml` | wxNote | saved Run commands | `runcommands.dat` |
| sessions, workspaces | wherever you save them | File ▸ Save Session, Project panels | Notepad++-shaped XML |

The shipped defaults next to the executable: `themes/*.yaml` (with `Default.yaml`, formerly
`stylers.model.xml`) and `contextmenu.yaml`. A user file of the same name wins.

### How every file is read

- **A file that does not parse is not used, and not overwritten.** wxNote carries on without it — on
  the defaults for `settings.yaml` and `keybindings.yaml`, on the built-in menu for
  `contextmenu.yaml`, on the built-in colours for a theme, on the built-in language definitions for
  `languages.yaml`, without the user's additions for `snippets.yaml` and `functionlist.yaml` — and
  the status bar says which file and where it broke
  (line and column): at startup for the files read then, otherwise when the file is next read.
  Saving to a broken `settings.yaml` or `keybindings.yaml` is refused until it is fixed (the
  Shortcut Mapper shows *Read-Only*) — writing would throw the user's whole file away (VS Code
  behaves the same way); a broken `macros.yaml` or `runcommands.yaml` makes that saved list
  read-only the same way. Only `state.yaml`, which holds nothing the user wrote, is simply rewritten.
- **A value of the wrong type falls back to the default for that one setting**, never for the file.
- **Unknown keys are kept.** A setting from a newer version, or a typo, survives every save.
- **A file is read whole or not at all.** One caught while another program writes it in place is read
  again; one that still cannot be read whole is treated as unreadable - never as empty, which the next
  save would then write over the user's. A UTF-8 byte-order mark is fine; a `settings.yaml` holding a
  second YAML document (`---`) is not edited, since writing it would drop the second.
- **Writes are atomic**: a complete temporary file is renamed over the old one, so a crash or a full
  disk leaves the previous good file.

## `settings.yaml`

Flat, dotted IDs at the top level, one setting per line, VS Code style:

```yaml
# My wxNote settings. Only what differs from the defaults is here.
editor.fontFamily: JetBrains Mono
editor.tabSize: 2        # the projects I work on
editor.useTabs: false
ui.themeMode: dark
files.associations:
  inc: PHP
  h: C++
languages:
  Python:
    editor.tabSize: 4
  Makefile:
    editor.useTabs: true
```

**Why flat IDs and not nested maps.** Each setting is then one top-level line, which is what lets
wxNote change a setting without disturbing the rest of the file (below), and the ID in the file is
the ID in the documentation and in any error message. YAML's nesting is still used where a value is
itself a map (`files.associations`, `languages`).

**How wxNote writes it — comments survive.** rapidyaml cannot carry comments through a parse and
re-emit, so wxNote does not regenerate this file. It edits it, line by line, as VS Code edits
`settings.json`:

- changing a setting replaces the value on its line and keeps the line's comment;
- setting one back to its default deletes its line (the file stays a list of differences);
- a new setting is added after the last line of its namespace (`editor.*` next to `editor.*`), or
  at the end;
- a map value (`files.associations`) is replaced as a block.

After every edit the new text is parsed again and checked against what was meant; if it does not
match — a layout the line editor does not understand, such as a flow map `{…}` at the top level —
wxNote falls back to writing the file out in full (comments are then lost, never values). The Style
Configurator's extension list is the only place that writes inside a block. `languages` is written
by hand only.

**Per-language values.** `languages:` maps a Language-menu name (`Python`, `C++`, `Shell`) to the
same IDs. A per-language value beats the general one. The settings that make sense per language are
marked so in the table: today the indentation pair.

**The settings.** Values are names, not numbers, wherever the old store kept an index
(`ui.themeMode: dark`, not `1`), so a hand-edited file means what it says.

| ID | Type | Default | Was (wxConfig) |
| --- | --- | --- | --- |
| `ui.language` | language code, or `system` | `system` | `UILanguage` (a wxLanguage number) |
| `ui.themeMode` | `system` / `light` / `dark` | `system` | `ThemeMode` 0/1/2 |
| `ui.colorTheme` | theme name; empty = follow `ui.themeMode` | — | `Theme` |
| `ui.toolbar.visible` | bool | `true` | `View/Toolbar` |
| `ui.toolbar.iconStyle` | `line` / `solar` / `iconpark` / `streamline` | `solar` | `ToolbarIconStyle` 0–3 |
| `ui.toolbar.iconSize` | 12–64 (px) | `16` | `ToolbarIconSize` |
| `ui.statusBar.visible` | bool | `true` | `View/StatusBar` |
| `ui.statusBar.zoomField` | bool | `false` | `View/ZoomField` |
| `ui.tabs.closeButton` | bool | `true` | `TabBar/CloseButton` |
| `ui.fullScreen.hideToolbar` | bool | `false` | `FullscreenAutohideToolbar` |
| `window.integratedTitleBar` | bool | `false` | `IntegratedBar` |
| `window.buttonStyle` | `native` / `flat` / `minimal` | `native` (Windows), `flat` (Linux) | `TopBarButtonStyle` 0–2 |
| `window.ignorePlatformDecorations` | bool (Linux) | `false` | `IgnorePlatformDeco` |
| `window.reuseInstance` | bool | `false` | `ReuseInstance` |
| `editor.fontFamily` | font name | `Cascadia Mono` | `Editing/FontFace` |
| `editor.tabSize` | 1–16 · *per language* | `4` | `Editing/TabWidth` |
| `editor.useTabs` | bool · *per language* | `true` | `Editing/UseTabs` |
| `editor.lineNumbers` | bool | `true` | `Editing/LineNumbers` |
| `editor.wordWrap` | bool | `false` | `Editing/Wrap` |
| `editor.wrapSymbol` | bool | `false` | `Editing/WrapSymbol` |
| `editor.showWhitespace` | bool | `false` | `Editing/Whitespace` |
| `editor.indentGuides` | bool | `true` | `Editing/IndentGuides` |
| `editor.highlightCurrentLine` | bool | `true` | `Editing/CaretLine` |
| `editor.autoIndent` | bool | `true` | `Editing/AutoIndent` |
| `editor.multiCursor` | bool | `true` | `Editing/MultiEdit` |
| `editor.scrollBeyondLastLine` | bool | `false` | `Editing/ScrollBeyond` |
| `editor.caretWidth` | px | `1` | `Editing/CaretWidth` |
| `editor.caretBlinkMs` | ms | `500` | `Editing/CaretBlink` |
| `editor.edgeColumn` | column; 0 = off | `0` | `Editing/EdgeColumn` |
| `editor.directWrite` | bool (Windows) | `true` | `Editing/DirectWrite` |
| `editor.customGutterColor` | bool | `false` | `Editing/CustomGutterColour` |
| `editor.gutterColor` | `'#RRGGBB'` | from the theme | `Editing/GutterColourValue` |
| `editor.longLineThreshold` | characters | `50000` | `Editing/LongLineChars` |
| `editor.autoComplete.enabled` | bool | `true` | `Editing/AutoComplete` |
| `editor.autoComplete.minChars` | count | `3` | `AutoComplete/FromChar` |
| `editor.autoClosePairs` | bool | `false` | `AutoComplete/InsertPairs` |
| `files.associations` | extension → Language-menu name | — | `UserExt` group |
| `files.largeFileThresholdMiB` | MiB | `16` | `Editing/LargeFileMiB` |
| `files.maxRecentFiles` | 1–50 | `10` | `RecentFiles/Max` |
| `files.confirmCloseUnsaved` | bool — ask when quitting; closing a tab always asks | `false` | `AskBeforeClose` |
| `files.defaultDirectory` | `follow` / `remember` / `fixed` | `follow` | — |
| `files.defaultDirectoryPath` | folder, for `fixed` | — | — |
| `files.newDocument.eol` | `crlf` / `lf` / `cr` | `crlf` | `NewDoc/Eol` |
| `files.newDocument.language` | Language-menu name; empty = Normal text | — | `NewDoc/Lang` (a menu index) |
| `files.newDocument.encoding` | `utf-8` / `utf-8-bom` / `utf-16le` / `utf-16be` / `ansi` | `utf-8` | `NewDoc/Encoding` |
| `search.webEngine` | `duckduckgo` / `google` / `bing` / `yahoo` / `brave` | `duckduckgo` | `Editing/SearchEngine` |
| `spelling.backend` | `native` / `native+hunspell` / `hunspell` | `native+hunspell` | `Editing/SpellBackend` |
| `spelling.dictionary` | dictionary name | `en_US` | `Editing/SpellDict` |
| `spelling.commentsOnly` | bool | `true` | `Editing/SpellCommentsOnly` |
| `print.header`, `print.footer` | text with `$(…)` variables | — | `Print/Header`, `Print/Footer` |

The table lives in code as one array (`src/settings_schema.h`): ID, type, default, allowed names. The
loader, the sparse writer and the tests all read it, so a setting cannot be added in one place and
forgotten in another.

## `state.yaml`

What wxNote remembers between runs. Not meant for editing — it is rewritten whole, often. Several
wxNote windows (processes) share it: each save reads the file as it is now and lays over it only what
this process changed since its last save, in the order it changed them, so one window never undoes
another's; a file caught mid-write by another program is read again, and if it cannot be read whole the
save waits for the next one rather than treat it as empty. Recovery ids are unique per process, so two
windows never back up into the same file.

```yaml
window: {x: 120, y: 80, width: 1100, height: 720, maximized: false}
zoom: 0
recentFiles:
  - C:\work\notes.md
session:                 # the files open at the last exit, reopened at the next start
  pending: true
  active: 1
  files: [C:\work\notes.md, C:\work\todo.txt]
recovery:                # unsaved text backed up under RecoveryBackups/<id>.bak
  entries:
    r6650b3f2-2f4c-1: {path: '', title: new 3}   # id: process start time, process id, a count
run:
  lastCommand: notepad "$(FULL_CURRENT_PATH)"
dialogs:
  lastDirectory: C:\work   # the folder a file was last opened from or saved to (files.defaultDirectory)
plugins:
  disabled: [someplugin.dll]
  pendingUninstall: []
appImage: {integrated: true, asked: true}
```

## `keybindings.yaml`

The keymap model is unchanged — compiled defaults, the active scheme (a delta over its `parent`),
then your rules, which survive scheme switches. Only the file is new:

```yaml
version: 1
scheme: wxnote.default
bindings:                                    # applied last; a later rule wins
  - {key: ctrl+shift+a, command: file.saveAll}
  - {command: -view.tab.tab9}                # '-': remove every key from the command
  - {key: ctrl+w, command: -file.close}      # ... or just this one
  - {key: ctrl+shift+x, command: editor.lineCut}
  - {key: ctrl+t, command: view.terminal, when: editor}
schemes:
  - id: my.keys
    name: My keys
    parent: wxnote.default
    bindings:
      - {key: ctrl+k, command: edit.commentUncomment.setSingle}
```

- **Keys** are written lower-case, `+`-joined, as VS Code, Sublime and Pulsar write them; a chord is
  two keystrokes separated by a space. Any case is accepted. On macOS `ctrl` is ⌘ and `rawctrl` the
  physical Control key, exactly as wx reads them.
- **Commands** are the stable IDs the Shortcut Mapper shows. `editor.*` names are the Scintilla
  commands of the editor tier (formerly a separate `"editor"` section); no menu command uses that
  prefix, so one list holds both.
- **`when`** is `global` (default), `editor` or `terminal`.
- A file whose `version` is newer than this build understands opens read-only, as before.
- What a build cannot read is kept, not dropped: a rule with a typo'd field, a `when` it does not know
  (not applied - a newer build's condition must not turn global), a rule that adds no key, a scheme with
  no `id`, an unknown top-level entry. The Shortcut Mapper writes them back where they were. Comments
  are not kept: the Mapper writes the file out whole.

## Themes — `themes/<name>.yaml`

```yaml
# GitHub Light - the default light theme for wxNote.  SPDX-License-Identifier: Apache-2.0
global:
  Default Style: {fg: '#242830', bg: '#FCFCFC', font: Cascadia Mono, size: 10}
  Current line background colour: {bg: '#F0F2F5'}
lexers:
  cpp:
    description: C++
    styles:
      - {id: 11, name: DEFAULT, fg: '#242830', bg: '#FCFCFC'}
      - {id: 5, name: INSTRUCTION WORD, fg: '#D02030', fontStyle: bold}
```

- Colours are `'#RRGGBB'`, the notation of VS Code, Sublime and TextMate themes. Quoted, since `#`
  would otherwise start a comment.
- `fontStyle` is `bold`, `italic`, `underline` in any combination (`bold italic`), as VS Code and
  Sublime spell it, or `normal`. `font`, `size` and `weight` (100–900) as before.
- **An absent field inherits.** The XML spelled "inherit" as an empty attribute on nearly 50,000
  elements; the YAML just leaves the field out, which is most of why it is smaller.
- `extensions` on a lexer (Notepad++'s `ext`) still adds extensions for that language, below
  `files.associations`.
- Unused Notepad++ attributes are not carried over: `keywordClass`, `colorStyle`, the model dates
  and the per-style keyword text, none of which wxNote ever read.
- The Style Configurator writes the whole theme back, keeping the comment block at the top of the
  file (the licence and credits).

## Smaller files

```yaml
# contextmenu.yaml - the editor's right-click menu, top to bottom; "-" is a separator.
items:
  - edit.undo
  - '-'
  - {command: edit.copy, label: Copy Text}            # a label of its own; the shortcut still shows
  - menu: Change Case                                 # a submenu
    items: [edit.convertCaseTo.uppercase, edit.convertCaseTo.lowercase]
  - {plugin: MIME Tools, command: Base64 Encode}     # a plugin's command, by its menu labels
  - view.wordWrap                                     # a toggle shows its check mark
```

Items are command IDs, as in key bindings; an all-digits item is taken as a command number. A label
comes from the real menu, in the current language, unless the item gives its own (translated when
wxNote has that text). A plugin's command is found by the plugin's menu name and the command's, any
case, `&` ignored - how Notepad++'s `contextMenu.xml` names them. An item naming nothing this build or
its loaded plugins have is left out, and so is a submenu left empty, so a typo or a missing plugin
cannot break the menu. A file that does not parse is reported in the status bar and the built-in menu
is used.

```yaml
# functionlist.yaml - Function List rules for more languages
languages:
  ocaml:
    extensions: [ml, mli]
    extend: false               # true: add to the built-in rules instead of replacing them
    ignoreCase: false
    comment: '\(\*[\s\S]*?\*\)'
    rules:
      - {kind: function, group: 1, regex: '^\s*let\s+(?:rec\s+)?(\w+)'}
      - {kind: container, group: 1, regex: '^\s*module\s+(\w+)'}
```

Regexes go in single quotes: a backslash is then just a backslash.

```yaml
# snippets.yaml - language key (or '*' for every language), then the trigger word
'*':
  todo: 'TODO(${1:who}): ${2:what}$0'
cpp:
  for: |-
    for (int ${1:i} = 0; $1 < ${2:n}; ++$1)
    {
    	$0
    }
```

A literal block (`|-`) keeps tabs and line breaks as typed. The built-in snippets use the same
format, so there is one parser.

```yaml
# macros.yaml (written by wxNote)
nextId: 3
macros:
  - id: 2
    name: Trim and save
    steps:
      - {msg: 2327, w: 0, l: 0}
      - {msg: 2170, w: 0, l: 0, text: 'hello'}
```

A step's text that is not valid UTF-8 (typed into a document in a legacy code page) is stored as
`textBase64` instead.

```yaml
# runcommands.yaml (written by wxNote)
nextId: 2
commands:
  - {id: 1, name: Open in browser, command: 'firefox "$(FULL_CURRENT_PATH)"'}
```

Sessions and workspaces are saved wherever you choose, as `.yaml`:

```yaml
# a session
activeView: 0
main:
  active: 1
  files:
    - {path: C:\work\a.cpp, caret: 120, firstLine: 3, bookmarks: [4, 10]}
    - {path: C:\work\b.txt, language: Python}
sub: {active: 0, files: []}
```

A file's `language` is written only when it was picked from the Language menu (its menu name, or
`Normal Text`), and is picked again when the session loads; a language that detection found is found again.

```yaml
# a workspace
name: My project
items:
  - folder: src
    items:
      - file: C:\work\src\main.cpp
  - file: C:\work\README.md
```

The Nib `nib.session/1` interface keeps its by-path calls; the file they read and write is now this
session format.

## `languages.yaml` — language definitions

Notepad++ keeps what it knows about each language in `langs.xml`: its file extensions, its comment
tokens and its keyword lists (and, per language, the indentation). The file starts as a copy of the
shipped `langs.model.xml` and the user edits the copy; the Style Configurator's *User-defined keywords*
are stored in the theme. wxNote keeps the same knowledge in the program - `lang_detect.h`,
`comment_tokens.h`, `keywords.h` - and `languages.yaml` holds **only the user's changes** to it, so a
later wxNote's new keywords and extensions still reach a user who changed something else. Indentation
per language is a setting, so it is in `settings.yaml`'s `languages:` block, as in VS Code.

```yaml
languages:
  C++:                                       # the Language menu's name, any case
    extensions: {add: [ipp, tpp]}            # without the dot
    filenames: [conanfile.txt]               # whole file names, any case
    firstLine: '^//.*-\*-\s*c\+\+'           # a regular expression the first line can match
    comments: {line: '//', block: ['/*', '*/']}   # '' / [] / a null takes a form away
    keywords:
      types: {add: [size_t, ssize_t]}
      userKeywords1: [Q_OBJECT, emit]        # coloured by the theme's USER KEYWORDS 1
  Python:
    keywords: {add: [match, case]}           # short for the "keywords" list, else the first
```

**Why this shape.** The other editors studied agree on most of it, and wxNote takes what they agree on:

| | file types | first line | comments | keyword lists | user's changes |
| --- | --- | --- | --- | --- | --- |
| VS Code | `extensions`, `filenames`, `filenamePatterns` | `firstLine` | `comments: {lineComment, blockComment: [o, c]}` | none (grammars) | `files.associations`, per-language settings |
| Sublime Text | `file_extensions` | `first_line_match` | `TM_COMMENT_START`/`_END` preferences | none (regex syntax) | a syntax's `extensions` setting |
| TextMate | `fileTypes` | `firstLineMatch` | `TM_COMMENT_*` preferences | none | deltas over a bundle item (`changed`, `deleted`) |
| Pulsar | `fileTypes` | `firstLineMatch` | `commentDelimiters: {line, block: [o, c]}` | none | `core.customFileTypes`, scoped settings |
| Notepad4 | `FileExtensions` | - | built in | built in, numbered | changed extensions in the INI |
| Notepad++ | `ext` | - | `commentLine`, `commentStart`, `commentEnd` | `instre1`, `instre2`, `type1`-`type7`, `substyle1`-`8` | the copied `langs.xml`; theme keywords |

- File names: `extensions` without the dot and `filenames` (Sublime, TextMate, Pulsar, VS Code), and
  `firstLine` (VS Code; `firstLineMatch` elsewhere).
- Comments: a `line` token and a `block: [open, close]` pair (Pulsar's spelling of VS Code's).
- Changes: every list is either a **replacement** (a plain list, as in every editor's settings) or an
  **edit** `{add, remove}` over wxNote's own list - TextMate's deltas over a bundle. A removal stays
  removed when wxNote's own list grows, which Notepad++'s model merge cannot offer (it re-adds what
  the user deleted).
- Keyword lists: only Notepad++ and SciTE expose them, and both by number (`instre1`…, `keywords2`…).
  wxNote names them by what they hold, from Lexilla's own description of each lexer's lists
  (`DescribeWordListSets`), curated in `keyword_sets.h`: `keywords`, `types`, `docKeywords`,
  `globalClasses`, `taskMarkers` for the C family, `functions1`-`3` for Lua (the themes' FUNC1-3)... The
  Style Configurator shows each name with its description. Lexilla substyles of identifiers - the
  themes' USER KEYWORDS 1-8, USER TAGS/ATTRIBUTES for HTML and XML, USER SCALAR for shell - are the
  `userKeywordsN` (`userTagsN`, `userAttributesN`, `userScalarsN`) groups, allocated in Notepad++'s
  order so the themes' style numbers line up.

**How it is applied.**

- *File names* - `extensions` and `filenames` rank below the user's own mappings (the Style
  Configurator's *User ext.*, `files.associations`) and above the built-in tables; a name a language's
  definition takes away decides nothing, so the file goes on to the theme's extensions and its first
  line. `firstLine` patterns are tried before Scintillua's. The Style Configurator's *Default ext.*
  shows the result.
- *Comments* - the buffer's language (as the status bar names it) has its tokens replaced; Toggle
  Comment, Block Comment and the toolbar button follow. A changed token is matched in its own case, and
  needs a following space only when it ends in a letter or digit (`rem`).
- *Keywords* - each list is wxNote's own with the edit applied, and goes to the lexer and to
  completion; the user groups are allocated as substyles only when one has words.
- The file is re-read when its time stamp or size changes - checked as a document is shown, before a
  comment command and when the Style Configurator opens - so a saved edit applies to the next document
  shown. An unknown language or list name, or a value of the wrong shape, is skipped and the status bar
  names it; the rest of the file still counts.

**Who writes it.** The user, by hand (**Settings ▸ Edit Language Definitions** starts it from a commented
template), and the Style Configurator, whose *User-defined keywords* box sets one list's `add`. A write
re-emits the file through rapidyaml: its opening comments are kept, comments further down are not, and
other entries keep their content. A list the file replaces whole is not edited from the dialog. The
Notepad++ import lays its entries over the file (`wxnLangDefsMerge`).

## Notepad++

Reproducing Notepad++'s file formats is interoperability work, and wxNote confines it to optional,
separately licensed GPL packages (`LICENSING.md`), so the core stays Apache-2.0 and knows none of
them. **`packages/npp-compat`** is the one place that reads
Notepad++'s files and translates them into wxNote's:

| Notepad++ file | Becomes | How |
| --- | --- | --- |
| `shortcuts.xml` | a "Notepad++ (imported)" key-binding scheme | `nib.keymap` (unchanged) |
| `config.xml` | `settings.yaml` values (tab size, wrap, EOL, encoding, …) | `nib.settings/1`, new: set a setting by ID, validated against the schema |
| themes / `stylers.xml` | `themes/<name>.yaml` | written into the user theme folder |
| `contextMenu.xml` | `contextmenu.yaml` | command numbers are shared with Notepad++; items named by menu text, `FolderName` submenus, `ItemNameAs` labels and plugin commands come across too |
| `langs.xml`, the theme's user-defined keywords | `languages.yaml` entries, laid over the user's file | the user's additions only, found by comparing with Notepad++'s `langs.model.xml` (beside `langs.xml` or in its program folder); Notepad++'s keyword classes mapped to wxNote's lists as its lexers wire them |
| session `.xml` | opens its files, with positions and bookmarks | `nib.session`, through a scratch wxNote session |
| workspace `.xml` | a `.yaml` workspace beside it | written, then opened in a Project panel |

**Extensions ▸ Import from Notepad++…** reads a Notepad++ settings folder — an installed one's
`%APPDATA%\Notepad++` on Windows, or a copy of it put in `<wxNote user data>/notepad++` on any system —
imports everything wxNote has a place for, and opens one report of what came across and what had
nowhere to go. **Import the Open Notepad++ File** does the same for one file open in wxNote, which is
how a session or a workspace comes in. The existing *Validate shortcuts.xml* entry stays, and
forwards to the plugin's shortcuts-only import. A file an import replaces — `contextmenu.yaml`,
`languages.yaml` (laid over, not replaced: the user's other entries stay), an imported theme edited
since, a translated workspace — is kept aside first as `.bak` (or `.bak2`, …,
never over an earlier backup), and is not replaced if that copy fails; imported themes get names of
their own — *&lt;name&gt; (Notepad++)*, and *Notepad++ (imported)* for `stylers.xml` — so none of wxNote's
is overwritten, and `config.xml`'s theme becomes the imported copy (the host refuses a theme name it
has no file for). The same translations run from a terminal as `npp2wxnote`. Notepad++ plugins running
under `npp-bridge` still send and receive Notepad++ session files (`NPPM_SAVECURRENTSESSION`,
`NPPM_LOADSESSION`, `NPPM_GETSESSIONFILES`, …): the bridge translates between that XML and the host's
own session files (`nib.session`, through a scratch file in the user data folder), so the active
file's position and bookmarks and each view's active tab come across.

What wxNote keeps from Notepad++ by design, not by format: the command numbers (`kCmd*` = `IDM_*`),
which is what makes `shortcuts.xml` and `contextMenu.xml` translate without a table - apart from
`contextMenu.xml` items named by their English menu text (Notepad++'s own file names Cut, Copy, Paste
and a dozen more that way), which npp-compat looks up in Notepad++'s English menu names
(`npp_menu_names.h`, from its `english.xml`) the way Notepad++ does: `&` and a trailing `...` ignored,
any case, `MenuEntryName` choosing between same-named items of different menus.

## Later: new languages in YAML

`languages.yaml` changes the languages wxNote has. A language it does not have is defined today by a
Scintillua Lua lexer (`nib.langdef/1`), and `udl-compat` turns a Notepad++ UDL file into one. The
intended native format for those is `languages/<name>.yaml` — keywords, comment and string delimiters,
number and operator rules, folding markers — compiled into a Scintillua lexer at load, with
`udl-compat` becoming a UDL → YAML translator. Not part of this change.
