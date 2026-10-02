# Languages &amp; Syntax

wxNote highlights code through **two** engines that coexist:

1. **Lexilla** — the Scintilla lexer library — powers the built-in languages.
2. **Scintillua** — Lua + LPeg grammars, embedded in the core — powers languages registered at runtime
   by plugins.

## Built-in languages

**Document&nbsp;&rsaquo; Language** lists **112 built-in languages**, bucketed into single-letter
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

## Comments

**Edit&nbsp;&rsaquo; Comment/Uncomment** and <kbd>Ctrl</kbd>+<kbd>/</kbd> insert the comment characters
**of the buffer's language**, not a fixed `//`:

| Comment | Languages |
| --- | --- |
| `#` | Python · Ruby · Perl · Raku · YAML · TOML · shell · PowerShell · Makefile · CMake · Dockerfile · R · Nix · Julia · Tcl · Elixir · GDScript · Nim · CoffeeScript · MySQL · gettext PO · Properties |
| `--` | SQL · MS SQL · Lua · Haskell · Ada · VHDL · Eiffel · ASN.1 |
| `//` | C · C++ · C# · Java · JavaScript · TypeScript · Go · Rust · Swift · Kotlin · Dart · Zig · Objective-C · PHP · D · Verilog · Scala · Groovy · JSON5 · SCSS · LESS · AsciiDoc · Stata |
| `'` | Visual Basic · VBScript · ASP · FreeBasic |
| `;` | LISP · Scheme · Assembly · INI · Registry · NSIS · Inno Setup · AutoIt · PureBasic · BlitzBasic · Csound · Rebol |
| `%` | TeX · LaTeX · MetaPost · Erlang · MATLAB · Octave · BibTeX · PostScript · MMIXAL · txt2tags · Visual Prolog |
| `<!-- -->` | HTML · XML · Markdown |

…and the rest of the 112 built-in languages, each with its own — COBOL's `*>`, Clarion's `!`, BaanC's
`|`, Batch's `rem`, Forth's `\`.

Which language applies follows the same rule the highlighting does: a **Document&nbsp;&rsaquo; Language**
pick decides it, otherwise the file's extension or name does. So forcing a language from the menu also
changes what gets commented.

A few points worth knowing:

- **Plain CSS has no line comment.** `//` is not valid CSS, so <kbd>Ctrl</kbd>+<kbd>/</kbd> wraps the
  line in `/* */` instead. SCSS and LESS *do* have `//` and use it. HTML, XML and Markdown work the
  same way, wrapping each line in `<!-- -->`.
- **Formats with no comment syntax at all** — JSON, patch files, Intel HEX, S-Record — are left
  untouched, and the status bar says so rather than inserting something that would break the file.
  The same happens when the language is simply unknown.
- **Block Comment** in a language that has no block form (Python, YAML, shell, Ruby) uses that
  language's line comment across the selection and reports the substitution in the status bar.
- **Uncommenting** removes the token and one following space, so `# x` and `#x` both round-trip
  cleanly, and indentation is preserved either way — the token always goes after the leading
  whitespace.

Languages registered at runtime by a plugin (see below) are not in this table: the `nib.langdef`
interface has no way to hand their comment characters to the host yet, so those buffers report that
their comment syntax is unknown instead of guessing.

Any of these can be changed for a language in [`languages.yaml`](#your-own-language-definitions-languagesyaml).

## Your own language definitions — `languages.yaml`

What Notepad++ keeps in `langs.xml` — which files open as each language, how it is commented, and its
keyword lists — wxNote keeps in its own data, and **`languages.yaml`** in the user data folder holds
your changes to it. **Settings&nbsp;&rsaquo; Edit Language Definitions** opens it, starting it from a
commented example the first time. Only what you write there changes; everything else stays as wxNote
has it, so a later wxNote's new keywords and extensions still reach you.

```yaml
languages:
  C++:                                     # the Language menu's name, in any case
    extensions: {add: [ipp, tpp]}          # files that open as it, without the dot
    filenames: [conanfile.txt]             # whole file names
    firstLine: '^//.*-\*-\s*c\+\+'         # a regular expression the first line can match
    comments: {line: '//', block: ['/*', '*/']}
    keywords:
      types: {add: [size_t, ssize_t]}
      userKeywords1: [Q_OBJECT, emit]      # coloured by the theme's USER KEYWORDS 1
  Python:
    keywords: {add: [match, case]}         # short for its "keywords" list
```

- **Every list takes two forms.** A plain list replaces wxNote's own; `{add: [...], remove: [...]}`
  changes it. A word or extension you remove stays removed when wxNote's own list grows.
- **Extensions** you add win over wxNote's own rules (and a plugin language's); one you remove no
  longer opens as that language, so the file falls back to its first line, or to plain text.
  The **User ext.** field of the [Style Configurator](themes.md#file-extensions) is the quicker way
  to map an extension, and wins over this file.
- **Comments** take a `line` token and a `block` pair; `''` or `[]` takes a form away.
- **Keyword lists** go by the names the Style Configurator shows for the language — `keywords`,
  `types`, `functions`, `taskMarkers`… — and `userKeywords1`…`8` (`userTags`, `userAttributes`,
  `userScalars` for HTML, XML and shell) are your own groups, coloured by the theme's **USER KEYWORDS**
  styles. What a list holds is what the highlighting and completion use. Write words in the case the
  language's own list uses (the Style Configurator shows it): SQL's or Pascal's lists are lower case,
  and their lexers match only lower-case entries. `null`, `Null` and `NULL` are words in a list like
  any other, though YAML reads them as nothing elsewhere.
- A language or list name wxNote does not know, or a value of the wrong shape, is skipped and named in
  the status bar; the rest of the file still counts. A file that does not parse is not used at all
  until it is fixed; one that cannot be read leaves things as they were. Changes apply to the next
  document you open or switch to.

The [Style Configurator](themes.md#keywords) edits the keyword part for you, and
**Extensions&nbsp;&rsaquo; Import from Notepad++…** brings across what you added to Notepad++'s
`langs.xml` — and the *User-defined keywords* of its Style Configurator — when Notepad++'s own
`langs.model.xml` is at hand to compare with (an installed Notepad++ has it in its program folder).

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
