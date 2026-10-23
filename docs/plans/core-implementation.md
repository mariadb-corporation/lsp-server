# Core implementation plan

Written 2026-10-15 on branch `wip/core-impl`, which starts from `main`. This is the plan
for building the MariaDB language server and its command-line tools. The earlier design
documents on the `architecture-review` branch are not a basis for this plan and are not
carried over; the only thing taken from that branch is the extraction core of
libmariadbparser (§1.1).

Read in order: §1 what we have · §2 the design · §3 build and repository layout ·
§4 phases · §5 open decisions · §6 out of scope.

---

## 1. What we already have, and what it costs us

### 1.1 libmariadbparser (`libs/mariadbparser/`)

The MariaDB server grammar (`sql/sql_yacc.yy`) and lexer (`sql/sql_lex.cc`) extracted
into a standalone C library with every grammar action blanked. What is in this branch:
the public header, the C/C++ sources, the one-shot extraction and merge tools (`stub.py`,
`lexslice.py`, `mrs_merge.py`, with `common.py`; `gen_tables.py` was dropped, the token
tables come from bison's output at run time), the lexer test fixtures as plain
files, and `UPSTREAM.md` with the pins and the procedure. **The generated sources
(`generated/`: the stubbed grammar, the lexer slice, the token tables) are to be
committed and maintained by hand**; the tools are for a full re-extraction only and the
build never runs them. The build orchestration, provenance gate, evidence records,
corpus archives, analysis tooling, Python test suite and regeneration document of the
original project were left behind on purpose. Facts the rest of this plan relies on:

- **API** (`include/mdbparse.h`, version 2.2 then, 2.3 since P3): `mdbp_parse_sql()` lexes and parses one
  statement; `mdbp_lex_sql()` / `mdbp_lex_sql_prefix()` lex without parsing (whole document
  or first N tokens). Every token carries byte offsets.
- **Verdict, not AST.** A parse returns accept/reject, the error token and byte position,
  the **expected-token set at the error** (computed by Bison's own LAC, `parse.lac full`,
  so it is the true set after default reductions), and the **derivation** (rule numbers
  in reduce order). There is no tree. The header states the derivation plus the rule table
  reconstructs the tree offline; §2.4 does exactly that.
- **Threading**: one parser object per thread, parsers independent, may migrate between
  threads over a happens-before edge. This was proven with TSan and a planted-race control
  on the original branch; the evidence files are not here, the contract in the header is.
- **Speed**, as measured on the original branch: 57k statements/s single-threaded over a
  212k-statement corpus; 0.22 ms to re-parse one statement at the 16 KB typing scenario.
  Parsing one statement is microseconds, as the pasted plan assumes.
- **Superset by design**: it rejects nothing the server's automaton accepts, so a reported
  syntax error is real. It accepts a little more than the server (actions are stubbed).
- **Pinned** to MariaDB 13.1.0 at commit `bab03b0f` (2026-08-19). The local clone at
  `/Users/mzinner/git/mariadb-server` is shallow and at `cded2b25` (2026-07-28), which is
  older than the pin, so a worktree or fetch at the pin is needed to build today.
- **Maintenance is manual between re-extractions.** `sql_yacc.yy` and `sql_lex.cc`
  each changed about seventy times on the server's `main` since January 2026; an
  upstream change is applied by diffing the server file between pins and patching the
  generated file by hand (actions are already blank, so grammar hunks are small), with
  the deviation recorded in `UPSTREAM.md`. Re-extracting with the tools is for a major
  jump and is deferred by decision.
- **`generated/` is committed**, produced by P0's one extraction run from the server tree
  at the pin. The plan assumed that run needed a Linux host; it ran on macOS (arm64,
  bison 3.8.2, `UPSTREAM.md`). The build needs bison 3.7.4 or newer and a compiler only, on
  every platform.
- **`REST` is not a keyword** in the server grammar, so `CREATE REST SERVICE ...` is
  rejected by the unmerged parser, usually at `REST`. §2.2 and §3 merge the REST SQL
  rules into the extracted grammar instead of routing to a second parser.

### 1.2 The REST SQL grammar (mariadb-shell `mysqlshdk/libs/mrs/`)

Already a **Bison grammar**, not ANTLR: `parser.yy` (C++ skeleton `lalr1.cc`, variant
values, `%locations`), a hand-written lexer modelled on `sql_lex.cc` (`lexer.{h,cc}`,
byte offset + line/column per token, `Sql_mode{ansi_quotes, no_backslash_escapes}`), an AST
of plain structs and a `std::variant` of 53 statement types (`ast.h`), and a driver with
no global state. `parse_script()` returns the AST or throws `Parse_error{message,line,col}`
on the **first** error. The `.g4` files there are documentation mirrors, not code.
`rest_sql_prefixes()` is the list of opening words the shell itself uses to route a
statement to this parser (`CONFIGURE REST`, `CREATE [OR REPLACE] REST`, `ALTER REST`,
`DROP REST`, `USE REST`, `SHOW [CREATE] REST`, `GRANT REST`, `REVOKE REST`, `CLONE REST`).
The grammar was **written to be merged into `sql_yacc.yy`**: its header says so, the rules
between `/* START OF MERGE PART */` (line 432) and `/* END OF MERGE PART */` (line 2319)
are written in the server grammar's style, and the rules after the marker
(`schema_name`, `pure_identifier`, `identifier`, `qualified_identifier`,
`text_string_literal`, `text_or_identifier`) exist only because the standalone parser has
no server to supply them. What the merge has to bridge:

- **Token names** follow the ANTLR/MySQL convention (`CREATE_SYMBOL`, `USE_SYMBOL`,
  `VIEW_SYMBOL`), the server's follow its own (`CREATE`, `USE_SYM`, `VIEW_SYM`); of the
  133 MRS keywords roughly half exist in the server, the rest (`REST`, `SERVICE`,
  `CONFIGURE`, `METADATA`, `CONTENT`, `AUTH`, `APP`, `VENDOR`, `PUBLISHED`, `ENDPOINTS`,
  `DAEMON`, ...) are new and must stay usable as identifiers.
- **Two lexer-level tokens the server lexer does not produce**: `REST_REQUEST_PATH`
  (`/service/schema`, used 3 times) and 19 `@ANNOTATION` tokens (`@IN`, `@NOCHECK`,
  ..., used 46 times). In the server lexer `/` is a character token and `@name` is
  `'@'` followed by an identifier, so both can be expressed as grammar rules over the
  server's tokens, with the annotation word checked by name in the tree walker.
- The AST (`ast.h`) and the hand-written lexer are not reused in a merge: the merged
  rules are stubbed like every other action, and REST objects come from the derivation
  tree by rule name, as the server statements do.

### 1.3 The statement splitter (mariadb-shell `mysqlshdk/libs/utils/utils_mysql_parsing.{h,cc}`)

`mysqlshdk::utils::Sql_splitter`: a chunked, streaming, byte-oriented splitter with a
context stack (strings, comments, `/*! */`, `$tag$` quoting, backtick/ANSI identifiers),
multi-character `DELIMITER`, `\G`/`\g` shell delimiters, and per-statement
`Range{offset, length, line_num}` plus the ending delimiter. `context()` reports the open
construct of an unfinished tail (the shell's continuation prompt uses it). Tests:
`unittest/utils_mysql_parsing_t.cc` (1,829 lines, run at chunk sizes 0–15 and 4–64).
Dependencies to cut: `scoped_contexts.h` (one call to the SQL handler registry),
`shcore::str_ibeginswith`, `shcore::str_format`, and three lookup tables from
`utils_lexing.h`. About one day's work; licence is GPLv2 with the FOSS exception, same as
this repository.

### 1.4 The VS Code extension (mariadb-shell-plugins `code_ext/`)

No LSP client yet. It has one semantic-token provider (ANSI_QUOTES double-quoted
identifiers), gutter statement markers from a TypeScript scanner with an explicit comment
that "a language server would report the same thing" (`src/extension.ts:473`), a
per-document connection binding (`src/editor/sqlEditorBinding.ts`), `sql_mode` tracking per
connection (`src/connections/sqlModes.ts`), and a shell locator that resolves the
`mariadb-shell` binary on PATH or in the managed install (`src/shell/locator.ts`). The
bundle is Vite; `vscode-languageclient` would bundle automatically. Language ids:
`mariadb-sql` and `sql`.

### 1.5 Connections in the MCP plugin (mariadb-shell-plugins `mcp_plugin/lib/config.py`)

Connections are identified by shell-syntax URIs, stored as generic secrets in the shell's
secret store under the keys `MCP:CONN:<uri>` and `GUI:CONN:<uri>` (the extension's own
list), with folder, caption and colour in a side file. Passwords are read with
`shell.read_secret()`; the plugin never runs a helper itself, so the helper protocol has to
come from the shell's C++ (`mysqlshdk/libs/secret-store-api/`, summarised in §2.11).
Sessions are opened with `shell.open_session(parse_uri(uri) + password)`; the plugin
rejects URIs that carry a password.

### 1.6 The client library: how the shell gets it

mariadb-shell does not take Connector/C from vcpkg. `cmake/bootstrap_mariadb.cmake`
fetches the MariaDB server tree (or uses `-DMARIADB_SOURCE_DIR`/`-DMARIADB_BUILD_DIR`),
initialises its `libmariadb` submodule, configures it, and builds the targets
`mariadbclient mysys mysys_ssl caching_sha2_password GenError`; the shell then links
`libmariadbclient` **statically** from that build, with headers from
`<src>/libmariadb/include`, `<src>/include` and the build's include directories. Shell
TUs pre-define the include guard of `mariadb_capi_rename.h` so that server headers on the
include path do not rename the client C API symbols. rest-daemon, by contrast, still
takes `libmariadb` from vcpkg (its latest commits bundle vcpkg's libmariadb, OpenSSL and
auth plugins); that route is not copied, because the vcpkg port lags the server. Its
`MySQLSession` wrapper (typed options, 5 s connect / 30 s read timeouts, SSL preferred,
per-thread `mysql_thread_init()` tokens) remains a good model for our session class.

### 1.7 The shell build (mariadb-shell `CMakeLists.txt`, `cmake/`)

CMake + Ninja, dependencies from a **vcpkg manifest** (`vcpkg.json`, auto-bootstrapped by
`-DWITH_VCPKG_TRIPLET=<triplet>`, `cmake/bootstrap_vcpkg.cmake`), the MariaDB server source
auto-fetched when none is given (`cmake/bootstrap_mariadb.cmake`), `cmake/bison.cmake`
(Homebrew bison on macOS, `win_bison` on Windows, minimum 3.0.4; libmariadbparser raises
that to 3.8 here, `libs/mariadbparser/CMakeLists.txt`), C++23 with
`-Werror -Wall -Wextra -Wpedantic -Wshadow` (`cmake/compiler.cmake`), `WITH_TESTS=1` by
default pulling gtest through the vcpkg `tests` feature, one `run_unit_tests` binary,
`cpack -G TGZ` packaging, GitHub workflows on a self-hosted runner with ccache and the
`[bypass-ci]` title convention. §3 copies this.

---

## 2. Design

### 2.1 The statement index

One index per open document (and per file read from disk for the CLI and the workspace
catalog). Built by the splitter in one streaming pass; `std::vector<StatementInfo>` ordered
by offset.

```cpp
enum class StatementStatus : uint8_t {
    Unparsed = 0,
    ServerSqlCorrect, ServerSqlIncorrect,
    RestSqlCorrect,   RestSqlIncorrect,
    Skipped,            // comment-only chunk, empty, or a client command (\G, DELIMITER line)
    Unterminated,       // open string/comment/DELIMITER region at end of input
};

enum class StatementKind : uint8_t { ServerSql, RestSql, Command, Comment, Empty };

struct StatementInfo {
    uint32_t start;             // byte offset of the first token, inclusive
    uint32_t end;               // byte offset after the delimiter, exclusive
    uint32_t start_line, end_line;         // 0-based, derived lazily from the line table
    uint32_t start_col,  end_col;          // UTF-16 code units (LSP), derived lazily
    StatementKind kind;
    std::atomic<StatementStatus> status;
    uint8_t  delimiter_len;     // 0 for an unterminated tail
    SqlModeBits sql_mode;       // mode in force at this statement (ANSI_QUOTES, NO_BACKSLASH_ESCAPES, ...)
    uint64_t content_hash;      // hash of the statement bytes: re-parse skipped if unchanged
    ParseResult* result;        // owned; null while Unparsed. See §2.4 for what it holds.
};
```

Points that go beyond the pasted sketch:

- Offsets are bytes into the document buffer, 32-bit (4 GiB ceiling, fine for 256 MB).
  Line/column are **not stored**: a line-start table (sorted `uint32_t` array, built in the
  same pass) converts offsets to lines by binary search, and columns to UTF-16 in one
  module. Storing bytes only means an edit shifts every later statement by one delta
  (§2.6) without touching lines.
- `sql_mode` travels with the statement because the splitter's behaviour depends on it
  (`"` is a string or an identifier), as does lexing. The splitter pass tracks
  `SET [SESSION] sql_mode = ...` statements textually; a connection-provided mode is the
  starting value.
- `content_hash` makes re-parsing idempotent across edits that only move a statement.
- `status` is atomic because parser threads write it while the LSP thread reads it.

**Built as:** `Statement{begin, body_end, end, mode, delimiter, kind, active_delimiter,
mentions_sql_mode, clean_after}` in `include/mdbls/statement_index.h`, `kind` being `sql`
or `comment`; `mode` and `delimiter` index the interned tables of `Statement_index`. There
is no REST-versus-server kind, no atomic status, no `content_hash` and no `ParseResult*` in
the entry. The verdicts (`Statement_check`, `include/mdbls/syntax_check.h`) live beside the
index in `Document`, which the event-loop thread owns alone; workers return
`Check_result`s computed on a `Snapshot`, and `Document::apply_checks()` carries them
across the edits made since through the splice history (`Document::translate()`), not by a
content hash. The line table is `Line_map`, bytes only, as planned.

### 2.2 Parsing one statement (the worker's job)

**One grammar.** The REST SQL rules are merged by hand into the stubbed server grammar
(§3, `generated/yy_stub.yy`), so libmariadbparser parses both dialects
with the server's own lexer and one automaton. There is no second parser, no prefix
routing and no "try the server grammar, then the REST grammar" loop: a statement is
parsed once, and whether it is a REST statement is a property of its derivation (the
reduction of `mrs_statement`), recorded in the index as `StatementKind::RestSql` after
the parse. The status values stay as the pasted plan lists them, so a consumer can still
tell "REST statement with an error" from "server statement with an error".

Why merged rather than two parsers:

- one token stream and one lexer, the server's, so `/*M!` comments, backslash rules and
  `/*!NNNNNN` gates behave identically in both dialects (the shell's standalone MRS lexer
  leaves `/*!` comments as invalid tokens, for example);
- one expected-token set, so keyword completion after `CREATE` offers `REST` next to
  `TABLE`, and completion inside a REST statement works like everywhere else;
- one tree type and one rule-name walker for the outline;
- it is the same merged grammar a future server-side MRS plugin needs, so the merge
  script is reusable there.

The worker holds one `mdbp_parser` (created with the statement's `sql_mode`, charset
utf8mb4, and the configured release id for `/*!NNNNNN */` comments), reused across
statements. Parsers whose `sql_mode` differs from the statement's are re-created; a small
per-thread cache keyed by mode bits avoids thrashing in mixed files.

A build option `MDBP_WITH_MRS` (default ON) leaves the merge out, which gives the pure
server grammar for comparison and for consumers that do not want REST SQL.

**Built as:** one grammar, merged by `libs/mariadbparser/tools/mrs_merge.py` (§3). The
worker's parsers are a `Parser_cache` keyed by (`sql_mode`, release id, derivation wanted).
`MDBP_WITH_MRS` was not built: bison has no conditional sections, and rules nobody reaches
cost nothing. The index records no REST kind (§2.1); a REST statement is known by its
`mrs_statement` node in the tree, which is how the catalog fold finds REST objects
(`src/catalog_symbols.inc`).

### 2.3 The thread pool

A process-wide `ParserPool`:

- N workers, N = `hardware_concurrency()` by default, configurable
  (`initializationOptions.threads`, `--threads`).
- A priority work queue of `(document, statement index)` jobs. Priorities, highest first:
  (1) the statement at the cursor for a pending completion/hover request (run
  synchronously on the requesting thread instead, §2.5), (2) statements inside the
  editor's visible range (known from the last `semanticTokens/range`, `documentHighlight`
  or a custom `mariadb/viewport` notification), (3) statements whose results feed the
  outline (DDL, detected by first token), (4) everything else in file order.
  This is what makes "jump to line 32015 in a 256 MB dump and see diagnostics almost
  immediately" work: the splitter pass is sequential but cheap (memchr-bound), and the
  parse work is reordered towards the viewport.
- Workers idle for 2 minutes are joined; the next job re-spawns. Thread creation cost is
  irrelevant against a 256 MB file, and a shell embedding the library should not keep
  idle threads.
- A job whose document version is older than the current one is dropped at dequeue.
- Each worker owns its parser objects; nothing is shared except the queue and the
  index entries it writes under the entry's own atomic. Results are published by
  swapping the `result` pointer after the parse completes.
- Diagnostics are published in batches (every ~50 ms or every K statements) while a
  large file is still being parsed, so the editor fills in progressively.

The pool is passive from the library's point of view (`libmariadblanguageserver` owns
it, MariaDB Shell can set N = 0 and drive parsing on its own thread).

