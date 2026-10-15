# libmariadbparser

A standalone C parser for MariaDB SQL, made from the server's own Bison grammar
(`sql/sql_yacc.yy`) and lexer (`sql/sql_lex.cc`) with every grammar action blanked, so it
recognises exactly the language the server's LALR automaton accepts. Two facts first:

1. **It parses the grammar-only language.** A string it rejects is invalid SQL; a string
   it accepts is not guaranteed valid, because the semantic checks inside the server's
   actions are not here. Use it to rule statements out, never to certify them.
2. **MariaDB variant only.** The Oracle-mode grammar is not extracted; `sql_mode`
   `ORACLE` and `MSSQL` are refused at parser creation.

The REST SQL (MariaDB REST Service) statements are merged into the same grammar, so one
parser handles both dialects with one lexer. See `UPSTREAM.md` for the pinned upstream
commits, how the generated files were produced, and every manual deviation.

## Layout

```text
include/mdbparse.h   the public C API, the only header a caller needs
src/                 the runtime: API, lexer glue, parser glue, the CLI (mdbparse.c)
generated/           the stubbed grammar, the lexer slice and the token tables;
                     committed, maintained by hand between re-extractions
tools/               stub.py, lexslice.py, gen_tables.py, common.py: the one-shot
                     extraction tools, not used by the build
tests/lex-fixtures/  lexer cases with expected tokens, each citing sql_lex.cc
UPSTREAM.md          pins, provenance of generated/, manual deviations
```

## API in short

`mdbp_parser_new()` creates a parser for a `sql_mode` and charset (utf8mb4 only);
`mdbp_parse_sql()` lexes and parses one statement and reports the verdict, the error
token with its byte offset, the expected-token set at the error (computed with LAC, so
it is the true set), and the derivation (rule numbers in reduce order); `mdbp_lex_sql()`
and `mdbp_lex_sql_prefix()` lex without parsing. One parser per thread, parsers are
independent and may move between threads. The header documents everything, including
the exact ownership of every pointer in a result.

## Building

Built by the top-level CMake of this repository. Needs bison 3.8.2 or newer and a C and
C++ compiler; no Python and no server source tree. The library's own `CMakeLists.txt`
is written in phase P0 of `docs/plans/core-implementation.md`, together with the first
run that fills `generated/`.
