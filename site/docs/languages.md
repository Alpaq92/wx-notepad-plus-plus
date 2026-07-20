# Languages &amp; Syntax

wxNote highlights code through **two** engines that coexist:

1. **Lexilla** — the Scintilla lexer library — powers the built-in languages.
2. **Scintillua** — Lua + LPeg grammars, embedded in the core — powers languages registered at runtime
   by plugins.

## Built-in languages

**Document&nbsp;&rsaquo; Language** lists **88 built-in languages**, bucketed into single-letter
submenus (A, B, C, …) so the list stays navigable. Each entry maps to the Lexilla lexer that highlights
it, and that lexer name doubles as the key used by the theme system for per-token colours.

The menu also has:

- **None (Normal Text)** at the top — force plain text on the active buffer,
- **User Defined Language ▸** containing **Open User Defined Language folder…**,
- **User-Defined** at the bottom.

The language is normally chosen automatically from the file's extension; picking an entry from this
menu **forces** that lexer on the active buffer for the rest of the session.

Set the language new documents start in with
**Preferences&nbsp;&rsaquo; New Document&nbsp;&rsaquo; Default language**.

## Code folding

Folding is available for languages whose lexer reports fold levels. The commands live under **View**:

- **Fold All** (<kbd>Alt</kbd>+<kbd>0</kbd>) / **Unfold All**
  (<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>0</kbd>)
- **Fold / Unfold Current Level** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> /
  <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>)
- **Fold Level ▸** and **Unfold Level ▸** — jump straight to levels 1–8
- **Hide Lines** — collapse an arbitrary selected range

## Scintillua — the extensible engine

The core embeds Lua, LPeg and Scintillua as its native, cross-platform language-definition engine. It
is a generic "load a lexer, lex some text" engine: it knows nothing about any particular editor's file
formats.

A plugin registers a language through the `nib.langdef` capability, supplying three things:

- a **name** (used both as the menu entry and as the Scintillua lexer name),
- a space-separated **extension list** without dots, e.g. `myl mylang`,
- the lexer's **Lua source**.

The host copies the strings, compiles the Lua once, and from then on the language behaves like any
other. See [Plugins](plugins.md) for the API.

```lua
-- the shape of a Scintillua lexer a plugin would hand to nib.langdef
local lexer = require('lexer')
local lex = lexer.new('mylang')
lex:add_rule('whitespace', lex:tag(lexer.WHITESPACE, lexer.space^1))
lex:add_rule('comment', lex:tag(lexer.COMMENT, lexer.to_eol('#')))
lex:add_rule('keyword', lex:tag(lexer.KEYWORD, lex:word_match('if else while')))
return lex
```

## Legacy Notepad++ User-Defined Languages

Legacy `userDefineLang.xml` files are handled by the optional **`udl-compat`** plugin, which parses the
Notepad++ UDL format, translates each definition into a Scintillua lexer, and registers it through
`nib.langdef`.

Practical consequences:

- Your existing UDL XML files keep working **if** `udl-compat` is installed.
- Delete the plugin and those files simply stop loading; nothing else changes.
- `udl-compat` is **GPL-3.0-or-later**, because reproducing the Notepad++ UDL format is what makes it
  interoperate. The core stays Apache-2.0 precisely because that reproduction is confined to this
  optional module.

Put your UDL files in the per-user folder reached by
**Document&nbsp;&rsaquo; Language&nbsp;&rsaquo; User Defined Language&nbsp;&rsaquo; Open User Defined
Language folder…** — it is created on demand if it does not exist yet.

## Encoding

Encoding is separate from language and lives under **Document&nbsp;&rsaquo; Encoding**. The menu has two
distinct halves:

- **Interpret as** — ANSI, UTF-8, UTF-8-BOM, UTF-16 BE/LE BOM, plus **Character sets ▸** grouped by
  script (Arabic, Baltic, Celtic, Cyrillic, Central European, Chinese, Eastern European, Greek, Hebrew,
  Japanese, Korean, North European, Thai, Turkish, Western European, Vietnamese). This **re-decodes**
  the bytes already on disk — reach for it when a file opens as mojibake.
- **Convert to…** — changes the encoding the document will be **written** in.

The current encoding is shown in status-bar field 5; double-click it for the same choices as a popup.
