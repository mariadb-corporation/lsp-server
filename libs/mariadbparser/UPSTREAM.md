# Upstream sources and manual deviations

This library is generated once from upstream sources and then maintained by hand. The
generated files live in `generated/` and are committed; `tools/stub.py`,
`tools/lexslice.py` and `tools/mrs_merge.py` are kept only for a full re-extraction or
re-merge, never run by the build.

## Pins

The MariaDB server commit below is the single source of the pin: the top-level CMake
reads it from this table (`cmake/mariadb_pin.cmake`) and builds the client library from
the same commit. Keep the row's first cell `MariaDB server` and its 40-character hash.

| Upstream | Commit | What is taken |
| --- | --- | --- |
| MariaDB server (`https://github.com/MariaDB/server`) | `bab03b0fc44e7aeac869b20b0055c6a8f2083eb6`, 13.1.0 alpha, 2026-08-19 | Grammar: `sql/sql_yacc.yy`, MariaDB variant, preprocessed by `sql/gen_yy_files.cmake`. Lexer: `sql/sql_lex.cc`, `sql/sql_lex.h`, `sql/lex_ident_cli.h`. Keywords: `sql/lex.h`, `sql/lex_symbol.h`. Character handling: `include/m_ctype.h`, `strings/ctype-utf8.{c,h}`, `strings/ctype-latin1.c`, `strings/my_strtoll10.c`, `mysys/charset.c`, `sql/share/charsets/Index.xml`. Release id: `VERSION` |
| MariaDB Shell (`git@github.com:mariadb-corporation/mariadb-shell.git`) | `4f45397bed864ccb034d4bc565b7b704def7ed93`, on `main`, 2026-10-13 ("Add the MariaDB REST Service to the shell: built-in REST SQL"), the last commit that changed the file | `mysqlshdk/libs/mrs/parser.yy` (2,443 lines), the rules between `/* START OF MERGE PART */` (line 432) and `/* END OF MERGE PART */` (line 2319), merged on 2026-10-15 (see "The REST SQL merge" below) |

## What `generated/` holds

| File | What it is |
| --- | --- |
| `yy_stub.yy` | `sql_yacc.yy`, MariaDB variant, with its C++ prologue replaced by `#include "stub.h"`, its `%union` by `%define api.value.type {int}`, and every rule given exactly one action `{ MDBP_R(<serial>); }`: 2,368 existing action bodies replaced (284 of them mid-rule) and 1,275 actions added to rules that had none. The 36 statements that write `parsing_place` are kept inside their actions, because the lexer reads that field back |
| `lexer_extracted.cc` | `Lex_input_stream::lex_token` / `lex_one_token` and their transitive closure: 24 functions and 24 types sliced by name from `sql_lex.cc`, `sql_lex.h`, `lex_ident_cli.h`, `m_ctype.h`, `ctype-utf8.c`, `ctype-latin1.c` and `my_strtoll10.c`, with one named rewrite |
| `keywords.h` | the `SYMBOL` type from `lex_symbol.h` and the keyword and SQL-function tables from `lex.h` |
| `lexstates.h` | the `my_lex_states` enum from `m_ctype.h` |
| `state_maps.h` | the utf8mb4 lexer state maps, computed by a generator compiled against the sliced charset data |
| `ctype-utf8.h` | `strings/ctype-utf8.h`, copied unchanged |

## How they were produced

Once, on 2026-10-15, on macOS (arm64) with bison 3.8.2 and Python 3.14, from the server
tree at the pin:

