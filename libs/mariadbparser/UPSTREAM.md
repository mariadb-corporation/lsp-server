# Upstream sources and manual deviations

This library is generated once from upstream sources and then maintained by hand. The
generated files live in `generated/` and are committed; the tools in `tools/` are kept
only for a full re-extraction (a new major server version), never run by the build.

## Pins

| Upstream | Commit | What is taken |
| --- | --- | --- |
| MariaDB server (`https://github.com/MariaDB/server`) | `bab03b0fc44e7aeac869b20b0055c6a8f2083eb6`, 13.1.0 alpha, 2026-08-19 | `sql/sql_yacc.yy` (grammar, MariaDB variant), `sql/sql_lex.cc` and `sql/sql_lex.h` (lexer), `sql/lex.h`, `sql/lex_symbol.h`, `sql/lex_ident_cli.h` (keyword tables and token types), `include/m_ctype.h`, `strings/ctype-utf8.{c,h}`, `strings/ctype-latin1.c`, `mysys/charset.c`, `sql/share/charsets/Index.xml` (utf8mb4 character classes), `strings/my_strtoll10.c`, `sql/gen_yy_files.cmake` (the `%ifdef MARIADB` preprocessor), `VERSION` |
| MariaDB Shell (`mariadb-shell`) | *to be recorded when the REST SQL rules are merged* | `mysqlshdk/libs/mrs/parser.yy`, the rules between `START OF MERGE PART` and `END OF MERGE PART` |

## How the generated files came to be

1. `sql/gen_yy_files.cmake` from the server tree applies the `%ifdef MARIADB` preprocessor
   to `sql_yacc.yy`.
2. `tools/stub.py` blanks every action body to `{ MDBP_R(<serial>); }`, keeping only the
   statements that assign `parsing_place` / `save_parsing_place`, because the lexer reads
   that field back. Output: `generated/yy_stub.yy`.
3. bison 3.8.2 with `-Dparse.lac=full` turns it into `parser.c` / `parser.h` and the
   `grammar.xml` report (done by the build, not committed).
4. `tools/lexslice.py` slices `Lex_input_stream::lex_token` / `lex_one_token` and their
   transitive closure out of `sql_lex.cc` by symbol name, and the charset tables the
   lexer needs. Output: `generated/lexer_extracted.cc`, `generated/keywords.h`,
   `generated/lexstates.h`, `generated/state_maps_gen.cc`.
5. `tools/gen_tables.py` derives the token-name tables from bison's `parser.h` and
   `grammar.xml`. Output: `generated/mdbp_tables.h`. (Its `--model` cross-check against
   a `grammar-model.json` from the original corpus is to be removed; the model is not
   kept here.)

Steps 2, 4 and 5 are run again only for a full re-extraction. Between re-extractions,
upstream changes are applied by hand: diff the server file between the old and the new
pin and apply the hunks to the generated file (actions are already blank, so grammar
hunks are small); update the pin above and list the deviation below.

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

## Manual deviations from upstream

None yet. Each entry: file, what was changed, why, and the upstream commit it tracks.
