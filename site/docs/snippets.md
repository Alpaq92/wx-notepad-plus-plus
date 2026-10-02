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

Put them in **`snippets.yaml`** in the user data folder: each language, then each trigger, then its
body. A `|` block keeps the body's real newlines and tabs instead of cramming it onto one line:

```yaml
# My snippets.
cpp:
  guard: |
    #ifndef ${1:HEADER_H}
    #define $1

    $0

    #endif  // $1
'*':
  sig: '-- ${1:name}'
```

- The language is the same key the editor uses elsewhere — `cpp`, `python`, `js`, `sh`, `html`, and so
  on. Use `'*'` (in quotes, which YAML needs for it) for a snippet that applies everywhere.
- Under the `|`, the body is indented with spaces; wxNote removes that indentation and the final line
  break, so the body ends where its last line does. A tab *after* the indentation is part of the body.
- A one-line body can sit right after the trigger. Put it in single quotes when it starts with a
  symbol or contains `: ` or ` #`; a quote inside is then written twice (`''`).
- Lines starting with `#` are comments **only between snippets**. Inside a `|` body, `#` is ordinary
  text — it has to be, since it is a comment marker in half the languages you might write a snippet for.
- Save the file and the next <kbd>Tab</kbd> uses it. If it does not parse, none of your snippets are
  used until it does, and the status bar says where it broke.

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

## Transforms

A field can show a **derived** version of another one:

```yaml
cpp:
  cls: |
    class ${1:my_thing}
    {
    public:
        ${1/^(\w)|_(\w)/\U$1$2/g}();
    };
```

Type `my_thing` once and the constructor reads `MyThing()`. The syntax is
`${N/find/replace/flags}`:

| Part | |
| --- | --- |
| `find` | a regular expression — the same PCRE2 engine [Search](search.md) uses |
| `replace` | `$1`, `\1`, and `\U` `\L` `\E` `\u` `\l` for case, as in Replace |
| `flags` | `g` replace every match, `i` match case-insensitively |

A `/` inside either half is written `\/`.

**Transforms update when you leave the field**, not on every keystroke — press <kbd>Tab</kbd> and the
derived text catches up. A derived field is never a stop you can tab into, and never takes typing: it
belongs to the field it mirrors.

If the pattern isn't a valid regular expression, the field is left exactly as it was and the status bar
says why. An author's mistake never eats your text.

## Not supported

Nested fields (`${1:${2:x}}`) are left as literal text — the whole construct stays as written instead
of silently dropping part of it. The same applies to a transform with a flag this engine doesn't know:
better to show it unchanged than to apply it with the flag quietly ignored.