1. `cmake -DIN=sql/sql_yacc.yy -DOUT1=yy_mariadb.yy -DOUT2=yy_oracle.yy -DVAL1=MARIADB -DVAL2=ORACLE -P sql/gen_yy_files.cmake`
   (the server's own `%ifdef` preprocessor).
2. `tools/stub.py --in yy_mariadb.yy --out yy_stub.yy` (`--map stub-map.json` also writes
   a manifest of every replaced and preserved action).
3. `bison -d -Dparse.lac=full -o parser.c yy_stub.yy`, without warnings; step 4 reads
   the token numbers from its `parser.h`.
4. `tools/lexslice.py --source <tree> --out <dir> --parser-h parser.h --compile-state-maps`.

Checks of the result:

- **Parser.** The original project's 212,332 recorded token streams were replayed
  through `mdbp_parse_tokens()`. Verdict, derivation, tokens consumed, error token,
  error position and the full expected-token set are identical for every one. Two
  fields differ, both for known reasons: the recorded error state comes from a Python
  reference parser that numbers states differently (the original build differed on the
  same 17,273 records), and the end token of 62 records is `$end` because a token
  stream has no lexer to report an abort.
- **Lexer.** The 277 fixtures in `tests/lex-fixtures/` that lex without a parser pass
  through `mdbp_lex_sql()` (`unittest/mariadbparser/lex_fixtures_t.cc`).

## How the build uses them

The build runs bison over `yy_stub.yy` (with `-k` and `-Dparse.lac=full`) and compiles.
The token-name table and the rule numbers are read from bison's own output at run time:
`yytname[]` and `yytranslate[]` for the names, and `yyn - 1` inside each action for the
rule number. So editing `yy_stub.yy` by hand cannot leave a stale table behind.

## Maintaining by hand

Between re-extractions, upstream changes are applied by hand: diff the server file
between the old and the new pin, apply the hunks to the matching generated file, update
the pin above, and list each deviation below. Rules for hand edits:

- **Every rule in `yy_stub.yy` needs exactly one action `{ MDBP_R(n); }`**, including
  rules that are new or had no action upstream. A rule without one still parses, but its
  reductions are missing from the derivation, and the syntax tree built from it is
  wrong. The number `n` is ignored at run time; keep it unique for readability.
- Grammar action bodies upstream adds are not copied, except statements that assign
  `parsing_place` or `save_parsing_place`, which are kept as they are.
- When `VERSION` changes, update `MDBP_LEX_DEFAULT_VERSION_ID` in `src/mdbp_lex.h`
  (`major*10000 + minor*100 + patch`; `130100` today). It is the release id
  `/*!NNNNNN ... */` comments are judged against when the caller sets none.
- Keyword changes in `lex.h` go into `generated/keywords.h`, and every new keyword also
  needs its `%token` in `yy_stub.yy`.

## The REST SQL merge

`tools/mrs_merge.py` translated the shell's merge part and appended it to
`generated/yy_stub.yy` (from the comment `MariaDB REST Service (REST SQL) statements` to the
end of the file), and appended the new keywords to `generated/keywords.h` (lines marked
`/* REST SQL */`). The tool's docstring lists every translation rule. Result:

- 199 rules, `mrs_statement` added to `verb_clause`, the 27 REST `SHOW` statements added
  to `show_param` (without their `SHOW`).
- 61 new keywords, all non-reserved: 59 in `keyword_sp_var_and_label`, `CLONE` and
  `CONFIGURE` in `keyword_verb_clause`. Words such as `rest`, `service`, `content` or
  `app` stay valid table, column, schema, role and label names, also right after `USE`,
  `GRANT` and `REVOKE`.
- No new conflicts: bison reports the server's 72 shift/reduce conflicts and nothing else.
- Verified: all 233 statements of the shell's `unittest/data/mrs/grammar_test.sql`
  (copied to `tests/fixtures/sql/mrs_grammar_test.sql`) parse; the original project's
  212,332 recorded server token streams get the same verdict and error position as
  before the merge.

Where the merged grammar accepts more than the shell's parser:

- An annotation is `'@'` and any word: the server lexer has no `@NOCHECK`-style tokens.
  Which annotation is allowed where is not checked, and a data-mapping field's mode,
  options, `@DATATYPE(...)` and CRUD options are one list in any order.
- `mrs_identifier` accepts every server keyword that the REST rules do not use (the
  shell's lexer reads those words as identifiers), and quoted names in both styles.

To take a later change to the shell grammar: either apply its diff to the merged rules by
hand, or remove the merged block, the `/* REST SQL keyword */` token lines, the merged
alternatives of `keyword_sp_var_and_label`, `keyword_verb_clause`, `show_param` and
`verb_clause`, and the `/* REST SQL */` keyword lines, and run `tools/mrs_merge.py` again.
Then update the shell pin above.

## Known judgement points in the transform

Kept from the original regeneration notes, since a re-extraction has to re-check them:

- `stub.py` preserves the `parsing_place` statements by a named rule; a new feedback
  field written by an action and read by the lexer would need the same treatment.
- `lexslice.py` slices by a symbol list and has one named rewrite rule; a renamed or
  newly split lexer function has to be added to the list.
- `src/lexshim.h` shims the pieces of `THD` / `LEX` the sliced lexer touches; the
  ledger of what is and is not modelled is `docs/thd.md`.
- The MariaDB variant of the grammar declares `%expect 72`; the REST SQL merge added no
  conflict. A later change that adds one has to resolve it, not raise the count.
- Only `utf8mb4` (and its alias `utf8mb4_general_ci`) is lexed; other charsets are
  refused, never approximated.
- `sql_mode` `ORACLE` and `MSSQL` are refused: they change the language, and the Oracle
  grammar variant is not extracted.

## Changes since the original project

Tools:

- `tools/gen_tables.py` was removed: the runtime builds the same tables from bison's
  own arrays.
- `tools/stub.py` lost its override mechanism and its rule-map mode;
  `tools/lexslice.py` lost its host check and its default paths (`--source` and
  `--out` are required); `tools/common.py` keeps only what the two tools use.
- `tools/lexslice.py` no longer passes GCC's `-fdiagnostics-plain-output` when it
  compiles the state-map generator, so it also runs with Clang.
- The wording the tools emit into the banner and a few comments of
  `generated/lexer_extracted.cc` and `generated/state_maps.h` no longer refers to the
  original project; a re-extraction still reproduces `generated/` byte for byte.
- The ledger of modelled server state moved from `tools/vendor/thd.md` to
  `docs/thd.md` and is maintained here.

Library sources (behaviour unchanged, as the replay above shows):

- `src/mdbp_runtime.c` and `src/parser_glue.c` build the token tables from `yytname[]`
  and `yytranslate[]`; `src/stub.h` defines `MDBP_R(n)` to record `yyn - 1`.
- `src/mdbp_once.h` replaces C11 `call_once`, which macOS lacks, with `pthread_once`
  or `InitOnceExecuteOnce`.
- `src/mdbparse.c`, a 2,500-line harness for comparing against the original project's
  Python reference lexer, was replaced by `src/mdbparse.cc`, a small CLI on the public
  API. The internal functions only the old CLI used were removed with it.
- Comments no longer refer to the original project's plan, evidence records, corpus
  or Python tests.

## Manual deviations from upstream

None yet. Each entry: file, what was changed, why, and the upstream commit it tracks.