**Built as:** `Parser_pool` (`include/mdbls/parser_pool.h`): workers started on demand up
to `max_threads`, where 0 means one per core, not no threads; each worker owns a
`Parser_cache`; idle exit after two minutes as planned. N comes from `--threads` only,
there is no `initializationOptions.threads`. The server uses three priorities
(`k_priority_request`, `k_priority_hot`, `k_priority_cold` in `src/lsp/server.cc`): a
request's own work, then the statements on screen or around the last edit, then the rest
in file order; there is no DDL tier. Nothing is dropped at dequeue: a job runs to the end
on its snapshot, and its results are translated or dropped when `Document::apply_checks()`
applies them. Diagnostics are published every 500 ms (`k_publish_interval`) while a large
document is being checked, not every 50 ms.

### 2.4 From derivation to syntax tree

libmariadbparser returns a derivation, not a tree, and the outline needs a tree. Two
routes; the plan takes (a) and keeps (b) as the fallback.

(a) **Build the tree in the language server from the derivation.** A derivation in reduce
order plus, per rule, the left-hand-side name and the right-hand-side symbol list is a
post-order traversal: walk the reductions, pop `|rhs|` children from a stack (terminals
are taken from the token stream in order), push the new node. Cost is linear in the
derivation; nodes live in a per-statement arena (`std::vector<Node>` with `uint32_t`
child indices). The rule table is not exported today, so this needs one small addition to
libmariadbparser (version 2.3): `mdbp_rule_info(int rule, mdbp_rule *out)` returning the
LHS name, the RHS length and the RHS symbol names/kinds, generated by `gen_tables.py` from
the grammar model that already drives the build. No grammar action changes.

