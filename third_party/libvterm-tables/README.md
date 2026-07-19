# libvterm generated encoding tables

`encoding/DECdrawing.inc` and `encoding/uk.inc` are the DEC character-set
tables that libvterm's `src/encoding.c` `#include`s. They are **generated
files**: upstream produces them from the `src/encoding/*.tbl` sources with
`perl -CSD tbl2inc_c.pl <file>.tbl` at build time, and the GitHub tag archive
we fetch (neovim/libvterm, pinned in the top-level `CMakeLists.txt`) ships
only the `.tbl` sources — not the `.inc` output. Perl is not a build
dependency we can assume (the MSVC CI runners configure without it), so the
two tiny outputs are vendored here instead and copied into the fetched source
tree at configure time.

- Upstream: <https://github.com/neovim/libvterm> (fork of Paul Evans'
  libvterm), tag `v0.3.3`.
- License: MIT, `Copyright (c) 2008 Paul Evans` — same as the rest of
  libvterm; these files are mechanical transformations of upstream's `.tbl`
  data. See `LICENSING.md` / `NOTICE` at the repository root.

**When bumping the pinned libvterm version:** regenerate both files from the
new tag's `.tbl` sources with upstream's own script (any perl, e.g. the one
in Git Bash):

    perl -CSD tbl2inc_c.pl src/encoding/DECdrawing.tbl > DECdrawing.inc
    perl -CSD tbl2inc_c.pl src/encoding/uk.tbl > uk.inc

and check whether upstream added new `.tbl` files. A *missing* table fails
the `vterm` target's compile (unresolved `#include`), but a *stale* one
compiles silently with outdated character data — which is why regeneration is
part of the bump, not an optional step.
