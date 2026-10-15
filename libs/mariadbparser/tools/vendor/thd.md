# THD ledger — every piece of server state the lexer reads

PLAN.md P2a deliverable 2. One row per place `Lex_input_stream` reads `thd`,
`m_thd` or `lex`, what this plan assumes instead (D-8, D-9) and what the
tree-sitter scanner (P4) will substitute. Line numbers are
`<mariadb-source>/sql/sql_lex.cc` at the sha256 in
`source/PROVENANCE.json` (14,345 lines), and `sql/sql_yacc.yy` (21,202 lines).

Nothing here is a licence to deviate: where a row says "not applied", the C
branch is present in `tools/bison2ts/reflex.py` with its citation and a comment
saying why it is unreachable, so a later phase can switch it on in one place.

## 1. `curr_sel->parsing_place` — the parser-state feedback (§1.3 item 2)

`parsing_place` is a field of the *current SELECT_LEX*, set by grammar actions
and read by `lex_token`. tree-sitter has no equivalent, but it hands the
scanner `valid_symbols[]`: the set of terminals the parser can shift next. The
substitution is per site, and each predicate below is what `reflex.py`
implements and what `src/scanner.c` will implement in P4.

| # | read at | C condition | `sql_yacc.yy` it replaces (setters → grammar uses) | `valid()` predicate | why it is the right stand-in | `scanner.c` (P4a) |
|---|---|---|---|---|---|---|
| 1 | sql_lex.cc:1961 (`case VALUES`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → return `VALUES`, **no lookahead** | set at sql_yacc.yy:2651 (`create:` → `BEFORE_OPT_LIST`), sql_yacc.yy:14243 (`insert_start:` → `BEFORE_OPT_LIST`), sql_yacc.yy:14324 (`insert_field_list:` → `AFTER_LIST`); read at sql_yacc.yy:16368. Used by sql_yacc.yy:9278 (`table_value_constructor: VALUES values_list`, reached through `insert_values` sql_yacc.yy:14341 → `create_select_query_expression` sql_yacc.yy:5019) and sql_yacc.yy:17639. The contractions it suppresses are sql_yacc.yy:5472 (`VALUES_LESS_SYM THAN_SYM`) and sql_yacc.yy:5486 (`VALUES_IN_SYM`), which belong to `part_values_in`, not to `INSERT`. | `valid("VALUES") and not (valid("VALUES_LESS_SYM") or valid("VALUES_IN_SYM"))` | The two `parsing_place` values are set by `insert_start` (sql_yacc.yy:14243) and `insert_field_list` (14324), i.e. exactly where the parser is about to accept `INSERT ... VALUES`. Measured on the pinned automaton: `VALUES` is shiftable in 41 states, `VALUES_LESS_SYM`/`VALUES_IN_SYM` in 10 (the partition-bound states) and only 9 of those 10 also take `VALUES`, so the predicate separates the insert sites from the partition sites. The branch's only observable effect is suppressing the contraction, so a false negative costs nothing when the follower is not `LESS`/`IN`. | `src/scanner.c` `identifier_tail`, the `VALUES` arm — **not implemented**. The C's gate is `parsing_place`, and its two halves need `valid("VALUES")`, which `valid_symbols[]` cannot answer: `VALUES` is an internal token under the amended D-3. The scanner does the contraction only when `valid_symbols[VALUES_LESS_SYM] \|\| valid_symbols[VALUES_IN_SYM]`, which is the queryable half and suppresses it exactly where the merged token is unreachable. One further **divergence is disclosed here rather than modelled**, and it applies to the unconditional `WITH`/`FOR` arms too: `skip_between_tokens()` (`src/scanner.c`) returns `SKIP_GIVE_UP` on an *executable*-comment opener between the two words, and `merge_word()` declines. The server lexes such a comment's body as SQL, so `VALUES /*!50000 LESS*/ THAN` really is `VALUES_LESS_SYM` at sql_lex.cc:1972 and the scanner leaves two plain words instead; seeing through the opener would mean deciding the version condition and re-lexing the body inside a one-token lookahead. This is the `(`-site abstention's twin (row 5, `enum SkipResult`) but, unlike it, it is not an equivalence. Fixtures `mrg-with-cube-across-an-executable-comment` and `mrg-values-less-across-an-executable-comment`; no corpus record reaches it (`corpus_disagreements` 0). |
| 2 | sql_lex.cc:1971 (`case VALUES`) | `parsing_place ∈ {IN_UPDATE_ON_DUP_KEY, IN_PART_FUNC}` → return `VALUE_SYM` | set at sql_yacc.yy:14485 (`opt_insert_update:` after `ON DUPLICATE KEY UPDATE` → `IN_UPDATE_ON_DUP_KEY`, cleared again at sql_yacc.yy:14489) and sql_yacc.yy:5151 (`part_type_def: LIST_SYM` → `IN_PART_FUNC`, cleared at sql_yacc.yy:5156). Used by sql_yacc.yy:10676 (`VALUE_SYM '(' simple_ident_nospvar ')'` → `Item_insert_value`). | `valid("VALUE_SYM") and not valid("VALUES")` | Set at sql_yacc.yy:14485 (`ON DUPLICATE KEY UPDATE`) and 5151 (`PARTITION BY LIST`), both *expression* contexts where `VALUES(col)` is the function spelt `VALUE_SYM` in the grammar and the `VALUES` verb cannot appear. Measured: `VALUE_SYM` is shiftable in 768 states, `VALUES` in 41, 18 in common — the predicate is true in the 750 expression states and false wherever the insert verb is still possible. | **Not implementable.** `VALUES` and `VALUE_SYM` are both internal tokens, so the scanner can neither read nor write them. Measured cost: 39 corpus records (`evidence/P4a/convergence.json#secondary.merge_layer_records.values_to_value_sym`). PLAN.md §5 P4a's amendment states the grammar-level fix and why P4a does not take it. See `docs/P4b-INHERITANCE.md` for how P4b inherits this (100 records total — 39 + 61, `evidence/P4a/convergence.json#secondary.merge_layer_records` — beside D-13's keyword-after-`.`/`@` risk). |
| 3 | sql_lex.cc:1985 (`case VALUE_SYM`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → return `VALUES` | same three setters as row 1 — sql_yacc.yy:2651, sql_yacc.yy:14243, sql_yacc.yy:14324. Used by sql_yacc.yy:9278: the grammar has no `VALUE_SYM` spelling of the insert verb, so `INSERT INTO t VALUE (1)` only parses if the lexer rewrites the token; without the swap it would reach sql_yacc.yy:10676 (`VALUE_SYM '('`) or sql_yacc.yy:3932 (`opt_value: VALUE_SYM`) instead. | `valid("VALUES") and not valid("VALUE_SYM")` | The mirror of row 2: `INSERT INTO t VALUE (1)` must reach the parser as `VALUES`. In the 23 states that take `VALUES` but not `VALUE_SYM` the swap is exactly what the grammar needs; where both are acceptable the C would have `NO_MATTER` and no swap happens. | **Not implementable**, same reason as row 2. Measured cost: 61 corpus records (`…merge_layer_records.value_sym_to_values`). See `docs/P4b-INHERITANCE.md` (100 records total — 39 + 61, `evidence/P4a/convergence.json#secondary.merge_layer_records` — beside D-13's keyword-after-`.`/`@` risk). |
| 4 | sql_lex.cc:1994 (`case PARTITION_SYM/SELECT_SYM/UNION_SYM`) | `parsing_place ∈ {BEFORE_OPT_LIST, AFTER_LIST}` → clear it | clears what sql_yacc.yy:2651 / sql_yacc.yy:14243 / sql_yacc.yy:14324 set, before the `'('` of sql_yacc.yy:12792 (`use_partition: PARTITION_SYM '(' using_list ')'`) — a **plain** `'('`, which is why `PARTITION_SYM` must clear; sql_yacc.yy:14295 saved the value and sql_yacc.yy:12796 restores it, so the insert's own column-list `'('` still sees `BEFORE_OPT_LIST`. `SELECT_SYM`/`UNION_SYM` do the same for the query-expression arm sql_yacc.yy:5019, whose inner parentheses are plain. | **none** | The branch only assigns `NO_MATTER`; it changes no token. `reflex.py` returns the token unchanged and records the site. | Nothing to do: the branch changes no token. `src/scanner.c` has no code for it, as `reflex.py` has none. |
| 5 | sql_lex.cc:2004 (`case left_paren`) | `parsing_place == BEFORE_OPT_LIST` → look ahead one token and return `LEFT_PAREN_LIKE`/`LEFT_PAREN_WITH`/`LEFT_PAREN_ALT`/`'('` | set at sql_yacc.yy:2651 (`CREATE TABLE t …`) and sql_yacc.yy:14243 (`INSERT INTO t …`); `AFTER_LIST` (sql_yacc.yy:14324) does **not** open this arm. Declared at sql_yacc.yy:430-432; used by sql_yacc.yy:5005 (`create_like: LEFT_PAREN_LIKE LIKE table_ident ')'`), sql_yacc.yy:5025 (`create_select_query_expression: LEFT_PAREN_WITH with_clause …`), sql_yacc.yy:6318 (`create_field_list_parens: LEFT_PAREN_ALT field_list ')'`) and sql_yacc.yy:14322 (`insert_field_list: LEFT_PAREN_ALT opt_fields ')'`). The `'('`, `SELECT_SYM`, `VALUES` followers stay a plain `'('` because they start the `query_expression` of sql_yacc.yy:5019; a bare `WITH` is handled by the read at sql_yacc.yy:16368, which clears `BEFORE_OPT_LIST` itself. | gate: `valid("LEFT_PAREN_ALT") or valid("LEFT_PAREN_WITH") or valid("LEFT_PAREN_LIKE")` (the predicate PLAN.md §1.3(2) names); **inside** the gate the choice is the C's, on the follower alone — and the chosen token is emitted only if `valid(chosen)`, which is the stand-in for sql_lex.cc:2013 *consuming* `parsing_place` | Measured on the pinned automaton, the three merged tokens are shiftable in five states between them — 4020 (`ALT`+`LIKE`, plain `'('` *not* acceptable), 4504 and 4756 (`ALT`+`WITH`+`'('`), 4960 and 5767 (`WITH`+`'('`) — against 554 states for plain `'('`. So the disjunction is a narrow gate that no single member covers, which is why the *gate* is a disjunction. The gate is not the whole predicate, because the C's flag is **consumed**: sql_lex.cc:2008-2013 takes the lookahead and then sets `curr_sel->parsing_place= NO_MATTER` (sql_lex.cc:2013) *before* the four-way return at sql_lex.cc:2014-2021, so one grammar action arms exactly one `(`. `valid_symbols[]` is a property of the parser state and cannot be spent, so the consumption is modelled the only way it can be — by asking `valid()` about the token the C would actually return and emitting it only if the parser can take it. That is a reproduction of `sql_lex.cc`, not a deviation from it: `valid()` simulates reductions (`ReplayValid._can_shift` pops the stack), so `valid(chosen)` false means the parser would refuse `chosen` outright. State 5767 is the measured instance and shows the arm is already spent there: in `CREATE TABLE t (a int) PARTITION BY RANGE (a) (PARTITION p0 VALUES LESS THAN (1))`, sql_yacc.yy:2651 arms `BEFORE_OPT_LIST` once, the field-list `(` — the `LEFT_PAREN_ALT` of sql_yacc.yy:6318 `create_field_list_parens` — consumes it at sql_lex.cc:2013, and by the partition list's `(` the flag is `NO_MATTER`, so sql_lex.cc:2005-2007 returns the plain `'('`. It cannot be `BEFORE_OPT_LIST` in 5767, and the automaton agrees: `LEFT_PAREN_ALT` is absent from 5767's `expected` set, which is `'('`, `LEFT_PAREN_WITH`, `SELECT_SYM`, `VALUES`, `WITH` (`ledger/disagreements/d1.jsonl`, `error.expected` of classes `5767/INTERVAL_SYM`, `5767/USING`, `5767/'+'`, `5767/CREATE`). Measured cost of the gate alone: `evidence/P2b/trajectory.jsonl` iter 1 → 2 drops `fp_total` from 3,645 to 2,800 — 845 records the family test turned into false positives. | `src/scanner.c`, the `c == '('` branch of `…_scan`: the gate is `valid_symbols[LEFT_PAREN_ALT] \|\| [LEFT_PAREN_WITH] \|\| [LEFT_PAREN_LIKE]`, the four-way choice is the C's own on the follower (`mark_end()` after the `(`, then a peek past whitespace and plain comments), and the chosen token is emitted only if `valid_symbols[chosen]` — verbatim what this row's `valid()` predicate says. Two follower shapes needed spelling out, because "not a comment" and "not modellable" are different answers (`enum SkipResult` in `src/scanner.c`): a `-` or `/` that merely *starts* like a comment is an operator, so it is none of `(`/`SELECT`/`VALUES` and the C reaches sql_lex.cc:2021's `LEFT_PAREN_ALT` (fixtures `paren-alt-before-a-minus`, `paren-alt-before-a-division`; the scanner used to abstain there, which the gated fuzz found); an *executable*-comment opener would have its body lexed as SQL, which a one-token lookahead cannot model, so the scanner abstains and the plain `'('` is left to tree-sitter (fixture `paren-plain-before-an-executable-comment`). During error recovery (all 29 valid) it is off and the plain `'('` wins. |

`parsing_place` values themselves (`NO_MATTER`, `BEFORE_OPT_LIST`, `AFTER_LIST`,
`IN_UPDATE_ON_DUP_KEY`, `IN_PART_FUNC`) are `enum_parsing_place` in sql_lex.h.
`NO_MATTER` is the value everywhere outside those five actions, and it makes
every predicate above false — which is why `reflex.py`'s default `valid()`
returns `False` for everything and `lexdump.py --valid none` is the default.

### 1b. `next_state` — the lexer's own carried state (added by P4a)

`parsing_place` is not the only thing `lex_one_token` reads from outside the
bytes in front of it: it starts in `next_state`, which the *previous* call set
(sql_lex.cc:2049). tree-sitter gives an external scanner no equivalent — it
restores the scanner's serialised state from the last **successful** external
token before every scan (`lib/src/parser.c:537`), and every token that sets an
interesting `next_state` is one the scanner abstains on — so each of these
needed an answer in P4a.

| # | state, set at | what it changes | `src/scanner.c` |
|---|---|---|---|
| 9 | `MY_LEX_HOSTNAME`, set at sql_lex.cc:2683 when `lex_one_token` returns `'@'` | the run after `@` is one `LEX_HOSTNAME` over `[alnum._$]`, not an identifier, a number or a qualified name | **substituted by `valid_symbols[LEX_HOSTNAME]`**, plus two conditions the API does give: the token must be *adjacent* to the previous one (MY_LEX_HOSTNAME scans from the byte right after the `@` with no skip loop, so a hostname can never follow whitespace — this is what keeps `CREATE USER test.user` out of it), and the run must be one no tree-sitter reading can deliver into the slot. **P11 corrected the second condition**, which said "one the internal `IDENT` token cannot produce" — a leading digit or dot, an embedded dot, or a non-ASCII character — on the reading that `ident_or_text: ident \| TEXT_STRING_sys \| LEX_HOSTNAME` accepts `ident` everywhere it accepts `LEX_HOSTNAME`. It does, but under D-13 the run is not offered to `IDENT` at all when its spelling is in the `reserved:` set: tree-sitter answers the keyword terminal unconditionally, and for the spellings `ident_or_text` refuses the parser then has no action at all — which was PLAN.md §5 P11's `e2e/lex_hostname/ts_rejects` class, 145 records including `SET @condition = 0`. So the arm also claims a run whose spelling is in `reserved:` and is accepted neither by `ident_or_text` nor by the function-keyword arm, and is not live as a keyword in any LR state that offers `LEX_HOSTNAME` (that last set is what the substitute cannot see: it costs 5 of the class's records and is what keeps `CHARSET=binary` and `DEFINER=CURRENT_USER` reading as the server reads them — measured, `evidence/P11/deltas.json`). Two things the substitute still cannot do: the *empty* run the C returns for `SET @ = 1`, `select @` and `… to foo@` (22 records — a zero-width claim would have to fire where the C would not, at every `ENGINE='InnoDB'`), and the run behind a `@` the scanner never saw. Measured: 0 D3 disagreements over 212,332 corpus records with the replay oracle (`evidence/P4a/convergence.json#secondary.corpus_disagreements`), and that population has a published size — the two emission paths are reached in **125** of them, **142** compared positions, all of them in agreement (`…#secondary.hostname_records` / `hostname_tokens_compared` / `hostname_tokens_agreed`); the branches themselves are pinned by the `host-*` rows of `tests/fixtures/scanner/scanner.jsonl`, which are the only fixtures that put `LEX_HOSTNAME` in `valid_symbols[]`. The substitution is exact only where the automaton really offers `LEX_HOSTNAME`: a probe that makes it valid at *every* position — no parser state does — gives **140** divergences over 20,000 fuzz cases (`…#secondary.hostname_probe_disagreements`), every one of them a position where the scanner claims a hostname and the C did not: `evidence/P4a/d3.json#hostname_probe.classes` is `EXTRA->LEX_HOSTNAME` 109 (the C lexed an `ident` from `MY_LEX_START` and π dropped it as internal) and `IDENT_QUOTED->LEX_HOSTNAME` 31 (the same class of token over a *longer* extent, because `hostname_char` runs past `.`). Under scope `hostname_probe` the first **40** of them are recorded in `ledger/disagreements/d3.jsonl` — `merge()` caps a scope's examples at 40 — and none is counted in `fuzz_disagreements`; `hostname_probe()` in `tools/diff_d3_run.py` says why. |
| 10 | `MY_LEX_IDENT_SEP` (:2196), `MY_LEX_IDENT_OR_QUALIFIED_SPECIAL_FUNC` (:2200), `MY_LEX_IDENT_OR_KEYWORD` (:2699), `MY_LEX_SYSTEM_VAR` (:2679) | inside a qualified name or after `@@`, `.5` is `'.'` + an identifier rather than a `DECIMAL_NUM`, and `3` is an `IDENT` rather than a `NUM` | **no substitute exists.** The scanner is always entered as if from `MY_LEX_START`. D3 excludes these regions on both sides (`tools/bison2ts/d3.py`'s `p6_lexer_state`/`s5_lexer_state`) and publishes their size. Over the corpus — the scope this row is about — `evidence/P4a/convergence.json#secondary.projection_dropped_corpus` says **6,036** reference tokens and **6,116** scanner tokens against `secondary.corpus_tokens_compared` = **503,804** compared. The whole-run counts, which also cover the fixtures and the 100,000 fuzz cases, are `secondary.projection_dropped`: **6,324** and **6,475**. It costs nothing in SQL — the sequences are `ident . digits` and `@@digits`, and an unquoted all-digit identifier is not legal MariaDB — but it is a real difference and it is counted. |
| 11 | `lookahead_token`, sql_lex.cc:1896 | a token the C peeked at is handed back **before** the merge switch, so it is never itself contracted | **no substitute exists**: tree-sitter re-lexes that position from scratch, so the scanner re-applies the merge switch there. D3 drops those positions (`s2_buffered_merge`). Measured: **0** over the corpus (`evidence/P4a/convergence.json#secondary.projection_dropped_corpus.s2_buffered_merge`) — the site exists but no corpus record reaches it — against **97** in the run as a whole (`…#secondary.projection_dropped.s2_buffered_merge`: 1 fixture, `mrg-lookahead-not-remerged`, and 96 fuzz cases). The rule is exercised only by the fixture and the fuzzer; nothing in the corpus depends on it. |

## 2. `thd->variables.sql_mode` (D-8, §1.3 item 3)

`sql_mode` is a constructor argument of `Reflex`; the P4 scanner will keep it
as scanner state and update it lexically from `SET sql_mode = '...'`.

| flag | read at | effect | this plan |
|---|---|---|---|
| `MODE_ANSI_QUOTES` | sql_lex.cc:2388 | `"` delimits identifiers instead of strings | implemented |
| `MODE_PIPES_AS_CONCAT` | sql_lex.cc:1450 | `OR2_SYM` → `MYSQL_CONCAT_SYM` (the `ORACLE_CONCAT_SYM` arm needs `MODE_ORACLE`) | implemented, MariaDB arm only |
| `MODE_HIGH_NOT_PRECEDENCE` | sql_lex.cc:1447 | `NOT_SYM` → `NOT2_SYM` | implemented |
| `MODE_IGNORE_SPACE` | sql_lex.cc:858 → `ignore_space`, used at 2946 | a function keyword may have whitespace before `(` | implemented |
| `MODE_NO_BACKSLASH_ESCAPES` | sql_lex.cc:1665 | `\` is an ordinary byte inside a literal | implemented |
| `MODE_ORACLE` | sql_lex.cc:1420-1444 (keyword table), 1451 (`ORACLE_CONCAT_SYM`), 2079 (`PERCENT_ORACLE_SYM`), 2625 (`COLON_ORACLE_SYM`) | a different dialect | **recognised and ignored** (D-8). PLAN.md §0.4 puts Oracle mode in §8; the branches are in `reflex.py`, unreachable, with their citations. |
| `MODE_MSSQL` | sql_lex.cc:2084 | `[ident]` | **recognised and ignored**, same reason |

Any other flag name raises `ReflexError` (→ `lexdump.py` exit 2) rather than
being silently accepted: a mode this lexer cannot honour must not look honoured.

**`SET sql_mode = '...'` self-update is deferred to P4** (D-8 describes the
*scanner*). `Reflex` takes the mode as a constructor argument and that argument
stays authoritative; no lexical tracking is implemented in P2a, not even behind
a flag. Reason: every P2a and P2b caller — the fixtures, `lexdump.py --mode`,
the D1 corpus records (all of which carry `sql_mode: ""`) — knows the mode up
front, so a second source of truth would be untested code in the reference
implementation the rest of the plan is measured against. P4 implements it once,
where tree-sitter's lack of per-parse options makes it necessary.

**P4a: what `src/scanner.c` accepts.** The scanner cannot recognise the
statement token by token — `SET`, `SQL_MODE` and `=` are internal tokens under
the amended D-3 and it never sees them, and tree-sitter restores its state from
the last *successful* external token before every scan
(`lib/src/parser.c:537`), so anything an abstaining scan wrote is discarded.
It therefore looks **forward** from each token it produced that can precede a
statement — `_terminator`, `delimiter_directive`, `comment`,
`version_comment_open`, `version_comment_close` — and accepts exactly:

```
SET [SESSION | GLOBAL | LOCAL] SQL_MODE = 'flags'
SET @@[SESSION.|GLOBAL.|LOCAL.]SQL_MODE = 'flags'
```

case-insensitive, any whitespace between the words, `:=` as well as `=`, and
`'` or `"` quoting (`"` only while `ANSI_QUOTES` is off). `flags` is a
comma-separated list; names outside the seven above are ignored rather than
fatal, and `''` clears every flag.

The forward look only **announces** the mode. It becomes effective at the next
`_terminator` or `delimiter_directive` — i.e. once the `SET` statement is over
— so the statement that sets the mode is itself lexed under the *old* mode,
which is what the server does: `lex_one_token` reads
`m_thd->variables.sql_mode` on every call (sql_lex.cc:1665, :2388;
`Lex_input_stream::reset` caches `ignore_space` from it at sql_lex.cc:858) and
`thd->variables.sql_mode` is written only when the `SET` *executes*, after its
own statement has been parsed. `SET sql_mode="ANSI_QUOTES"` therefore lexes its
own `"ANSI_QUOTES"` as a `TEXT_STRING`, and only the next statement sees `"` as
an identifier quote; the mirror holds for a backslash inside a statement that
turns `NO_BACKSLASH_ESCAPES` on. Fixtures
`scanner.jsonl#mode-set-statement-lexed-under-old-mode` and
`#mode-nbe-set-statement-lexed-under-old-mode` pin both, with a double-quoted
and a backslash-bearing value respectively — a single-quoted value cannot tell
the two timings apart. D3 could not see any of this until P10: `reflex.py`
takes `sql_mode` as a constructor argument, so the `scanner.jsonl` fixtures
were the only evidence. P10's `sqlmode_track.TrackingReflex` gives D3 the
other engine for the rows that ask for it (see below).

**The first statement (P10).** Until P10 this section ended: "a `SET sql_mode`
that is the very first statement of an input, with no `DELIMITER` line, comment
or earlier statement in front of it, does not take effect", and the fixture
`#mode-first-statement-not-tracked` pinned it. That was a defect written up as
a consequence, and P10 measured it as one: the *identical* text takes effect
behind a `--` comment, a `/* */` comment, a `DELIMITER ;` line or a bare `;`,
and fails only with nothing in front of it (`evidence/P10/diagnosis.json#
hypothesis`).

It is repaired. The forward look still needs an anchor, but a document that
opens with the `SET` does have one — the word `SET` itself, which `scan()`
reaches on the scan that *declines* it (it is a plain identifier run, so the
internal keyword token wins and whatever the look consumed afterwards is
outside the token either way). What that look finds is kept in a field
`deserialize()` does not clear **while the state it is handed is empty**, and
"empty" is tree-sitter's own signal that no external token exists yet on this
stack (`ts_parser__external_scanner_deserialize` passes length 0 for a null
`last_external_token`). The first token the scanner really produces merges it
into `pending_sql_mode` and serialises it; the ordinary machinery takes over
from there, and the window closes the moment `deserialize()` is handed a
non-empty state. `tests/fixtures/scanner/scanner.jsonl#mode-first-statement-
tracked` pins it, and the nine `mode-set-*` rows of
`tests/fixtures/lex/sqlmode.jsonl` pin it in **both** engines — six
`mode-set-first-statement-*`, one where a second `SET sql_mode=''` resets, and
two negatives (`mode-set-other-variable-…`, `mode-set-not-statement-initial-…`)
that assert the mode does *not* follow —
`tools/bison2ts/sqlmode_track.py` is the reference side's half, a subclass of
`Reflex` used only by the rows that ask for it. It is deliberately *not* in
`reflex.py`: the server's lexer does not track the mode, its executor does, and
`reflex.py` models `lex_one_token`.

The blast radius was measured rather than assumed: **0 corpus records** change
tokenisation, wrapped or bare (`evidence/P10/diagnosis.json#blast_radius`, with
a control showing the sweep can detect a move). D-7's `DELIMITER` wrapper puts
a produced token in front of every record, and of the 306 records that open
with a `SET sql_mode` only one has anything after that statement's terminator
for the mode to reach.

## 3. Fixed THD assumptions (D-9, §1.3 item 6)

| state | read at | assumption | substitute / consequence |
|---|---|---|---|
| `stmt_prepare_mode` | sql_lex.cc:2122 | **on**: `?` is `PARAM_MARKER` when the next byte is not an identifier byte | The plan fixes prepare mode on (D-9): the arbiter drives statements through `PREPARE`, and editors want `?` accepted. `Reflex(prepare_mode=...)` and `lexdump.py --no-prepare-mode` exist so the other branch stays testable; P4's scanner hard-codes on. Set to `FALSE` by `Lex_input_stream::reset` (sql_lex.cc:859) and turned on by the PS code path. |
| `thd->charset()` | sql_lex.cc:2038-2040 (`state_map`, `ident_map`), 1643, 2913, 2877, 3026 (`charlen`) | **utf8mb4**, always | `state_map`/`ident_map` are built by `_init_state_maps()` following `init_state_maps` (mysys/charset.c:60) over `ctype_utf8mb4` (strings/ctype-utf8.c:2760); `charlen` follows `my_charlen_utf8mb4` (3006). Consequence recorded here because it is surprising: with a `use_mb` charset, `scan_ident_start` (2877) and `scan_ident_middle` (2920) set `is_8bit = true` **unconditionally**, so every identifier they return is `IDENT_QUOTED`, not `IDENT`. Only `scan_ident_common` (2800, reached after `@@` or after `.`) calls `get_7bit_or_8bit_ident` and can return `IDENT`. sql_yacc.yy accepts the two interchangeably (`IDENT_cli`, `ident_cli`, `ident_cli_func` at sql_yacc.yy:16644-16668), so this is invisible to the grammar — but it is visible in a token dump, and the fixtures pin it. |
| `MYSQL_VERSION_ID` | sql_lex.cc:2488, 2493 | **130100** (VERSION says 13.1.0, and `MYSQL_VERSION_ID = major*10000 + minor*100 + patch`) | `Reflex(version=...)`, default `reflex.MYSQL_VERSION_ID`. Governs `/*!NNNNNN */` and the reversed `/*!!NNNNNN */` form. |
| `thd->killed` | sql_lex.cc:2042 | **never** | The branch (`send_kill_message(); return END_OF_INPUT`) is a server-liveness check with no lexical meaning; `reflex.py` records it at the top of `lex_one_token` and does not implement it. It does *not* make `END_OF_INPUT` a kill marker: `END_OF_INPUT` is a declared terminal (sql_yacc.yy:426) that `query` requires at 2130 (empty query), 2164 (unterminated statement) and 2173 (`opt_end_of_input`), and its normal source is `MY_LEX_EOL` at eof (sql_lex.cc:2646). Killing the thread simply makes the parser see an immediate end of input. |
| `lookahead_token` / `lookahead_yylval` | sql_lex.cc:1896, 1936, 1956, 1981, 2010 | one buffered token | Real lexer state, not THD state, and implemented as such (`Reflex.lookahead_token`). The C buffers **any** token here, `END_OF_INPUT` included (`WITH`, `FOR`, `VALUES` or `(` at end of input), and hands it over on the next call — dropping it would delete the terminal that `query: directly_executable_statement END_OF_INPUT` (sql_yacc.yy:2164) needs. Worth a row because of a second trap: the buffered token is returned by the early branch at 1896, **before** the merge switch, so no contraction is ever applied to it — `WITH VALUES LESS` is `WITH`, `VALUES`, `LESS_SYM`, not `WITH`, `VALUES_LESS_SYM` (fixture `mrg-lookahead-not-remerged`). |
| `lex->parsing_options.lookup_keywords_after_qualifier` | sql_lex.cc:2191 | **false** | Set true only inside the `field_spec` rule (sql_yacc.yy:6487, reset at 6492). With it false, `.` sets `MY_LEX_IDENT_OR_QUALIFIED_SPECIAL_FUNC`, so after a qualifier only `SUBSTRING`/`SUBSTR`/`TRIM`/`REPLACE` resolve as keywords (2745). A `valid()`-based substitute is possible but was not needed: no fixture distinguishes the two, and P4 can revisit it with a `valid()` query on the field-definition tokens. |
| `lip->hint_comment` | sql_lex.cc:2570 | **false** | `Lex_input_stream::reset` sets it false (860) and only the optimizer-hint parser sets it true, so `HINT_COMMENT` is never emitted and `/*+ ... */` is an ordinary comment. `Reflex.hint_comment` exists and is false. |
| `in_comment` / `save_in_comment_state` | sql_lex.cc:2420, 2556, 2603, 2646, 2651 | real lexer state | Implemented. Note the C does **not** restore `in_comment` when `/*!` is followed by non-digits (2556): the body is lexed as SQL and the matching `*/` is swallowed by `MY_LEX_END_LONG_COMMENT`. `reflex.py` reproduces that. |
| `m_echo` / `m_cpp_*` (the pre-processed buffer) | sql_lex.cc:1554, 2534, and every `yyGet`/`yySkip` | not modelled | The cpp buffer is the statement text the binlog and the query cache see; it never feeds back into the token stream. `yyUnput` is implemented as the pointer decrement only, and the raw-buffer overwrites at 2534 and 2499-2501 (blanking a reversed comment's digits) are not performed — both write *behind* `m_ptr`, into bytes the lexer has already passed. |
| `add_digest_token` / `m_digest` | sql_lex.cc:1910 and each merge arm | not modelled | Performance-schema digest only. `gen_lex_token.cc` is digest text, not lexemes (PLAN.md §1.2). |
| `thd->alloc`, `body_utf8_*`, `str->set_ident` | sql_lex.cc:1693, 2844, 2973 | not modelled | Semantic values. `Reflex` reports the raw lexeme span instead; `Token.text == source[start:end]` by construction. Two places where the C's *token value* is narrower than the scan position are reported as the narrower lexeme, with the C line that says so: `MY_LEX_CHAR` sets the keyword length to 1 (2090) although the escape fall-through consumed two bytes, and `scan_ident_middle` uses `length` (2946-2967) although `IGNORE_SPACE` left `m_ptr` past the skipped spaces. `MY_LEX_COMMA`'s `restart_token()` (2136) moves `m_tok_start` past the comma for `remember_name`; the comma keeps its own extent here. |