(b) **Build the tree inside the parser.** Since `YYSTYPE` is `int`, `stub.py` could emit
`{ $$ = mdbp_node(ctx, serial, N, $1, ..., $N); }` instead of `{ MDBP_R(serial); }` and
`yylex` could set the token's node id. Also mechanical and regenerable, slightly faster,
but it changes every action. Taken only if (a) proves too slow for the DDL-only tree
building the outline needs, which is unlikely.

What a `ParseResult` holds:

```cpp
struct ParseResult {
    bool accepted;
    Diagnostic error;                    // byte range, message, expected tokens
    std::vector<Token> tokens;           // name, byte span (copied out of the parser's buffers)
    std::vector<int> derivation;         // server statements only
    std::unique_ptr<SyntaxTree> tree;    // built lazily, kept only for DDL and for the cursor statement
};
```

Trees are built on demand and dropped for statements that are neither DDL nor near the
cursor: a 256 MB dump is mostly `INSERT` rows whose trees nobody needs, and re-building one
costs microseconds.

The tree exposes nodes by **grammar rule name** (`create_table_stmt`, `field_spec`,
`table_ident`, `column_def`, `key_def`, ...), which is what the outline walker matches on.
Rule names in `sql_yacc.yy` are stable across releases far more than rule numbers; the
walker is written against names and the regeneration test (§4, P3) lists every name it
depends on so a MariaDB bump that renames one fails loudly.

**Built as:** route (a), without `mdbp_rule_info()` and without `gen_tables.py`.
libmariadbparser 2.3 records the parser's stack depth at every reduction
(`mdbp_derivation_depths()`) and exposes bison's own symbol and rule tables
(`mdbp_grammar_symbol_count()`, `mdbp_grammar_terminal_count()`,
`mdbp_grammar_symbol_name()`, `mdbp_grammar_rule_count()`, `mdbp_grammar_rule_lhs()`,
`mdbp_grammar_rule_length()`, `mdbp_grammar_token_symbol()`; `include/mdbparse.h`), which
give the exact tree, mid-rule actions included, with no grammar change.
`Syntax_tree::build()` (`include/mdbls/syntax_tree.h`) builds it on demand from a parser
that still holds its result. There is no `ParseResult`: the per-statement result kept is
`Statement_check{status, error_offset, error_length, error_token, expected}`; tokens and
derivations are not stored, and a consumer that needs the tree parses the statement again.

### 2.5 Completion and the incomplete-statement loop

Implemented exactly as the pasted plan describes, with these bindings to what we have:

1. **Statement at the cursor** comes from the index (binary search on offsets). The
   statement is re-parsed synchronously on the request thread with its own parser; the
   pool is not involved.
2. **Sentinel insertion**: splice the text ` mdb_cursor_ ` into a copy of the statement at
   the cursor byte (text-level splicing; the lexer turns it into `IDENT`). On accept, find
   the token whose text is the sentinel, walk up the tree (§2.4) to the enclosing
   `select_item`/`table_ref`/`expr` node and the enclosing query block, and collect table
   references and aliases from that block's `FROM`/`JOIN` subtree. Qualified (`a.|`)
   works for free: the sentinel sits in a qualified-identifier node whose qualifier is `a`.
   A partially typed word (`a.fi|`) is found with `mdbp_lex_sql_prefix()` and replaced;
   its text becomes the filter prefix.
3. **Fallback attempts**, in the order of the pasted plan: replace the token under the
   cursor; insert the sentinel and truncate after the cursor; token-level scope scanner
   over `FROM`/`JOIN`/`AS` (no parse needed).
4. **Keyword completion** needs no `parse.error custom` and no push parser: parse the text
   up to the cursor; the result's `expected` array **is** `yypcontext_expected_tokens()`
   computed with LAC, already shipped by libmariadbparser 2.x. Terminal names
   (`SELECT_SYM`, `'('`) map to spellings through MariaDB's own keyword table, which the
   library already carries; punctuation and `IDENT`-class terminals are filtered out.
5. **Identifier candidates** come from the catalog (§2.7): schemas, tables of the current
   schema, columns of the tables in scope, routines, plus the live-connection catalog when
   configured.

