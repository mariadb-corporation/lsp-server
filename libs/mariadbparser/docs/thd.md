# THD ledger: every piece of server state the lexer reads

The sliced lexer (`generated/lexer_extracted.cc`) is MariaDB's own
`Lex_input_stream` code. Wherever it reads `thd`, `m_thd`, `lex` or the current
`SELECT_LEX`, it reads a field of the fake `THD` / `LEX` / `SELECT_LEX` that
`src/lexshim.h` declares and `src/mdbp_lex.cc` fills in. This file lists every
such read, with what the library assumes in place of the server's state.

This ledger is maintained here, with the library. It started as a copy from the
research project the extraction tools came from, which is not part of this
repository; there is no upstream copy to stay in sync with. Update it whenever
`src/lexshim.h` starts or stops modelling a piece of server state, and re-check
the line numbers at each re-extraction.

Line numbers are `sql/sql_lex.cc` and `sql/sql_yacc.yy` of the MariaDB server
at the pin in `UPSTREAM.md`.

## 1. `curr_sel->parsing_place`: the parser-state feedback

`parsing_place` is a field of the current `SELECT_LEX`, set by grammar actions
and read by `lex_token`. In this library the grammar actions that write it are
kept verbatim in `yy_stub.yy` (stub.py's `preserve_parsing_place` rule), and the
`SELECT_LEX` they write is the same object the lexer reads (`mdbp_lex_select()`
in `src/mdbp_lex.cc`). So the five read sites below run as in the server
whenever the parser drives the lexer (`mdbp_parse_sql()` in `src/mdbp_api.c`).
When the lexer runs without a parser (`mdbp_lex_sql()`), nothing writes the
field and it stays `NO_MATTER`.

| # | read at | C condition | set by (`sql_yacc.yy`) and used by |
|---|---|---|---|
| 1 | sql_lex.cc:1961 (`case VALUES`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → return `VALUES`, **no lookahead** | set at sql_yacc.yy:2651 (`create:` → `BEFORE_OPT_LIST`), sql_yacc.yy:14243 (`insert_start:` → `BEFORE_OPT_LIST`), sql_yacc.yy:14324 (`insert_field_list:` → `AFTER_LIST`); read at sql_yacc.yy:16368. Used by sql_yacc.yy:9278 (`table_value_constructor: VALUES values_list`, reached through `insert_values` sql_yacc.yy:14341 → `create_select_query_expression` sql_yacc.yy:5019) and sql_yacc.yy:17639. The contractions it suppresses are sql_yacc.yy:5472 (`VALUES_LESS_SYM THAN_SYM`) and sql_yacc.yy:5486 (`VALUES_IN_SYM`), which belong to `part_values_in`, not to `INSERT`. |
| 2 | sql_lex.cc:1971 (`case VALUES`) | `parsing_place ∈ {IN_UPDATE_ON_DUP_KEY, IN_PART_FUNC}` → return `VALUE_SYM` | set at sql_yacc.yy:14485 (`opt_insert_update:` after `ON DUPLICATE KEY UPDATE` → `IN_UPDATE_ON_DUP_KEY`, cleared again at sql_yacc.yy:14489) and sql_yacc.yy:5151 (`part_type_def: LIST_SYM` → `IN_PART_FUNC`, cleared at sql_yacc.yy:5156). Used by sql_yacc.yy:10676 (`VALUE_SYM '(' simple_ident_nospvar ')'` → `Item_insert_value`): in these expression contexts `VALUES(col)` is the function the grammar spells `VALUE_SYM`. |
| 3 | sql_lex.cc:1985 (`case VALUE_SYM`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → return `VALUES` | same three setters as row 1. Used by sql_yacc.yy:9278: the grammar has no `VALUE_SYM` spelling of the insert verb, so `INSERT INTO t VALUE (1)` only parses if the lexer rewrites the token; without the swap it would reach sql_yacc.yy:10676 (`VALUE_SYM '('`) or sql_yacc.yy:3932 (`opt_value: VALUE_SYM`) instead. |
| 4 | sql_lex.cc:1994 (`case PARTITION_SYM/SELECT_SYM/UNION_SYM`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → clear it | clears what sql_yacc.yy:2651 / 14243 / 14324 set, before the `'('` of sql_yacc.yy:12792 (`use_partition: PARTITION_SYM '(' using_list ')'`), a **plain** `'('`, which is why `PARTITION_SYM` must clear; sql_yacc.yy:14295 saved the value and sql_yacc.yy:12796 restores it, so the insert's own column-list `'('` still sees `BEFORE_OPT_LIST`. `SELECT_SYM`/`UNION_SYM` do the same for the query-expression arm sql_yacc.yy:5019, whose inner parentheses are plain. The branch changes no token. |
| 5 | sql_lex.cc:2004 (`case left_paren`) | `parsing_place == BEFORE_OPT_LIST` → look ahead one token and return `LEFT_PAREN_LIKE`/`LEFT_PAREN_WITH`/`LEFT_PAREN_ALT`/`'('` | set at sql_yacc.yy:2651 (`CREATE TABLE t …`) and sql_yacc.yy:14243 (`INSERT INTO t …`); `AFTER_LIST` (sql_yacc.yy:14324) does **not** open this arm. Declared at sql_yacc.yy:430-432; used by sql_yacc.yy:5005 (`create_like: LEFT_PAREN_LIKE LIKE table_ident ')'`), sql_yacc.yy:5025 (`create_select_query_expression: LEFT_PAREN_WITH with_clause …`), sql_yacc.yy:6318 (`create_field_list_parens: LEFT_PAREN_ALT field_list ')'`) and sql_yacc.yy:14322 (`insert_field_list: LEFT_PAREN_ALT opt_fields ')'`). The `'('`, `SELECT_SYM`, `VALUES` followers stay a plain `'('` because they start the `query_expression` of sql_yacc.yy:5019; a bare `WITH` is handled by the read at sql_yacc.yy:16368, which clears `BEFORE_OPT_LIST` itself. |

Row 5 consumes the flag: sql_lex.cc:2008-2013 takes the lookahead and then sets
`parsing_place= NO_MATTER` (sql_lex.cc:2013) before the four-way return at
sql_lex.cc:2014-2021, so one grammar action arms exactly one `(`. Example:
in `CREATE TABLE t (a int) PARTITION BY RANGE (a) (PARTITION p0 VALUES LESS THAN (1))`,
sql_yacc.yy:2651 arms `BEFORE_OPT_LIST` once, the field-list `(` (the
`LEFT_PAREN_ALT` of sql_yacc.yy:6318) consumes it, and the partition list's `(`
is a plain `'('`. On top of the C, `src/mdbp_lex.cc`
(`mdbp_downgrade_left_paren()`) keeps a merged `LEFT_PAREN_*` token only if the
lexer's `valid()` callback (the live parser during a parse, "never" without one)
accepts it, and otherwise returns the plain `'('`. That is equivalent to the
C declining the arm: the lookahead buffering and the clearing happen before the
choice, as they do for the C's own plain `'('` return.

The `parsing_place` values (`NO_MATTER`, `BEFORE_OPT_LIST`, `AFTER_LIST`,
`IN_UPDATE_ON_DUP_KEY`, `IN_PART_FUNC`, …) are `enum enum_parsing_place`,
declared once in `src/stub.h`. `NO_MATTER` makes every condition above false.

### Lexer state that is not THD state

`next_state` (set by the previous call, sql_lex.cc:2049; for example
`MY_LEX_HOSTNAME` after `'@'` at sql_lex.cc:2683, `MY_LEX_IDENT_SEP` at :2196,
`MY_LEX_IDENT_OR_QUALIFIED_SPECIAL_FUNC` at :2200, `MY_LEX_IDENT_OR_KEYWORD`
at :2699, `MY_LEX_SYSTEM_VAR` at :2679) and `lookahead_token` /
`lookahead_yylval` (sql_lex.cc:1896, 1936, 1956, 1981, 2010) are members of
`Lex_input_stream` itself, so they are sliced with the class and behave as in
the server. One consequence worth knowing: the buffered token is returned by the
early branch at sql_lex.cc:1896, **before** the merge switch, so no contraction
is applied to it: `WITH VALUES LESS` is `WITH`, `VALUES`, `LESS_SYM`, not
`WITH`, `VALUES_LESS_SYM`. The C buffers any token there, `END_OF_INPUT`
included, which `query: directly_executable_statement END_OF_INPUT`
(sql_yacc.yy:2164) needs.

## 2. `thd->variables.sql_mode`

The mode is a per-lexer option (the comma list passed to `mdbp_lex_new()`),
fixed for the whole input. Composite names (`ANSI`, `POSTGRESQL`, `DB2`,
`MAXDB`, `MYSQL40`, `MYSQL323`) expand the way `sql/sys_vars.cc`'s
`expand_sql_mode()` does; flags with no lexical effect are accepted and inert.

| flag | read at | effect | this library |
|---|---|---|---|
| `MODE_ANSI_QUOTES` | sql_lex.cc:2388 | `"` delimits identifiers instead of strings | implemented |
| `MODE_PIPES_AS_CONCAT` | sql_lex.cc:1450 | `OR2_SYM` → `MYSQL_CONCAT_SYM` (the `ORACLE_CONCAT_SYM` arm needs `MODE_ORACLE`) | implemented, MariaDB arm only |
| `MODE_HIGH_NOT_PRECEDENCE` | sql_lex.cc:1447 | `NOT_SYM` → `NOT2_SYM` | implemented |
| `MODE_IGNORE_SPACE` | sql_lex.cc:858 → `ignore_space`, used at 2946 | a function keyword may have whitespace before `(` | implemented |
| `MODE_NO_BACKSLASH_ESCAPES` | sql_lex.cc:1665 | `\` is an ordinary byte inside a literal | implemented |
| `MODE_ORACLE` | sql_lex.cc:1420-1444 (keyword table), 1451 (`ORACLE_CONCAT_SYM`), 2079 (`PERCENT_ORACLE_SYM`), 2625 (`COLON_ORACLE_SYM`) | a different dialect | **refused**: the Oracle grammar variant is not extracted |
| `MODE_MSSQL` | sql_lex.cc:2084 | `[ident]` | **refused**, same reason |

`SET sql_mode = '...'` inside the input does not change the mode. The server's
lexer does not track it either: `lex_one_token` reads
`m_thd->variables.sql_mode` on every call (sql_lex.cc:1665, :2388;
`Lex_input_stream::reset` caches `ignore_space` from it at sql_lex.cc:858), and
`thd->variables.sql_mode` is written only when the `SET` executes. A caller that
wants statement-by-statement tracking has to restart the lexer with the new mode
after the `SET` statement.

## 3. Fixed THD assumptions

| state | read at | assumption | substitute / consequence |
|---|---|---|---|
| `stmt_prepare_mode` | sql_lex.cc:2122 | **on** by default: `?` is `PARAM_MARKER` when the next byte is not an identifier byte | Editors want `?` accepted. `Lex_input_stream::reset` sets it `FALSE` (sql_lex.cc:859) and the server's PS code path turns it on; `mdbp_lex_new()` sets it from its `prepare_mode` argument after `init()`. |
| `thd->charset()` | sql_lex.cc:2038-2040 (`state_map`, `ident_map`), 1643, 2913, 2877, 3026 (`charlen`) | **utf8mb4**, always (`utf8mb4_general_ci` is accepted as an alias; any other charset is refused) | `state_map`/`ident_map` are `generated/state_maps.h`, computed by MariaDB's `init_state_maps` (mysys/charset.c:60) over `ctype_utf8mb4` (strings/ctype-utf8.c:2760); `charlen` is the sliced `my_charlen_utf8mb4` (3006). A surprising consequence: with a `use_mb` charset, `scan_ident_start` (2877) and `scan_ident_middle` (2920) set `is_8bit = true` **unconditionally**, so every identifier they return is `IDENT_QUOTED`, not `IDENT`. Only `scan_ident_common` (2800, reached after `@@` or after `.`) calls `get_7bit_or_8bit_ident` and can return `IDENT`. sql_yacc.yy accepts the two interchangeably (`IDENT_cli`, `ident_cli`, `ident_cli_func` at sql_yacc.yy:16644-16668), so this is invisible to the grammar but visible in a token dump. |
| `MYSQL_VERSION_ID` | sql_lex.cc:2488, 2493 | **130100** by default (VERSION says 13.1.0, and `MYSQL_VERSION_ID = major*10000 + minor*100 + patch`) | `MDBP_LEX_DEFAULT_VERSION_ID` in `src/mdbp_lex.h`; `mdbp_lex_set_version_id()` changes it per lexer. Governs `/*!NNNNNN */` and the reversed `/*!!NNNNNN */` form. |
| `thd->killed` | sql_lex.cc:2042 | **never** | The branch (`send_kill_message(); return END_OF_INPUT`) is a server-liveness check with no lexical meaning; `killed` stays 0 and `send_kill_message()` is an empty body. `END_OF_INPUT` is still a declared terminal (sql_yacc.yy:426) that `query` requires at 2130 (empty query), 2164 (unterminated statement) and 2173 (`opt_end_of_input`); its normal source is `MY_LEX_EOL` at eof (sql_lex.cc:2646). |
| `lex->parsing_options.lookup_keywords_after_qualifier` | sql_lex.cc:2191 | **false** | Set true only inside the `field_spec` rule (sql_yacc.yy:6487, reset at 6492), whose action is not kept. With it false, `.` sets `MY_LEX_IDENT_OR_QUALIFIED_SPECIAL_FUNC`, so after a qualifier only `SUBSTRING`/`SUBSTR`/`TRIM`/`REPLACE` resolve as keywords (2745). |
| `lip->hint_comment` | sql_lex.cc:2570 | **false** | `Lex_input_stream::reset` sets it false (860) and only the optimizer-hint parser sets it true, so `HINT_COMMENT` is never emitted and `/*+ ... */` is an ordinary comment. |
| `in_comment` / `save_in_comment_state` | sql_lex.cc:2420, 2556, 2603, 2646, 2651 | real lexer state, sliced | Note the C does **not** restore `in_comment` when `/*!` is followed by non-digits (2556): the body is lexed as SQL and the matching `*/` is swallowed by `MY_LEX_END_LONG_COMMENT`. |
| `m_echo` / `m_cpp_*` / `body_utf8_*` (the pre-processed buffer) | sql_lex.cc:1554, 2534, and every `yyGet`/`yySkip` | not modelled | The pre-processed buffer is the statement text the binlog and the query cache see; it never feeds back into the token stream. The `body_utf8_*` members are empty bodies in `src/mdbp_lex.cc`. The raw-buffer overwrites at 2534 and 2499-2501 (blanking a reversed comment's digits) write *behind* `m_ptr`, into bytes the lexer has already passed. |
| `add_digest_token` / `m_digest` | sql_lex.cc:1910 and each merge arm | not modelled | Performance-schema digest only. `src/mdbp_lex.cc` uses the call, which follows every `lex_one_token()` return, to record the token's byte span. |
| `thd->alloc`, `thd->strmake`, `str->set_ident` | sql_lex.cc:1693, 1794, 2844, 2973 | not modelled as a `MEM_ROOT` | Semantic values are allocated from a per-lexer arena that lives until `mdbp_lex_free()`. Two places where the C's *token value* is narrower than the scan position: `MY_LEX_CHAR` sets the keyword length to 1 (2090) although the escape fall-through consumed two bytes, and `scan_ident_middle` uses `length` (2946-2967) although `IGNORE_SPACE` left `m_ptr` past the skipped spaces. `MY_LEX_COMMA`'s `restart_token()` (2136) moves `m_tok_start` past the comma for `remember_name`. |
