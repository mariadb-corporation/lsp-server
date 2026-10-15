# Upstream source and manual deviations

`Sql_splitter`, MariaDB Shell's SQL statement splitter, copied from the shell and
maintained by hand. The files keep the shell's paths under `include/` so that its
`#include` lines need no change.

## Pin

| Upstream | Commit | Files |
| --- | --- | --- |
| MariaDB Shell (`git@github.com:mariadb-corporation/mariadb-shell.git`) | `6c0d7a9c37f64419defa07d9c18bfa3e9f305222`, 2026-08-28 ("AIPL-15: Add result format suffixes to the \G statement delimiter"), the last commit that changed any of them; identical on `main` at `856382a0` | `mysqlshdk/libs/utils/utils_mysql_parsing.h`, `mysqlshdk/libs/utils/utils_mysql_parsing.cc`, `mysqlshdk/libs/utils/utils_lexing.h`, `unittest/utils_mysql_parsing_t.cc` |

| Here | Shell |
| --- | --- |
| `include/mysqlshdk/libs/utils/utils_mysql_parsing.h` | `mysqlshdk/libs/utils/utils_mysql_parsing.h` |
| `include/mysqlshdk/libs/utils/utils_lexing.h` | `mysqlshdk/libs/utils/utils_lexing.h` |
| `src/utils_mysql_parsing.cc` | `mysqlshdk/libs/utils/utils_mysql_parsing.cc` |
| `unittest/sqlsplitter/utils_mysql_parsing_t.cc` (top level) | `unittest/utils_mysql_parsing_t.cc` |

To take an upstream change, diff these files in the shell between the pinned commit and
the new one, apply the hunks here, update the pin, and keep the deviations below.

## Manual deviations from upstream

All are marked `lsp-server:` in the source.

- `utils_lexing.h`: reduced to the three lookup tables in `namespace internal` that the
  splitter uses; the shell's span helpers and its `utils_string.h` include are gone.
- `utils_mysql_parsing.cc`:
  - the includes of `scoped_contexts.h` and `utils_string.h` are removed; a local
    `namespace shcore` provides the two helpers the file calls, `str_ibeginswith()` and
    `str_format()`;
  - keyword commands (the `commands` list given to the constructor) no longer check
    the shell's SQL handler registry, which does not exist here;
  - in the `\` case, a command callback answering `(0, false)` makes the backslash an
    ordinary character. Upstream would not advance in that case; the shell's own
    callbacks never answer 0 there. The language server relies on this so the splitter
    never moves bytes in its input (see below).
  - **bug fix**: with dollar-quoted strings off, a `$` pushed an identifier context in
    every context, including inside a versioned comment (`/*!90200 ... $$ */`) and an
    identifier already open. The comment's `*/` was then not seen and the next
    delimiter was missed, merging two statements. A `$` now starts an identifier only
    at statement level. The shell has the same bug; its chunked `iterate_sql_stream()`
    hides it when a statement ends inside the chunk that is still being read, because
    an unfinished statement is scanned again from its start.
- `utils_mysql_parsing_t.cc`: includes `<gtest/gtest.h>` and the header by its full
  path instead of the shell's test headers, and defines local stand-ins for
  `g_test_trace_scripts`, `shcore::str_rstrip()` and `shcore::str_format()`.

## The splitter and its input buffer

`Sql_splitter::next_range()` takes a mutable buffer because the shell strips backslash
commands (`\c`, `\source`, …) and keyword commands out of it with `memmove`. The language
server passes no keyword commands, and its command callback answers `(0, false)` for every
backslash except the `\g`/`\G` delimiters, so the buffer is never written and every
reported range is a byte offset into the original text.
