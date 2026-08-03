# Snippets

A snippet is a piece of text you insert by name, with the spots you need to fill in already marked.
Type `for`, press <kbd>Tab</kbd>, and you get the loop with the cursor on the variable name.

## Using them

Two ways in:

- **Type a trigger and press <kbd>Tab</kbd>.** If the word before the cursor names a snippet for this
  file's language, it expands. If it does not, <kbd>Tab</kbd> indents as usual.
- **Edit&nbsp;&rsaquo; Insert Snippet…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>J</kbd>) lists
  everything available for the current language, filtered as you type.

Once a snippet is in:

| Key | Does |
| --- | --- |
| <kbd>Tab</kbd> | jump to the next field |
| <kbd>Shift</kbd>+<kbd>Tab</kbd> | jump back |
| <kbd>Esc</kbd> | leave the snippet where it is |

Fields with a default value arrive selected, so typing replaces them. When a field is used more than
once, **every copy updates as you type** — write the loop variable once and it changes everywhere.

Continuation lines are indented to match the line you started on, and use the document's own line
endings, so inserting into a CRLF file will not mix in stray LFs.

## Writing your own

Put them in **`snippets.txt`** in the user data folder. The format is deliberately plain, so bodies
keep their real newlines and tabs instead of being crammed onto one line:

```
[cpp:guard]
#ifndef ${1:HEADER_H}
#define $1

$0

#endif  // $1
```

- The header is `[language:trigger]`. Everything until the next header is the body.
- The language is the same key the editor uses elsewhere — `cpp`, `python`, `js`, `sh`, `html`, and so
  on. Use `*` for a snippet that applies everywhere.
- Lines starting with `#` are comments **only between snippets**. Inside a body, `#` is ordinary text
  — it has to be, since it is a comment marker in half the languages you might write a snippet for.

### Fields

| Syntax | Meaning |
| --- | --- |
| `$1`, `$2`, … | a field, visited in ascending order |
| `${1:default}` | a field with text already in it |
| `$0` | where the cursor ends up when you finish |
| repeated `$1` | a mirror — all copies edit together |
| `\$` | a literal dollar sign |

If there is no `$0`, the cursor lands after the snippet.

A snippet you define **overrides a built-in with the same trigger**, so you can replace any of the
shipped ones by repeating its name.

## What ships

A short, deliberately unambitious set — the loops and guards people retype most:

| Language | Triggers |
| --- | --- |
| C/C++ | `for` `forr` `if` `sw` `cls` |
| Python | `def` `cls` `for` `main` |
| JavaScript | `fn` `for` `log` |
| Shell | `for` `if` |
| HTML | `a` `div` |
| *(any)* | `todo` |

It is a starting point, not a library — the intent is that you add the ones you actually use.

## Not supported

Transforms like `${1/find/replace/}` are left as literal text rather than half-applied. They depend on
replacement case operators, and are worth doing properly rather than partially.

Nested fields (`${1:${2:x}}`) are likewise left alone — the whole construct stays as written instead
of silently dropping part of it.