The same walk-up serves hover (object at offset → catalog entry), go-to-definition
(→ the `CREATE` statement's range in the index, in this or another workspace file),
and document highlight.

**Built as (P5):** `complete()` (`include/mdbls/completion.h`) over `Statement_names`
(`include/mdbls/sql_names.h`), which classifies a name token by the grammar rules above
it (`table_ident`, `simple_ident`, `field_ident`, `key_part_simple`, `insert_ident`,
`sp_name`, `call`, `use`, ...) and collects the tables in scope from the enclosing
`query_specification`, `update`, `delete` and `insert` nodes, derived tables and CTEs
included. The fallbacks are as planned, in order: the sentinel with the rest of the
statement, the sentinel with the statement cut after it, then the partial tree and the
tokens before the cursor (`classify_by_tokens`, `scanned_tables`); "replace the token
under the cursor" is the first step, since the sentinel replaces the whole word. The
partial word is found by scanning identifier bytes back from the cursor, not with
`mdbp_lex_sql_prefix()`, so a word in backticks completes too. Keyword spellings come
from MariaDB's keyword table, which libmariadbparser 2.4 exports
(`mdbp_keyword_count()`, `mdbp_keyword_at()`). Against the plan: the expected set of a
prefix that is already a complete statement comes from `mdbp_parse_tokens()` with the
prefix's tokens and an `ABORT_SYM` appended; and a non-reserved keyword where a name may
stand is offered only when the parser's expected set after it differs from that after a
name, which costs one parse per keyword, so it is done only for the keywords matching
the typed letters, cached per prefix, and skipped (list marked incomplete) when nothing
is typed. Hover, definition, highlight, references and rename go through
`Name_resolver` (`include/mdbls/navigation.h`). See `docs/language-server.md`,
"Completion".

### 2.6 Keeping the index current while typing

Incremental document sync (`TextDocumentSyncKind.Incremental`): the client sends ranges
and replacement text; the server keeps the document in a **piece table / gap buffer** so
that a mid-file edit in a 256 MB document does not memmove 128 MB. For each change:

1. Convert the LSP range (line, UTF-16 column) to byte offsets with the line table.
2. Apply the edit; delta = new length − old length.
3. Find the first statement overlapping the edited range (binary search); re-run the
   splitter from that statement's start, seeded with the splitter context, delimiter and
   `sql_mode` recorded at that boundary, until it produces a boundary equal to an old
   boundary shifted by delta **with the same splitter state** (delimiter, mode, context
   stack empty). Everything after that point is kept and its offsets shifted by delta;
   the shift is applied lazily through a per-index "offset adjustment" tree so the cost
   is O(log n) per edit rather than O(n), and materialised when the index is read in bulk.
4. Statements between the two points are replaced; those whose `content_hash` is
   unchanged keep their `ParseResult`, the rest become `Unparsed` and are queued (the
   cursor's statement first).
5. Rebuild the line table for the edited region only (line starts after the edit shift by
   the line delta).

Resynchronisation can fail to land: an unclosed quote or `DELIMITER` with no terminator
re-splits to end of file. That region is bounded by a configurable statement count after
which the remaining tail is marked `Unterminated` with one diagnostic, so typing an
opening quote in a 256 MB file costs a bounded amount.

A debounce timer per document (default 150 ms, 0 in tests) collapses keystroke bursts
before diagnostics are published; completion requests bypass it.

**Built as:** `Document` (`include/mdbls/document.h`, `src/document.cc`) over
`Statement_index::update()` and `Line_map::update()`. The text is a `std::string` changed
in place unless a snapshot still shares it (then it is copied first), not a piece table;
the measured edit cost (P4) left no reason for one. The kept statements after the
resynchronisation point are shifted eagerly in `Statement_index::update()`, not through a
lazy offset tree. Verdicts follow their statements through the splice history (§2.1), not
by content hash. The bounded give-up region is not built: an edit that opens a string or
comment re-splits to the end of the file, which on the 256 MB dumps took 5 ms and 24 ms
because the splitter resynchronises at the next quote or `*/`. The debounce is
`initializationOptions.debounceMs`, default 150 ms, as planned.

### 2.7 The object model and outline

`Catalog`: schemas → tables (columns with type, nullability, default, charset/collation,
generated expressions; indexes; primary/unique/foreign keys; table options), views,
triggers, routines (parameters, return type), events, sequences, users/roles (names
only), plus REST objects from the merged grammar's rules (services, schemas, views,
procedures, content sets, auth apps, roles, users), walked by rule name like the rest. Every object records the statement index that defined it
and the byte range of its name token, so the outline links back to source and
go-to-definition works.

It is produced by folding DDL statements in file order: `CREATE` adds, `ALTER` mutates,
`DROP` removes, `RENAME` moves, `USE` sets the current schema. A workspace catalog folds
several files in a configured order; the open document's own DDL overrides it; a live
connection snapshot (opt-in, background only, read-only session) overrides both.

Outputs:

- `textDocument/documentSymbol` (nested: schema → table → columns/keys).
- A JSON document on request (`mariadb/outline` custom request and the CLI's `outline`
  subcommand): the full object model with positions. Its schema is versioned
  (`"format": 1`) because the modeler and the ALTER generator will consume it.

The model is designed so that `diff(Catalog a, Catalog b)` is a later, separate module
that emits `ALTER` statements, and so the EER modeler can read the JSON; neither is built
in this plan.

**Built as:** `Catalog` (`include/mdbls/catalog.h`) with `Schema`, `Table`, `Column`,
`Index`, `Foreign_key`, `Check`, `View`, `Routine`, `Trigger`, `Named_object`,
`Rest_service`, `Rest_schema` and `Rest_object`, each carrying an `Origin`; `Outline` and
the JSON writer in `outline.h` (`docs/outline-format.md`). Server-level users and roles
are not folded; of the name-only objects only the REST ones are (`rest_auth_apps()`,
`rest_roles()`, `rest_users()`). The workspace catalog and the live-connection snapshot
are P5.

**Built as (P5):** `Catalog_view` (`include/mdbls/catalog_view.h`) looks names up in
layers, the document's own DDL first, then the workspace catalog, then the connection's
snapshot, as planned. The workspace catalog (`include/mdbls/workspace_catalog.h`) folds
the files of `initializationOptions.catalogFiles` (a directory: its `*.sql` files sorted
by path) in the order given into one `Outline` (`fold_outline()`); an `Origin` now
records the file it came from (`source`). The live snapshot is built from
`information_schema` by `fetch_live_catalog()` (`include/mdbls/live_catalog.h`,
`Catalog::from_schemas()`).

### 2.8 LSP features in scope

In the order they ship (§4): diagnostics; document symbols and the outline request;
folding ranges (one per statement, plus `BEGIN...END`); selection ranges; semantic tokens
(`full`, `full/delta` and `range`, lexer-based with catalog classification of identifiers;
`range` is answered first for large files); completion; hover; go-to-definition and
document highlight for tables/columns/aliases; references within the document; rename of
aliases and, within the document, of objects defined there; signature help for built-in
functions (from the function list the extension's grammar tool already extracts);
workspace symbols over the workspace catalog. Formatting was added later as P8.

Custom messages: `mariadb/documentContext` (client → server: connection URI, `sql_mode`,
current schema, server version for a document), `mariadb/viewport` (client → server:
visible line range, used for scheduling only), `mariadb/outline` (request),
`mariadb/refreshCatalog` (request), `mariadb/parseProgress` (server → client, for a
status bar item on large files).

**Built as (P4):** diagnostics, document symbols, folding ranges, selection ranges and
semantic tokens (`range`, `full`, `full/delta`) are served (`docs/language-server.md`).
Semantic tokens do not classify identifiers yet; that needs the P5 catalog. Of the custom
messages, `mariadb/documentContext`, `mariadb/viewport`, `mariadb/outline` and
`mariadb/parseProgress` exist; `mariadb/refreshCatalog` is P5, with completion and the rest
of the list.

**Built as (P5):** completion, hover, definition, document highlight, references (document
scope), prepareRename and rename (aliases, CTEs, variables, and the tables, views, columns
and routines the document defines; document scope), signature help (built-in functions
from the server's help tables, stored functions and procedures) and workspace symbols are
served, and `mariadb/refreshCatalog`. Signature help comes from a built-in function table
that `scripts/gen_builtin_functions.py` writes once from the server tree
(`src/builtin_functions.inc`: names from the function registries, call forms and summaries
from `scripts/fill_help_tables.sql`), not from the extension's grammar tool, which has
names only. Semantic tokens still do not classify identifiers.

### 2.9 The command-line interface

One binary, `mariadb-lsp`, with subcommands (the LSP mode is explicit, so a bare run
prints help):

```text
mariadb-lsp lsp [--stdio] [--threads N] [--log FILE] [--shell-bin-dir DIR]
mariadb-lsp check   [--sql-mode M] [--server-version V] [--threads N] [--max-errors K]
                    [--connection URI] [--format text|json] FILE...
mariadb-lsp outline [--sql-mode M] [--server-version V] [--connection URI]
                    [--format json] FILE...
mariadb-lsp connections                               # URIs found in the secret store
mariadb-lsp split   [--format json|tsv] FILE          # the statement index, for debugging
mariadb-lsp tokens  [--sql-mode M] FILE               # lexer dump
mariadb-lsp bench   FILE                              # index + parse timings and RSS
```

`check` prints `success` (exit 0) when every statement parses, otherwise one line per
error `FILE:LINE:COL: error: <message>; expected: <tokens>` (exit 1), or the same as a
JSON array with `--format json`. Exit 2 means the input could not be read or the options
were refused (unsupported `sql_mode`, charset, version). `outline` writes the §2.7 JSON to
stdout. With `--connection URI` both read `sql_mode` and the server version from that
server and check unknown tables and columns against its catalog (§2.11); without it the
catalog is folded from the files only. All subcommands share the library; the CLI is
also the harness the end-to-end tests and benchmarks drive.

**Built as** (`src/mariadb-lsp/main.cc`, `mariadb-lsp --help`):

```text
mariadb-lsp lsp     [--stdio] [--threads N] [--log FILE]
mariadb-lsp check   [--threads N] [--max-errors N] [--format tsv|json] FILE|-...
mariadb-lsp outline FILE|-...
mariadb-lsp split   [--format tsv|json] [--text] FILE|-...
mariadb-lsp tokens  FILE|-...
mariadb-lsp tree    FILE|-...
```

`--sql-mode LIST`, `--delimiter D`, `--charset utf8mb4|latin1`, `--version-id N` and
`--stats` apply to every command. Against the plan: `--server-version V` is
`--version-id N` (`major*10000 + minor*100 + patch`, default 130100); `tree` was added;
the text format is called `tsv`; `check` prints nothing on success rather than `success`;
`bench` was not built, `--stats` prints sizes and timings to stderr instead;
`--connection`, `connections` and `--shell-bin-dir` are P5 with §2.11. Exit codes 0, 1
and 2 are as planned.

**Built as (P5):** `connections [--format json]`, `--connection URI` on `check`, `outline`
and `complete`, and `--shell-bin-dir DIR` (also on `lsp`), as planned. With
`--connection`, `check` reports the tables and columns the server and the file do not
define as warnings (`FILE:LINE:COL: warning: unknown column ...`), which do not change
the exit code. `complete --at LINE:COL FILE` was added: the completion candidates at a
position, for debugging.

### 2.10 Library boundary

`libmariadblanguageserver` (C++23 inside, C API outside, as the shell will embed it):
documents, index, pool, catalog, completion, diagnostics, JSON outline, live-connection
catalog. The LSP transport (JSON-RPC framing, method dispatch) and the CLI live in the
executable, not in the library. JSON goes through RapidJSON, which the shell already uses
from vcpkg; the only other runtime dependency is the MariaDB client library (§2.11, §3).

**Built as:** `libmariadblanguageserver` is C++ only so far; the C API for the shell comes
later (`libs/mariadblanguageserver/CMakeLists.txt`). The LSP transport and dispatch live in
the library, not in the executable: `src/lsp/transport.cc`, `src/lsp/server.cc`,
`src/lsp/features.cc` with the headers in `include/mdbls/lsp/`; `src/mariadb-lsp/main.cc`
holds the CLI and starts the server. RapidJSON is a public dependency of the library
(`include/mdbls/lsp/json_position.h` writes with it). The client library is built and
linked as `mariadb::client` (`cmake/mariadb_client.cmake`); since P5 the library links it
for live connections (`src/live_catalog.cc`; `-DWITH_MARIADB_CLIENT=OFF` builds without,
and connections then report that).

### 2.11 Live connections: URIs and the shell's secret store

Suggestions from a live database (schemas, tables, columns, routines not defined in the
open files) use the same connection model as the MCP server, and no password ever travels
through the editor or the LSP channel:

- **A connection is named by its URI**, in the shell's own syntax
  `[scheme://]user@host[:port][/schema][?options]`, scheme `mariadb` by default, SSH
  tunnels as `mariadb+ssh://...?ssh-host=...`. The extension already keeps one URI per
  document (`SqlEditorBinding.connectionFor`, the `-- MariaDB connection: <uri>` header,
  `mariadb.defaultConnection`); it sends that URI in `mariadb/documentContext`. The CLI
  takes `--connection <uri>`. URIs are normalised exactly as the MCP plugin's
  `normalize_connection_uri` does (password dropped, host lower-cased, port 3306 added
  for TCP schemes) so that lookups match the stored keys.
- **Passwords are read from MariaDB Shell's secret store, directly.** The language
  server implements the shell's helper protocol itself (it is small, and the MCP plugin
  only reaches it through the shell's `shell.read_secret()`, which we cannot call from
  C++ without linking the shell). As the shell does it
  (`mysqlshdk/shellcore/credential_manager.cc`, `mysqlshdk/libs/secret-store-api/`):
  1. Helper name: `MARIADB_SHELL_CREDENTIAL_STORE_HELPER` environment variable, else the
     `credentialStore.helper` option in `<MARIADB_SHELL_USER_CONFIG_HOME>/options.json`,
     else `default`, which means `windows-credential` on Windows, `keychain` on macOS and
     `login-path` elsewhere; `<disabled>` means no store.
  2. Helper location: the executable `mariadb-secret-store-<helper>` in the **shell's
     binary directory**. The server finds it next to `mariadb-shell` (the extension
     passes the resolved shell path in `initializationOptions.shellBinary`, the CLI
     takes `--shell-bin-dir`, else `mariadb-shell` on PATH).
  3. Exchange: run `<helper> get` with a JSON object on stdin, read the answer on
     stdout, exit code 0 on success:

     ```json
     {"SecretType": "<type>", "SecretID": "<id>"}
     → {"SecretType": "<type>", "SecretID": "<id>", "Secret": "<value>"}
     ```

     `list` takes an optional `{"SecretType": "<type>"}` filter and returns an array of
     `{SecretType, SecretID}`; `version` takes nothing.
- **Two kinds of stored connection, looked up in this order:**
  - *MCP and GUI connections* (the ones the MCP server and the VS Code extension
    manage): `SecretType` is `generic` (or a tenant group UUID in multi-tenant mode),
    `SecretID` is `GUI:CONN:<uri>` then `MCP:CONN:<uri>` (legacy prefixes
    `GUI:Connection:` and `MCP:Connection:` are accepted on read). The returned value
    carries a one-byte marker the shell adds: `r` followed by the raw password, or `b`
    followed by its base64 encoding.
  - *Regular shell connections* (stored by `shell.store_credential()` when the user
    connects in the shell): `SecretType` is `password`, `SecretID` is the connection's
    URL in the shell's `user_transport` form, `user@host[:port]`, `user@/path` for a
    socket, with a scheme only for `file`/`ssh`. The value is the password itself.
  - A URI that matches neither has no password; the server reports "connection not
    configured" once and the live catalog stays off for that document.
- **Opening the session** uses the client library (`mysql_real_connect`) with the URI's
  user, host, port, socket and schema, SSL preferred, 5 s connect and 30 s read timeouts,
  the first statement `SET SESSION TRANSACTION READ ONLY`, then `SELECT @@sql_mode,
  VERSION()` so the document's mode and release id follow the server. SSH-tunnel URIs
  are not supported in the first version (reported as such); they need the shell.
- **Fetching the catalog** runs only on a background worker: `information_schema`
  queries for schemata, tables, columns, statistics, routines, parameters, triggers,
  views, events, in one pass per refresh, with backoff on failure. No LSP request and
  no completion ever waits on the database; candidates come from the last snapshot.
  Refresh on `initialize`, on `mariadb/documentContext` naming a new URI, on an explicit
  `mariadb/refreshCatalog` request, and at a configurable interval.
- Passwords are held in memory only as long as `mysql_real_connect` needs them, never
  logged, never written to diagnostics; the helper's stdout is scrubbed from logs the
  way the shell's `hide_secret` does.

**Built as (P5):** as described, in `include/mdbls/connection_uri.h` (parsing and the MCP
plugin's normalisation, its test cases ported to `unittest/mariadblanguageserver/
connection_t.cc`), `include/mdbls/secret_store.h` (helper selection, `get` and `list`,
the lookup order GUI, MCP, then the shell's `password` secrets; tested against
`mariadb-secret-store-plaintext`) and `include/mdbls/live_catalog.h` (one thread per
connection; TLS as rest-daemon's `MySQLSession` sets it up). Against the plan: the
connection is not kept open between refreshes; a refresh connects, reads and
disconnects. The secret type is always `generic` (no tenant groups). The refresh interval
defaults to off (`catalogRefreshSeconds`). The server's `sql_mode` and version apply to a
document unless its `mariadb/documentContext` sets them. Errors are shown once per
connection and message with `window/showMessage`.

---

## 3. Repository layout and build

This section describes the tree as it is (rewritten 2026-10-17; the original listed
modules and scripts that were never copied or written). The README's layout table is the
short form of the same tree.

```text
CMakeLists.txt                 top-level, shaped like mariadb-shell's: reads VERSION, bootstraps
                               vcpkg, pins and builds the client library, adds the four
                               subdirectories below
vcpkg.json                     openssl (feature "tools"), zlib, rapidjson; feature "tests":
                               gtest; version overrides for openssl, zlib and gtest
cmake/                         copied from mariadb-shell: bootstrap_vcpkg, bootstrap_mariadb,
                               bison; written here: compiler, mariadb_client, mariadb_pin
libs/mariadbparser/            include/mdbparse.h (2.3), src/, generated/ (yy_stub.yy with the
                               REST SQL rules merged in, lexer_extracted.cc, keywords.h,
                               lexstates.h, state_maps.h, ctype-utf8.h; committed), tools/
                               (stub.py, lexslice.py, mrs_merge.py, common.py),
                               tests/lex-fixtures/ (nine .jsonl files), docs/thd.md, README.md,
                               UPSTREAM.md
libs/sqlsplitter/              Sql_splitter from mariadb-shell under its own include paths,
                               maintained by hand, UPSTREAM.md
libs/mariadblanguageserver/    the engine library, C++ only (C API later): include/mdbls/ and
                               src/ with Statement_index, Line_map, Sql_mode_tracker,
                               Parser_pool, Statement_check, Syntax_tree, Catalog, Outline,
                               Document, semantic tokens, charset detection; the LSP server
                               in src/lsp/ (transport, server, features) and include/mdbls/lsp/
src/mariadb-lsp/               the executable: main.cc with the CLI subcommands, and the
                               ctest smoke tests of each subcommand
unittest/                      gtest, one run_unit_tests binary: mariadbparser/, sqlsplitter/,
                               mariadblanguageserver/, client/
tests/e2e/                     lsp_client.py and test_lsp.py: Python end-to-end tests
                               speaking LSP over stdio, run by ctest
tests/fixtures/                sql/ (sakila schema, world, a mysqldump file, the shell's REST
                               grammar test, a latin1 file), outline/ (sakila golden JSON);
                               large dumps are generated, not committed
scripts/                       gen_fixture_dump.py
docs/                          language-server.md, outline-format.md, plans/
.github/workflows/             ci.yml (Linux, tests), devtools-ci-start.yml (unused)
.github/vcpkg-triplets/        x64-windows.cmake, an overlay triplet for Windows builds
```

Build conventions, copied from the shell where the shell has them:

- `cmake -S . -B bld -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>`
  bootstraps vcpkg into `../vcpkg` (or uses `VCPKG_ROOT`) and installs the manifest;
  `-DWITH_TESTS=0` drops the `tests` feature and the `unittest/` directory. CMake 3.22,
  Ninja, git and a C++23 compiler are needed.
- **The client library comes from the server checkout, as the shell does it.** The server
  commit is pinned in one place, the `| MariaDB server` row of
  `libs/mariadbparser/UPSTREAM.md`, which `cmake/mariadb_pin.cmake` reads (editing the
  file re-runs configure); `-DMARIADB_GIT_TAG` overrides it with a warning when the two
  differ. `cmake/bootstrap_mariadb.cmake`, copied from the shell, clones the server into
  `../mariadb-server-lsp` (`-DMARIADB_BOOTSTRAP_DIR`), initialises `libmariadb` and builds
  `mariadbclient mysys mysys_ssl caching_sha2_password GenError`; an existing build is
  reused with `-DMARIADB_SOURCE_DIR`/`-DMARIADB_BUILD_DIR` (its commit is checked against
  the pin, a mismatch warns). `cmake/mariadb_client.cmake` exposes the result as the
  interface target `mariadb::client`: `libmariadbclient` plus the server's `mysys`,
  `mysys_ssl`, `strings` and `dbug`, linked statically, headers from
  `<src>/libmariadb/include`, `<bld>/libmariadb/include`, `<src>/include` and
  `<bld>/include`, `MARIADB_CAPI_RENAME_INCLUDED` pre-defined as the shell does, OpenSSL
  and zlib from vcpkg. No vcpkg `libmariadb`. `-DWITH_MARIADB_CLIENT=OFF` builds without
  the client library and without `unittest/client/`.
- **libmariadbparser builds from `generated/`**: `bison -k -d -Dparse.lac=full` over
  `generated/yy_stub.yy`, the result `#include`d by `src/parser_glue.c`; the token names
  come from bison's `yytname[]` and `yytranslate[]` at run time and the rule numbers from
  `yyn` inside each action, so there is no generated table and no XML report. Bison 3.7.4
  or newer is required (Rocky Linux 9's; its tables equal 3.8.2's but for the unused
  `yydefgoto[0]`) (`libs/mariadbparser/CMakeLists.txt`; the shell's
  `cmake/bison.cmake` finds Homebrew bison on macOS and `win_bison` on Windows). No
  Python, no server tree, no provenance gate at build time. The library is built static
  and shared (`libmariadbparser.so.2`, version script on Linux, version read from
  `mdbparse.h`), plus the `mdbparse` CLI when RapidJSON is found.
- `cmake/compiler.cmake`: C++23 (`-std=c++23`; `/std:c++23preview` on MSVC in place of
  CMake's `/std:c++latest`) and the shell's warning set (`-Werror -Wall -Wextra
  -Wpedantic -Wshadow ...`, `/W4 /WX` on MSVC), applied per target through
  `mdbls_target_warnings()` rather than globally, because the extracted lexer in
  `libs/mariadbparser` is compiled as written; the library's own C files are held to
  `-Werror` separately. RapidJSON's SIMD scanning (`RAPIDJSON_NEON` / `RAPIDJSON_SSE2`) is
  defined for every target.
- `mysql_library_init()` once in `main()` and an RAII `mysql_thread_init()` token per
  worker that opens a session: planned, with the live catalog (P5). Today only
  `unittest/client/` calls the client library.
- Packaging (`cpack`, install into mariadb-shell's `bin/`, §5 decision 4): planned (P7).
  The shell's git hooks, `scripts/run_unit_tests.py` and its static-analysis module were
  not copied either.
- **The REST SQL merge was done once by `libs/mariadbparser/tools/mrs_merge.py`** on
  2026-10-15, from the shell's `mysqlshdk/libs/mrs/parser.yy` at the commit pinned in
  `UPSTREAM.md`, and the result is maintained by hand like the rest of `generated/`: the
  merge part's 199 rules were translated and appended to `yy_stub.yy`, `mrs_statement`
  hooked into `verb_clause` and the 27 REST `SHOW` statements into `show_param`; the 61
  new keywords are all non-reserved, 59 in `keyword_sp_var_and_label` and `CLONE` and
  `CONFIGURE` in `keyword_verb_clause`, and appended to `generated/keywords.h`;
  annotations are `'@'` and a word; there was no new conflict, so `%expect 72` is
  unchanged. A later change that adds a conflict has to resolve it, not raise the count
  (`UPSTREAM.md`). To take a newer shell grammar, patch the merged block by hand or remove
  it and run the tool again. The shell's lexer, AST and driver are not vendored.
- Vendored code is maintained by hand, not synchronised by a script: `libs/sqlsplitter/UPSTREAM.md`
  and `libs/mariadbparser/UPSTREAM.md` each hold a pin table and the list of deviations,
  those in the splitter marked `lsp-server:` in the source. There is no
  `sync_from_shell.py`, no `PROVENANCE.json` and no CI drift check; an upstream change is
  taken by diffing the shell between pins and applying the hunks.
- Tests: `unittest/CMakeLists.txt` globs `*/*_t.cc` into `run_unit_tests`
  (`gtest_discover_tests`, fixture directories passed as compile definitions);
  `src/mariadb-lsp/CMakeLists.txt` adds a ctest smoke test per subcommand
  (`mariadb-lsp.split`, `.check-*`, `.outline`, `.tree`, ...); `tests/e2e` runs as
  `mariadb-lsp.e2e` on a generated 16 MB dump (Python 3, standard library only), and
  `-DMDBLS_LARGE_TESTS=ON` adds `mariadb-lsp.e2e-large` at 256 MB.
- CI (`.github/workflows/ci.yml`, on pull requests and by hand), modelled on MariaDB
  Shell's `shell-ci.yml`: one Linux job on a GitHub-hosted runner (this repository has no
  access to the shell's self-hosted devtools runner; `devtools-ci-start.yml`, which starts
  it, is kept for when it does), `x64-linux-dynamic`, RelWithDebInfo, ccache, then every
  ctest test; a failing test, or none registered, fails the job. ccache, vcpkg's binary
  cache and the server bootstrap are kept with `actions/cache`. A PR titled `[bypass-ci]`
  builds nothing.

---

## 4. Phases

Each phase was to end with its tests green in CI on Linux and macOS (Windows from P4), and
a short record of what was measured. As built: CI (`.github/workflows/ci.yml`) has not run
yet, so every "done" below rests on local runs on this macOS machine; there is no
`docs/plans/records/` directory, the status paragraphs here and the "Measured" table in
`docs/language-server.md` hold the record. Numbers below marked *target* are to be
measured, not assumed.

### P0 — Build foundation (1–2 weeks)

**Status 2026-10-15: done on macOS, except the items marked open.** The extraction ran
once and `generated/` is committed; the build needs bison and a compiler only; the token
tables and rule numbers come from bison's own output at run time, so `gen_tables.py` is
gone; the output matches the original build on all 212,332 recorded token streams; the
client library is built from the pinned server tree as in the shell; the unit tests pass,
the parser tests also under ThreadSanitizer. Open: a Linux run in CI (workflow written,
not yet run), Windows (the lexer uses POSIX threads and the CLI POSIX I/O), the shell's
git hooks (they enforce copyright headers the imported parser sources lack),
`scripts/run_unit_tests.py` (ctest is used for now), and cpack packaging (P7).


- Copy the shell's CMake modules and `vcpkg.json` shape; top-level `CMakeLists.txt`,
  `unittest/` with `run_unit_tests`, `scripts/run_unit_tests.py`, CI workflow;
  `bootstrap_mariadb.cmake` at the parser's pin, the client library linked from it.
- **The one extraction run**: on a Linux host with the server tree at the pin, run
  `stub.py`, bison, `lexslice.py` and `gen_tables.py` (its grammar-model cross-check
  removed) by hand, commit the output as `libs/mariadbparser/generated/`, record the
  run in `UPSTREAM.md`. From then on the tools are not needed to build. **Built as:**
  the run was on macOS (arm64, bison 3.8.2), with the server's own `gen_yy_files.cmake`
  preprocessor, `stub.py`, bison and `lexslice.py`; `gen_tables.py` is gone, the tables
  come from bison's output at run time (§2.4, §3); `UPSTREAM.md` records the steps.
- libmariadbparser's CMake: bison over `generated/yy_stub.yy`, the library and the
  `mdbparse` CLI, on macOS, Linux and Windows, with no Python and no server tree.
- gtests for the parser library: parse, lex, expected set, and the nine lexer fixture
  files under `tests/lex-fixtures/` replayed through `mdbp_lex_sql()`.
- Done when: `cmake --build` and `ctest` pass on macOS (this machine) and on the Linux
  CI runner.

### P1 — Splitter and statement index (1 week)

**Status 2026-10-15: done on macOS.** `libs/sqlsplitter` holds the shell's splitter
(commit `6c0d7a9`, maintained by hand, `UPSTREAM.md`), its tests pass. Two upstream
bugs were fixed here and should go back to the shell: a `$` inside a versioned comment
left the comment open (two statements merged), and the delimiter test ran `strlen()` to
the end of the buffer at every `;` (quadratic on a whole file; hidden in the shell by
64 KB chunks). `libs/mariadblanguageserver` has `Line_map`, the `sql_mode` tracker
(follows mysqldump's save/restore) and `Statement_index`; it matches the shell's own
chunked driver on the sakila schema, `world.sql` and a mysqldump file. `mariadb-lsp
split` and `tokens` exist. Measured on this machine (Apple M-series, macOS) with
`scripts/gen_fixture_dump.py`: indexing a 256 MB dump takes 1.2 s with extended
INSERTs (8,112 statements) and 1.2 s with one row per INSERT (2.29 M statements); peak
memory is 17 MB and 120 MB above the file's 256 MB. `tokens` over the 256 MB dump
takes 5.4 s. Not done: a separate differential run against a `mariadb-shell` binary
(the comparison uses the shell's code linked here instead).


- `libs/sqlsplitter`: vendored `Sql_splitter`, dependencies cut, its 1,829-line gtest
  suite ported.
- `StatementIndex` (§2.1): streaming build from a file or buffer, line table,
  offset→line/UTF-16 column module, `sql_mode` tracking.
- CLI skeleton with `split` and `tokens`.
- Done when: the index of a generated 256 MB dump builds in *target* under 2 s on this
  machine and under 400 MB RSS beyond the file itself; chunk-boundary tests pass at the
  shell's chunk sizes; the sakila schema and a set of mysql-test files split to the same
  statements the shell's own splitter produces (differential test against
  `mariadb-shell` when available).

### P2 — REST SQL merge, parallel parsing pipeline and `check` (2–3 weeks)

**Status 2026-10-15: the REST SQL merge is done** (`libs/mariadbparser/UPSTREAM.md`, "The
REST SQL merge"): no new conflicts, the shell's 233-statement grammar test parses, server
verdicts unchanged over 212,332 recorded statements. The `MDBP_WITH_MRS` switch was not
built: bison has no conditional sections, and the merged rules cost nothing when unused.

**Status 2026-10-15: P2 is done on macOS.** `Parser_pool` (on-demand workers up to one per
core, priorities, idle exit after 2 minutes, one parser cache per worker) and
`check_statements()` (jobs of about 1 MB of statements, one result slot per statement)
are in `libs/mariadblanguageserver`; `mariadb-lsp check FILE...` prints
`FILE:LINE:COL: error: ...` with the expected terminals in readable form, notes statements
it cannot check (ORACLE/MSSQL mode, invalid UTF-8), exits 0 or 1, and has `--format json`.
Measured on this machine with 8 threads: a 256 MB dump checks in 2.2 s wall time with
extended INSERTs and 2.7 s with one row per INSERT (2.29 M statements), of which the
parallel check is 0.9 s and 1.3 s; peak memory 278 MB and 531 MB. Two errors planted at
lines 1,000,000 and 2,000,000 are reported at their positions. ThreadSanitizer reports
no race in the pool, checker and multi-thread tests or in a `check` run on 8 threads (no
planted-race control was run). Done in P4: the viewport priority (`mariadb/viewport`, the
hot range) and the handling of results from stale snapshots, which are translated through
the edits since or dropped (`Document::translate()`).

- **Merge spike first (2–3 days):** merge the `MERGE PART` rules into
  `generated/yy_stub.yy` by hand to the point where bison accepts the grammar; the
  deliverable is the conflict count and the list of conflicts with the construct behind
  each. The known candidates are `USE REST ...`
  against `USE <schema>`, and `GRANT REST ...` / `REVOKE REST ...` against a role named
  `rest`, where LALR(1) has to decide on `REST` before seeing the next word. The
  expected resolution is to prefer the shift (REST is read as the keyword after USE,
  GRANT and REVOKE; a schema or role literally named `rest` needs backticks there),
  recorded in `UPSTREAM.md`. If the spike finds conflicts that cannot be resolved that
  way, the fallback is the shell's standalone MRS parser as a second library, and §2.2
  is rewritten; that is decided by the spike, not now.
- Finish the merge: token renames, new keywords into the keyword lists and
  `generated/keywords.h`, request-path and annotation rules, `MDBP_WITH_MRS` (a bison
  `%ifdef` around the merged rules, as the server's own `%ifdef MARIADB` works; not
  built, the server's `%ifdef` is its own preprocessor and the unused rules cost
  nothing), the shell's `unittest/data/mrs/grammar_test.sql` as a corpus that must
  parse, and a test that a set of server-only statements gets the same verdicts with
  the merge on and off (built as the replay of the recorded server token streams
  before and after the merge, `UPSTREAM.md`).
- `ParserPool` (§2.3) with priorities, idle join, version-stale dropping; per-thread
  parser cache keyed by `sql_mode`. Built as `Parser_pool` and `Parser_cache`; stale
  results are handled when applied, in `Document` (P4).
- `check` subcommand, text and JSON output, exit codes.
- Thread-safety evidence: TSan build of the pool over a statement corpus with a planted
  race as control, N-thread differential against the single-threaded run.
- Done when: `check` over the sakila schema, the mysql-test SQL files and the shell's
  REST grammar test file reports the expected verdicts (golden files) with one parser; a
  256 MB dump checks end-to-end in *target* under 10 s on 8 cores; TSan clean.

### P3 — Syntax tree, object model, `outline` (2–3 weeks)

**Status 2026-10-15: done on macOS.** Instead of a rule-info table generated at build
time, libmariadbparser 2.3 records the parser's stack depth at every reduction
(`mdbp_derivation_depths()`) and exposes bison's own rule and symbol tables
(`mdbp_grammar_*()`), which give the exact tree with no grammar change. `Syntax_tree`
builds it; `Catalog` folds CREATE/ALTER/DROP/RENAME/USE (tables with columns, indexes,
foreign keys, checks and options; views, routines, triggers, events, sequences; REST
services, schemas, objects, content sets, auth apps, roles, users); `mariadb-lsp outline`
writes it as JSON format 1 (`docs/outline-format.md`), `mariadb-lsp tree` prints trees.
The sakila schema folds completely (35 of 35 DDL statements) and is a golden test; a test
fails when a rule name the fold uses leaves the grammar. On the 256 MB dumps the outline
phase takes 4 ms and 38 ms on 8 threads (only DDL is parsed), against 1.2 s for indexing.
Done in P4: `textDocument/documentSymbol`. Not done: a JSON Schema file for the format,
and reading objects out of statements with a syntax error.

- libmariadbparser 2.3: `mdbp_rule_info()` generated from the grammar model; symbol
  version `MDBPARSER_2.3`. Built as `mdbp_derivation_depths()` and the
  `mdbp_grammar_*()` tables (§2.4); the symbol version is `MDBPARSER_2.3` as planned.
- Tree builder (§2.4a), arena nodes, lookup by rule name, token spans.
- Catalog fold for the DDL in §2.7, including `ALTER TABLE` sub-commands and REST objects;
  JSON outline writer with a versioned schema and a JSON Schema file under `docs/`.
- `outline` subcommand; `textDocument/documentSymbol` data prepared.
- Done when: the sakila schema and a `CREATE TABLE` corpus from mysql-test fold without
  errors into the expected model (golden JSON fixtures); every rule name the walker uses
  is listed in one test that fails if the grammar model lacks it; tree building for all
  DDL in a 256 MB dump adds *target* under 10% to `check`'s time.

### P4 — The language server (3 weeks)

**Status 2026-10-15: done on macOS, except the items marked open.** `mariadb-lsp lsp`
(described in `docs/language-server.md`) has the threads of the plan: reader,
writer, event loop, request pool and parser pool. It has incremental sync with
`Statement_index::update()`, `Line_map::update()` and per-statement verdicts that
survive edits. Checks run as chunked passes, viewport and edit first; results from
stale snapshots are moved through the edits or dropped. It serves diagnostics,
document symbols, folding, selection ranges and semantic tokens (`range`, `full`,
`full/delta`). It also handles `mariadb/documentContext`, `mariadb/viewport`,
`mariadb/outline`, `mariadb/parseProgress` and work done progress. 17 Python e2e tests
run under ctest. ThreadSanitizer reports no race in the unit tests or the e2e run (a
planted race in a control program was reported).

Measured on 8 cores with the 256 MB dump: range tokens for 100 lines in the middle
arrive about 370 ms after the `didOpen` message is read (target 500 ms). An edit in
the middle publishes its diagnostics in 4 ms median and 5 ms max over 10 edits
(target 100 ms p95).

Three changes against §2.6 got there. The text is a `std::string` changed in place
unless a snapshot shares it, not a piece table. The transport reads the file
descriptor, not `std::cin`, which reads a character at a time. Opening indexes in
parallel (`build_parallel`, 775 ms to 165 ms).

Open items:
- The Windows build is ported and has a CI job, but it has not been run yet.
- The bounded give-up region of §2.6 is not built. An edit that opens a string or
  comment which never closes re-splits to the end of the file and rechecks what follows.
  In a dump the splitter resynchronizes at the next quote or `*/`: 5 ms and 24 ms
  measured.
- Semantic tokens do not classify identifiers; that needs the catalog (P5).
- `mariadb/refreshCatalog` is P5.

- JSON-RPC over stdio (Content-Length framing), one reader thread, one writer thread,
  request workers on the pool, `$/cancelRequest`, incremental sync with the piece table,
  the edit path of §2.6 with bounded give-up regions, debounce.
- Features: diagnostics (batched publishing), document symbols, folding, selection
  ranges, semantic tokens (`range` first, then `full` and `full/delta`), the custom
  messages of §2.8, `window/workDoneProgress` for large files.
- Python end-to-end tests: start the binary, speak LSP, assert; a replay test that opens
  a large file and checks a `semanticTokens/range` request is answered before the full
  parse finishes.
- Windows build added to CI.
- Done when: opening the 256 MB dump and requesting tokens for lines 32000–32100 answers
  in *target* under 500 ms; typing in the middle of it publishes diagnostics for the
  edited statement in *target* under 100 ms p95; all e2e tests pass on three platforms.

### P5 — Completion, hover, navigation, live catalog (2–3 weeks)

**Status 2026-10-17: done on macOS.** Completion (§2.5), hover, definition, document
highlight, references and rename in document scope, signature help, workspace symbols,
the workspace catalog (`catalogFiles`) and the live catalog of §2.11 are built and
tested: 33 completion scenarios and the navigation, URI, secret store and workspace unit
tests, and 17 end-to-end tests over LSP (`tests/e2e/test_language.py`). Completion at a
1 MB document measured 0.21 ms p95 in a Release build (target 20 ms); ctest checks the
bound. The live test starts a throwaway `mariadbd` the way the shell's sandboxes do and
keeps its secrets in a private store written through `mariadb-secret-store-plaintext`;
it covers the catalog, the server's `sql_mode` taking effect, `mariadb/refreshCatalog`,
an unconfigured connection, `check --connection` and `connections`. ThreadSanitizer
reports no race in the unit tests or the end-to-end run. libmariadbparser is 2.4 (the
keyword table). Open: Linux and Windows untested (the secret store's Windows
process code is written but has never run); semantic tokens still do not classify
identifiers; references and rename stay within one document.

- Sentinel loop (§2.5), keyword completion from the expected set with spelling mapping,
  catalog candidates, scope scanner fallback, completion item ranking and
  `textEdit` ranges.
- Hover, go-to-definition, document highlight, references, rename (document scope),
  signature help, workspace symbols; workspace catalog fold with configured file order.
- Live-connection catalog (§2.11): URI normalisation (ported from the MCP plugin's
  `config.py` with its test cases), the secret-store helper client (tested against
  `mariadb-secret-store-plaintext` from a shell build, the way the MCP plugin's
  `run_tests.py` does), the client session wrapper (modelled on rest-daemon's
  `MySQLSession`: typed options, timeouts, SSL preferred, read-only first statement),
  `information_schema` snapshot, background refresh with backoff; `connections`
  subcommand and `--connection` on `check`/`outline`.
- Done when: a table of completion scenarios (cursor positions × expected candidate sets)
  passes; completion p95 *target* under 20 ms at 1 MB documents; a live catalog test
  against a sandbox server started by `mariadb-shell` passes.

### P6 — VS Code integration (in vscode-extension repo)

- Add `vscode-languageclient`; locate `mariadb-lsp` next to the resolved
  `mariadb-shell` binary (or `mariadb.lsp.path`); `documentSelector` for `mariadb-sql`
  and `sql`; send `mariadb/documentContext` from `SqlEditorBinding` and `SqlModes`
  changes; status bar progress from `mariadb/parseProgress`.
- Replace the TypeScript statement scanner's gutter ranges with the server's folding
  ranges or a `mariadb/statements` request; keep the ANSI_QUOTES token provider until the
  server's semantic tokens cover it, then drop it.
- Settings: `mariadb.lsp.enabled`, `mariadb.lsp.path`, `mariadb.lsp.threads`,
  `mariadb.lsp.trace`.
- Extend the vscode mock for unit tests; one ui-test that opens a SQL file and sees a
  diagnostic.

**Status 2026-10-18: started, on vscode-extension's `wip/lsp-integration`.** The extension
finds `mariadb-lsp` through `mariadb.lsp.path`, else the PATH, else `~/.local/bin` (the
user's choice over "next to `mariadb-shell`"; downloading it when missing, as the shell
is, comes later), probing `mariadb-lsp --version`, added here for that. It starts the
server when the first SQL document opens, sends `mariadb/documentContext` with each
document's connection (picked, header or default), and `mariadb/refreshCatalog` after a
script with DDL runs from the editor, since the live catalog is otherwise read once. A
shell the extension installed is passed as `shellBinary`, since its secret store helpers
are not on the PATH. The settings are the four above (`mariadb.lsp.trace.server`) and a
`MariaDB: Restart SQL Language Server` command. The server's semantic tokens are
switched off in the client's middleware: VS Code takes one provider's answer, and the
server's would hide the extension's ANSI_QUOTES marks while adding nothing over the
TextMate grammar until identifiers are classified (next step 1). Unit tests over a fake
client; the ui-test suite `lsp` checks a syntax error in the Problems view and its fix,
and completion of a sandbox's tables through the live catalog. Since then: a status bar
entry from `mariadb/parseProgress` (the client declines work done progress, so it is not
shown twice), and settings for `catalogFiles`, `catalogRefreshSeconds`, `keywordCase`,
`sqlMode`, `serverVersion`, `debounceMs`, `maxDiagnostics` and the progress threshold,
sent through `workspace/didChangeConfiguration`; a changed `catalogRefreshSeconds` now
applies to the connections already read (`Live_catalog::set_refresh_interval`). Since then:
`--mcp` (the extension runs the server so; catalogs are read with the `db.*` tools of the
extension's MCP server through `mariadb/dbCall`, so one MCP server serves both and no
credentials reach the server), and completion that goes from a schema to a table to a
column and adds the table to FROM, joined on a foreign key or through an n:m link table;
in MCP mode the details of tables, views and routines are read when a request first
needs them. Since then: the added FROM and JOIN clauses follow the statement's layout and
the `lineWidth` setting (default 80) instead of growing the line being typed. Open: the download, the
gutter's statements from the server.

### P7 — Hardening and packaging (ongoing from P4; 1–2 weeks of focused work)

- ASan/UBSan and TSan CI flavours, Valgrind on Linux, fuzzing of the splitter and the
  edit path (libFuzzer, as the shell's `cmake/fuzzer.cmake`), RSS and latency gates in CI
  with the generated fixtures, static analysis as `cmake/static_analysis.cmake`.
- `cpack` packages per platform; install into the shell's `bin/`; release workflow.

Status (2026-10-19): `cmake/packaging.cmake` adds install rules and a CPack ZIP package
(`cmake --build <dir> --target package`): `bin/mariadb-lsp`, `LICENSE`, `README.md`,
`docs/language-server.md`, `docs/outline-format.md`, named
`mariadb-lsp-<version>-<platform>.zip` as MariaDB Shell names its packages; with a
`-dynamic` vcpkg triplet it also holds, in `lib/`, the shared libraries the binary loads
(OpenSSL, zlib; found with a `RUNTIME_DEPENDENCY_SET`), and the installed binary's RPATH
points there (`@loader_path/../lib`, `$ORIGIN/../lib`). Built on macOS 26 (arm64, static
and `-dynamic` triplets); Linux and Windows not tried yet, and the
macOS binary is neither signed nor notarized.

Status (later on 2026-10-19): no release workflow of its own, by decision: MariaDB Shell
builds this repository as its `src/lsp-server` submodule (`cmake/lsp_server.cmake` there,
an `ExternalProject` with the shell's compiler, vcpkg toolchain, triplet and server tree,
`-DWITH_TESTS=0 -DMDBLS_CHECK_SERVER_PIN=OFF`) and bundles `bin/mariadb-lsp` next to
`mariadb-shell`, with the RPATH of its other bundled binaries, so it uses the package's
OpenSSL and zlib; signing follows the shell's. Built that way on macOS arm64. CI here tests
on Linux; `build-release.yml`, on a push to `release/candidate`, builds on the shell's
release platforms and triplets (Rocky 9 x64/arm64, macOS arm64/x64, Windows x64/arm64).
The CPack zip stays for trying a build on its own.

### P8 — Formatting (added 2026-10-18)

`textDocument/formatting` and `rangeFormatting`, and `mariadb-lsp format FILE`. The
style, as the user chose it: comma-last lists that continue on the same line up to the
width (no break after every comma); JOINs indented one level under FROM with `ON` on the
JOIN's line (one level deeper on the next when too long); subqueries inline when they
fit, else on lines of their own one level deeper; keyword case from `keywordCase`, width
from `lineWidth`; statements that do not parse are left untouched. Completion's inserted
FROM and JOIN follow the same rules, so the two agree.

Built on the parse tree and the gaps between its tokens: a small Wadler/Prettier-style
printer (texts, lines that break or not, groups, nests, aligns, fills) and a builder that
maps the grammar's clause nodes onto it (`src/formatter.cc`). Comments are kept before
the token after them, and a line comment ending a clause keyword's line puts what
follows one level deeper than the keyword; versioned comments and the statements the
formatter does not know are left alone. Each layout is checked: the same tokens and comments, and it parses.

**Status 2026-10-18: queries done** (SELECT, WITH, UNION, SELECT ... INTO), see
`docs/language-server.md` "Formatting". **2026-10-19: DML and views done**: INSERT and
REPLACE ... SELECT, UPDATE, DELETE (single and multi-table), CREATE and ALTER VIEW, with
the query rules for their clauses (the query on a line of its own under the header;
`DELETE FROM t` keeps FROM on its line); CREATE TABLE and ALTER TABLE with several
changes one item per line, one level deeper, whatever their length, partitions and
subpartitions too, table options filling the line (the user's choice,
2026-10-19; not aligned in columns, short tables too). **2026-10-23: routines done**:
CREATE PROCEDURE, FUNCTION, TRIGGER and EVENT, and compound statements at the top level;
one statement per line in blocks, IF, CASE and loops, one level deeper (the user's
choice, 2026-10-23), each statement laid out on its own by the rules above; the header
on its line, parameters on it when they fit (else one per line), characteristics and
event clauses one level deeper on lines of their own; empty lines between statements
kept. Clauses one level deeper than their statement's first line by default, at the top
level and in bodies, each switchable (`indentClauses`, `indentRoutineClauses`, the user's
choice 2026-10-23); completion's added FROM always on a line of its own, placed by the
same settings, and `completionAddsTables` to turn the added tables off. **2026-10-23:
more statements**: EXPLAIN/ANALYZE of a query, CREATE TABLE ... AS SELECT (the query
under the header, as a view's), INSERT ... VALUES/SET (rows fill the line aligned after
VALUES, the user's choice; up to 64 KB per statement and 4 MB of formatted text, so
dumps stay untouched and fast), GRANT/REVOKE/CREATE and ALTER USER clauses, ALTER
EVENT/PROCEDURE/FUNCTION as routines, REST SQL as MariaDB Shell's SHOW CREATE REST
writes it (the user's choice). Open: other DDL (short; recasing at most),
Oracle-mode routines (the parser does not take sql_mode=ORACLE) (`AS BEGIN ... END`, packages: left as they
are), the original relative indentation of line breaks inside other constructs (they all
continue one level deeper), `onTypeFormatting`.

### P9 — SOURCE includes (added 2026-10-19)

MSM's `SOURCE '<path>'[<start>:<end>];` directives: the outline, diagnostics and navigation
of a script take in the files it includes. Plan, decisions and status in
[`source-includes.md`](source-includes.md).

Estimated total: about four months of one engineer's work to the end of P6, with P0–P2
being the part that de-risks everything else and should be done first in exactly that
order.

---

## 5. Decisions needed from you

1. ~~**REST as a keyword.**~~ Resolved by the merge (2026-10-15): all 61 new REST SQL
   keywords are non-reserved, including after `USE`, `GRANT` and `REVOKE`; no backticks
   are needed anywhere.
2. **Tree construction.** Offline from the derivation plus a small rule-info API (§2.4a),
   not inside the grammar actions. Agree? (Built in that spirit, but the API differs:
   the reduction depths plus bison's own grammar tables, not a generated rule-info
   table.)
3. **MariaDB pin.** The build fetches the server at the parser's pin (`bab03b0f`,
   13.1.0) so that one checkout serves the client library and the grammar. Moving the
   pin, and the re-extraction that comes with it, is deferred as agreed.
4. **Where the binary ships.** The plan assumes `mariadb-lsp` is installed into
   mariadb-shell's `bin/` and the extension finds it there. Alternative: the extension
   bundles per-platform binaries itself.
5. **Sync kind.** Incremental sync with a piece table (§2.6), as your step 5 asks. Agree?
   (Incremental sync was built; the text is a `std::string` changed in place, not a
   piece table, since the measured edit cost did not call for one.)
6. **C++ standard and JSON library.** C++23 and RapidJSON, matching the shell.
7. **Connection lookup order.** For a URI, the GUI list (`GUI:CONN:`) is tried first,
   then the MCP list (`MCP:CONN:`), then the shell's own `password` credentials (§2.11).
   The last step means a connection the user only ever opened in the shell works in the
   editor too; the MCP plugin deliberately does not read those. Keep that step, or
   restrict the server to the GUI and MCP lists like the MCP plugin?
8. **SSH tunnels.** `mariadb+ssh://` URIs are out of the first version; the server
   reports them as unsupported. Agree, or should the server shell out to
   `mariadb-shell` for those?

## 6. Out of scope for this plan

`ALTER` generation from two catalogs and EER rendering (the model is shaped
for them, the code is not written here); Oracle `sql_mode`; multi-version grammar pins
inside libmariadbparser (one pin now; the option is left in the API); re-extracting the
parser against a newer server tree (deferred by decision); an MCP transport; packaging
into distro repositories.
