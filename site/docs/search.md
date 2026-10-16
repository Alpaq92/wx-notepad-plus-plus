# Search &amp; Regular Expressions

Find, Replace, Mark, Count and Find in Files all share one engine, so a pattern behaves identically
wherever you type it — including the incremental search bar.

## Search modes

The Search Mode radio group offers three:

| Mode | Meaning |
| --- | --- |
| **Normal** | plain text, no special characters |
| **Extended** | `\n`, `\r`, `\t`, `\0`, `\x…` are interpreted |
| **Regular expression** | the full syntax below |

## Regular expressions

wxNote uses **PCRE2** — the same family Notepad++ and most other editors use — so patterns you already
know will work.

### Patterns can cross lines

This is the headline capability, and the one most likely to be missing from editors built on the same
foundations. A pattern may contain `\n` and match across line breaks:

```
BEGIN[\s\S]*?END
```

matches a whole block however many lines it spans. So these all work:

| Goal | Pattern | Replace with |
| --- | --- | --- |
| Join wrapped lines | `\n\s+` | *(a space)* |
| Delete a block between markers | `<!--[\s\S]*?-->` | *(empty)* |
| Drop blank lines | `^\s*\n` | *(empty)* |
| Strip trailing whitespace | `\s+$` | *(empty)* |

`.` does **not** cross a line break — that is standard, and keeps `.*` from swallowing your file. When
you want it to, tick **`.` matches newline** next to the Search Mode options. (The checkbox is greyed
out unless *Regular expression* is selected, since it means nothing to the other modes.) `[\s\S]` and a
leading `(?s)` do the same thing if you prefer to keep it in the pattern.

`^` and `$` match at every line boundary, not just at the start and end of the document.

### What you can use

Lookbehind, lookahead, named groups, non-greedy quantifiers, atomic groups and inline flags are all
available:

| | Example |
| --- | --- |
| Lookbehind | `(?<=\$)\d+` — a number, but only after a `$` |
| Negative lookbehind | `(?<!\$)\b\d+` |
| Lookahead | `\d+(?= dollars)` |
| Named groups | `(?<year>\d{4})-(?<month>\d\d)` |
| Inline flags | `(?i)hello` — case-insensitive for this pattern |

`\w`, `\b` and the character classes are **Unicode-aware**, so `\w+` matches `źółw` as one word.

### Replacements

Groups can be referenced either way — `$1` and `\1` both work, and `${1}` disambiguates when a digit
follows:

| | Effect |
| --- | --- |
| `$1` `\1` `${1}` | capture group 1 |
| `$0` | the whole match |
| `\U` … `\E` | upper-case until `\E` |
| `\L` … `\E` | lower-case until `\E` |
| `\u` / `\l` | upper- or lower-case the **next character only** |
| `\n` `\r` `\t` `\\` | newline, carriage return, tab, backslash |

So `(\w+)@(\w+)` → `$2 at $1` turns `me@here` into `here at me`, and `^(\w)` → `\u$1` capitalises the
first letter of every line.

Case conversion applies to ASCII letters. Text outside that range is passed through unchanged rather
than half-converted.

### When a pattern is wrong

An invalid pattern reports **"Invalid regular expression"** with the reason and the position, e.g.
*missing closing parenthesis at offset 7*. It is never silently treated as "no matches found".

### Notes

- **Whole word** works with regular expressions too — the pattern is bounded as a whole, so `a|bc`
  requires the entire alternation to stand alone, not just its last branch.
- **Zero-width matches** (`x*`, `\b`, a bare lookahead) are handled: Replace All steps forward one
  character each time instead of looping forever, and never splits a multi-byte character.
- A file that is not valid UTF-8 still searches. Malformed bytes simply do not match.

## Find All, in one document or all of them

The **Find** tab's **Find All in Current Document** and **Find All in All Opened Documents** list every
match in the **Search results** panel; the **Replace** tab's **Replace All in All Opened Documents**
replaces in every open document after asking, each document in one undo step. A document open in both
views is searched once, and read-only ones are left alone.

## Find in Files

**Search&nbsp;&rsaquo; Find in Files** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>) opens the Find
dialog's **Find in Files** tab. Find in Files and Replace in Files use the same engine, so a multi-line
pattern works across a whole directory tree.

- **Directory** — `...` browses; `<<` fills in the current document's folder.
- **Filters** — Notepad++'s syntax: wildcards separated by spaces or `;` (`*.cpp *.h`), `!pattern` to
  leave files out (`!*.min.js`), and `!older` (or `!+older`) to leave a folder out wherever it is
  (`!
ode_modules`). `*.*` means every file, with or without an extension.
- **In all sub-folders** — on by default; off searches the directory alone.
- **In hidden folders** — off by default, so `.git` and the like stay out (on Windows, folders with the
  hidden attribute).

Find All lists its hits in the Search results panel. Replace in Files changes the files on disk and
reports how many replacements it made, as Notepad++ does.

## The Search results panel

Every Find All adds a search to the panel, newest on top, the older ones folded away; a search lists
the files or documents it hit and each line with its matches highlighted. Lines with several matches
appear once.

- **Enter** or a double-click opens the document at the match, selected. On a file line it opens the
  file; on a search line it folds or unfolds it.
- **Delete** removes a hit line, a file with its hits, or a whole search.
- <kbd>F4</kbd> / <kbd>Shift</kbd>+<kbd>F4</kbd> step through the hits; <kbd>F7</kbd> shows the panel
  and moves between it and the editor.
- The right-click menu has **Copy**, **Copy Selected Line(s)**, **Copy Selected Pathname(s)**,
  **Select All**, **Clear All**, **Open Selected Pathname(s)**, **Find in These Search Results...** (the
  same search over the listed lines, as a new search of its own), **Word Wrap Long Lines** and **Purge
  for Every Search** (keep only the latest search). The last two are remembered.

## Marks and counting

**Count** and **Mark All** accept the same patterns. A zero-width match highlights nothing rather than
underlining an arbitrary neighbouring character.
