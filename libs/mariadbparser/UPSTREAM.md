# Upstream sources and manual deviations

This library is generated once from upstream sources and then maintained by hand. The
generated files live in `generated/` and are committed; `tools/stub.py` and
`tools/lexslice.py` are kept only for a full re-extraction (a new major server version),
never run by the build.

## Pins

| Upstream | Commit | What is taken |
| --- | --- | --- |
| MariaDB server (`https://github.com/MariaDB/server`) | `bab03b0fc44e7aeac869b20b0055c6a8f2083eb6`, 13.1.0 alpha, 2026-08-19 | `sql/sql_yacc.yy` (grammar, MariaDB variant), `sql/sql_lex.cc` and `sql/sql_lex.h` (lexer), `sql/lex.h`, `sql/lex_symbol.h`, `sql/lex_ident_cli.h` (keyword tables and token types), `include/m_ctype.h`, `strings/ctype-utf8.{c,h}`, `strings/ctype-latin1.c`, `mysys/charset.c`, `sql/share/charsets/Index.xml` (utf8mb4 character classes), `strings/my_strtoll10.c`, `sql/gen_yy_files.cmake` (the `%ifdef MARIADB` preprocessor), `VERSION` |
| MariaDB Shell (`git@github.com:mariadb-corporation/mariadb-shell.git`) | `4f45397bed864ccb034d4bc565b7b704def7ed93`, on `main`, 2026-10-13 ("Add the MariaDB REST Service to the shell: built-in REST SQL"), the last commit that changed the file | `mysqlshdk/libs/mrs/parser.yy` (2,443 lines), the rules between `/* START OF MERGE PART */` (line 432) and `/* END OF MERGE PART */` (line 2319). **Not merged yet**: `generated/yy_stub.yy` holds the server grammar only until phase P2 |

## How the generated files came to be

`generated/` holds:

| File | What it is |
| --- | --- |
| `yy_stub.yy` | `sql_yacc.yy`, MariaDB variant, every action body replaced by `{ MDBP_R(<serial>); }` except the 36 statements that write `parsing_place` |
| `lexer_extracted.cc` | `Lex_input_stream::lex_token` / `lex_one_token` and their closure, sliced from `sql_lex.cc` by symbol |
| `keywords.h` | the keyword and SQL-function tables from `lex.h` |
| `lexstates.h` | the lexer state enum from `m_ctype.h` |
| `state_maps.h` | the utf8mb4 lexer state maps, computed from the server's charset tables |
| `ctype-utf8.h` | `strings/ctype-utf8.h`, copied unchanged |

They were produced once, on 2026-10-15, on macOS (arm64) with bison 3.8.2 and
Python 3.14, from the server tree at the pin above:

1. `cmake -DIN=sql/sql_yacc.yy -DOUT1=yy_mariadb.yy -DOUT2=yy_oracle.yy -DVAL1=MARIADB -DVAL2=ORACLE -P sql/gen_yy_files.cmake`
   (the server's own `%ifdef` preprocessor).
2. `tools/stub.py --in yy_mariadb.yy --out yy_stub.yy --map stub-map.json`:
   3,927 actions, 36 `parsing_place` statements preserved.
3. `bison -d -Dparse.lac=full -o parser.c yy_stub.yy` (needed by step 4 for the
   token numbers; no warnings).
4. `tools/lexslice.py --source <tree> --out <dir> --parser-h parser.h --compile-state-maps`:
   24 functions, 24 types, 1 rewrite.

The result was checked against the original project's recorded output: all
212,332 recorded token streams give the identical verdict, derivation, tokens
consumed, error token, error position and expected-token set.

The build runs only bison over `yy_stub.yy` (with `-k` and `-Dparse.lac=full`)
and compiles. The token-name table and the rule numbers are read from bison's
own output at run time (`yytname[]`, `yytranslate[]`, and `yyn` inside each
action), so editing `yy_stub.yy` by hand cannot leave a stale table behind.

Between re-extractions, upstream changes are applied by hand: diff the server
file between the old and the new pin and apply the hunks to the generated file
(actions are already blank, so grammar hunks are small); update the pin above
and list the deviation below.

## Known judgement points in the transform

Kept from the original regeneration notes, since a re-extraction has to re-check them:

- `stub.py` preserves the `parsing_place` statements by a named rule; a new feedback
  field written by an action and read by the lexer would need the same treatment.
- `lexslice.py` slices by a symbol list and has one named rewrite rule; a renamed or
  newly split lexer function has to be added to the list.
- `src/lexshim.h` shims the pieces of `THD` / `LEX` the sliced lexer touches; the
  ledger of what is and is not modelled is `tools/vendor/thd.md`.
- The grammar's `%expect 72` / `%expect 73` literals are the server's; the merged REST
  SQL rules change the count, and the new count is recorded below with each conflict's
  resolution.
- Only `utf8mb4` is lexed; other charsets are refused, never approximated.
- `sql_mode` `ORACLE` and `MSSQL` are refused: the Oracle grammar variant is not
  extracted.

## Changes to the tools since the original project

- `tools/gen_tables.py` was removed: the runtime builds the same tables from bison's
  own arrays.
- `tools/lexslice.py` no longer passes GCC's `-fdiagnostics-plain-output` when it
  compiles the state-map generator, so it also runs with Clang.

## Manual deviations from upstream

None yet. Each entry: file, what was changed, why, and the upstream commit it tracks.
