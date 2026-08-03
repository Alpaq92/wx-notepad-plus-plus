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

## Find in Files

Find in Files and Replace in Files use the same engine, so a multi-line pattern works across a whole
directory tree. The *Filters* field takes `;`-separated wildcards (`*.cpp;*.h`), and a file matching
two of them is scanned once.

## Marks and counting

**Count** and **Mark All** accept the same patterns. A zero-width match highlights nothing rather than
underlining an arbitrary neighbouring character.
