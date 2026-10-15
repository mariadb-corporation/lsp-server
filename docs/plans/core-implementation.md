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

The MariaDB server grammar (`sql/sql_yacc.yy`) and lexer (`sql/sql_lex.cc`) mechanically
extracted into a standalone C library, by script, from a server source tree. What is in
this branch is the **extraction core only**: the public header, the C/C++ sources, the
pipeline (`tools/extract.sh`, `stub.py`, `lexslice.py`, `gen_tables.py`,
`provenance.py`, `build_record.py`, `common.py`), the override mechanism, the pinned
provenance, the grammar model and lexer fixtures (two small archives), the regeneration
procedure (`docs/REGENERATION.md`) and the README. The evidence records, the 212k-record
corpus archives, the analysis tooling, the Python test suite and the test-host-bound
`PLAN.md` were left behind on purpose. Facts the rest of this plan relies on:

- **API** (`include/mdbparse.h`, version 2.2): `mdbp_parse_sql()` lexes and parses one
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
- **Regeneration is scripted but unproven across revisions**: the pipeline has run against
  one server revision only, with zero override entries. `sql_yacc.yy` and `sql_lex.cc`
  each changed about seventy times on the server's `main` since January 2026.
  Re-extracting against a newer tree is deferred (not part of this plan, by decision).
- **Build gaps**: Linux only (CMake stops on macOS), needs `uv` and bash, needs a MariaDB
  source tree at configure time. §3 fixes these.
- **`REST` is not a keyword**, so `CREATE REST SERVICE ...` is rejected by this parser,
  usually at `REST`; `USE rest SERVICE` is rejected later. §2.2 therefore routes REST SQL
  by prefix before the server parser runs.

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
The parser-only file set is `parser.yy parser.{h,cc} parser_driver.h lexer.{h,cc} ast.h
strings.{h,cc}`; it includes no shell headers (its CMake comment says so, so it can be
shared with a server plugin). AST positions exist only per statement (line/column of its
first token), not per name.

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
(Homebrew bison on macOS, `win_bison` on Windows, minimum 3.0.4), C++23 with
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

### 2.2 Parsing one statement (the worker's job)

```text
if kind == RestSql  (first two words match rest_sql_prefixes(), case-insensitive)
    parse with libmariadbparser-mrs  -> RestSqlCorrect / RestSqlIncorrect
else
    parse with libmariadbparser      -> ServerSqlCorrect / ServerSqlIncorrect
```

This differs from the pasted plan in the order: the pasted plan tries the server grammar
first and the REST grammar on failure. The prefix check is what the shell itself does to
route statements (`Sql_handler_registry`, also called from inside its splitter), it costs a
few byte compares, it is exact (every REST statement starts with one of eleven prefixes),
and it saves one guaranteed-failing server parse per REST statement. The server grammar
remains authoritative for everything else. If we later add `REST` as a keyword to
libmariadbparser, nothing here changes.

The worker holds one `mdbp_parser` (created with the statement's `sql_mode`, charset
utf8mb4, and the configured release id for `/*!NNNNNN */` comments) and one MRS driver,
both reused across statements. Parsers whose `sql_mode` differs from the statement's are
re-created; a small per-thread cache keyed by mode bits avoids thrashing in mixed files.

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
    Diagnostic error;                    // byte range, message, expected tokens (server) / message (MRS)
    std::vector<Token> tokens;           // name, byte span (copied out of the parser's buffers)
    std::vector<int> derivation;         // server statements only
    std::unique_ptr<SyntaxTree> tree;    // built lazily, kept only for DDL and for the cursor statement
    std::unique_ptr<mrs::ast::Statement> rest_ast;   // REST statements
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

### 2.7 The object model and outline

`Catalog`: schemas → tables (columns with type, nullability, default, charset/collation,
generated expressions; indexes; primary/unique/foreign keys; table options), views,
triggers, routines (parameters, return type), events, sequences, users/roles (names
only), plus REST objects from the MRS AST (services, schemas, views, procedures, content
sets, auth apps, roles, users). Every object records the statement index that defined it
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

### 2.8 LSP features in scope

In the order they ship (§4): diagnostics; document symbols and the outline request;
folding ranges (one per statement, plus `BEGIN...END`); selection ranges; semantic tokens
(`full`, `full/delta` and `range`, lexer-based with catalog classification of identifiers;
`range` is answered first for large files); completion; hover; go-to-definition and
document highlight for tables/columns/aliases; references within the document; rename of
aliases and, within the document, of objects defined there; signature help for built-in
functions (from the function list the extension's grammar tool already extracts);
workspace symbols over the workspace catalog. Formatting is not in this plan.

Custom messages: `mariadb/documentContext` (client → server: connection URI, `sql_mode`,
current schema, server version for a document), `mariadb/viewport` (client → server:
visible line range, used for scheduling only), `mariadb/outline` (request),
`mariadb/refreshCatalog` (request), `mariadb/parseProgress` (server → client, for a
status bar item on large files).

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

### 2.10 Library boundary

`libmariadblanguageserver` (C++23 inside, C API outside, as the shell will embed it):
documents, index, pool, catalog, completion, diagnostics, JSON outline, live-connection
catalog. The LSP transport (JSON-RPC framing, method dispatch) and the CLI live in the
executable, not in the library. JSON goes through RapidJSON, which the shell already uses
from vcpkg; the only other runtime dependency is the MariaDB client library (§2.11, §3).

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

---

## 3. Repository layout and build

```text
CMakeLists.txt                 top-level, shaped like mariadb-shell's
vcpkg.json                     rapidjson; feature "tests": gtest
cmake/                         copied from mariadb-shell: bootstrap_vcpkg, bootstrap_mariadb,
                               bison, compiler, compile_flags, libutils, exeutils, packaging,
                               git_hooks, static_analysis
libs/mariadbparser/            the extraction core (§1.1), plus: macOS/Windows build,
                               rule-info API (2.3)
libs/mariadbparser-mrs/        vendored REST SQL parser from mariadb-shell (parser.yy, lexer, ast)
libs/sqlsplitter/              vendored Sql_splitter from mariadb-shell, dependencies cut
libs/mariadblanguageserver/    the engine library (C API in include/, C++ in src/)
src/mariadb-lsp/               the executable: LSP transport + CLI subcommands
unittest/                      gtest, one run_unit_tests binary, as in the shell
tests/e2e/                     Python tests speaking LSP over stdio and driving the CLI
tests/fixtures/                small SQL files; large dumps are generated, not committed
scripts/                       run_unit_tests.py, gen_fixture_dump.py, sync_from_shell.py
.github/workflows/             ci.yml copied from shell-ci.yml (self-hosted runner, ccache,
                               [bypass-ci]); macOS and Windows jobs added
```

Build conventions, copied from the shell rather than invented:

- `cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>`
  bootstraps vcpkg into `../vcpkg` and installs the manifest; `-DWITH_TESTS=0` drops gtest.
- **One MariaDB server checkout feeds two things.** `cmake/bootstrap_mariadb.cmake`,
  copied from the shell, fetches the server at `MARIADB_GIT_TAG` (or takes
  `-DMARIADB_SOURCE_DIR`/`-DMARIADB_BUILD_DIR`), initialises `libmariadb`, and builds
  `mariadbclient mysys mysys_ssl caching_sha2_password GenError`. From that tree:
  - the **client library** is linked statically, exactly as the shell links it, headers
    from `<src>/libmariadb/include` and `<src>/include`, with the `mariadb_capi_rename.h`
    guard pre-defined as the shell does; no vcpkg `libmariadb`, no runtime bundling of a
    shared client, auth plugins compiled in as the shell has them;
  - **libmariadbparser's extraction** runs against `sql/sql_yacc.yy` and `sql/sql_lex.cc`
    of the same tree. Until the pin moves, `MARIADB_GIT_TAG` is the parser's pin
    (`bab03b0f`), and the provenance check keeps it honest; moving the pin and the
    re-extraction it needs are deferred by decision. A target `mariadbparser-vendor`
    writes the generated parser sources into the tree so that a release build needs no
    server source.
- `cmake/bison.cmake` finds Homebrew bison on macOS and `win_bison` on Windows;
  libmariadbparser's `tools/extract.sh` is ported to Python (it already needs Python for
  `stub.py`, 3.11 or newer for `tomllib`) so the extraction runs on all three platforms;
  `uv` is dropped in favour of `find_package(Python3 3.11)`.
- `cmake/compiler.cmake`: C++23, `-Werror -Wall -Wextra -Wpedantic -Wshadow`. The C
  library keeps its own flags.
- `mysql_library_init()` runs once in `main()`, and every pool worker that opens a
  session holds an RAII `mysql_thread_init()`/`mysql_thread_end()` token.
- Packaging: `cpack -G TGZ` per platform; the binary is meant to be installed into
  mariadb-shell's `bin/` next to `mariadb-shell`, which is where the extension's locator
  will look (§5, decision 4).
- Vendored code from mariadb-shell (`libs/sqlsplitter`, `libs/mariadbparser-mrs`) is
  copied by `scripts/sync_from_shell.py`, which records the shell commit in a
  `PROVENANCE.json` next to the copy; a CI check compares the vendored files with the
  shell tree when one is available and fails on silent drift. Local edits are confined to
  a small patch file applied after copying, so a re-sync is mechanical.

---

## 4. Phases

Each phase ends with its tests green in CI on Linux and macOS (Windows from P4), and a
short record in `docs/plans/records/<phase>.md` of what was measured. Numbers below marked
*target* are to be measured, not assumed.

### P0 — Build foundation (1–2 weeks)

- Copy the shell's CMake modules and `vcpkg.json` shape; top-level `CMakeLists.txt`,
  `unittest/` with `run_unit_tests`, `scripts/run_unit_tests.py`, CI workflow;
  `bootstrap_mariadb.cmake` at the parser's pin, the client library linked from it.
- libmariadbparser builds on macOS and Linux from this tree: `extract.sh` → Python,
  `uv` requirement dropped, platform gate removed, the library's CMake rewritten to use
  the bootstrapped server tree and nothing from the left-behind apparatus;
  `mariadbparser-vendor` target and a build from vendored sources without a server tree.
- A first gtest for the parser library (parse, lex, expected set) so the C library has
  a test in this tree.
- Done when: `cmake --build` and `ctest` pass on macOS (this machine) and on the Linux
  CI runner.

### P1 — Splitter and statement index (1 week)

- `libs/sqlsplitter`: vendored `Sql_splitter`, dependencies cut, its 1,829-line gtest
  suite ported.
- `StatementIndex` (§2.1): streaming build from a file or buffer, line table,
  offset→line/UTF-16 column module, `sql_mode` tracking, REST-prefix classification.
- CLI skeleton with `split` and `tokens`.
- Done when: the index of a generated 256 MB dump builds in *target* under 2 s on this
  machine and under 400 MB RSS beyond the file itself; chunk-boundary tests pass at the
  shell's chunk sizes; the sakila schema and a set of mysql-test files split to the same
  statements the shell's own splitter produces (differential test against
  `mariadb-shell` when available).

### P2 — Parallel parsing pipeline and `check` (2 weeks)

- `libs/mariadbparser-mrs`: vendored REST parser, built with `BISON_TARGET` as in the
  shell, its `parser_t.cc` tests ported; a C wrapper returning the first error with byte
  offsets (convert the lexer's line/column back to bytes via the line table).
- `ParserPool` (§2.3) with priorities, idle join, version-stale dropping; per-thread
  parser cache keyed by `sql_mode`.
- `check` subcommand, text and JSON output, exit codes.
- Thread-safety evidence: TSan build of the pool over a statement corpus with a planted
  race as control, N-thread differential against the single-threaded run.
- Done when: `check` over the sakila schema, the mysql-test SQL files and the REST
  grammar test file reports the expected verdicts (golden files); a 256 MB dump checks
  end-to-end in *target* under 10 s on 8 cores; TSan clean.

### P3 — Syntax tree, object model, `outline` (2–3 weeks)

- libmariadbparser 2.3: `mdbp_rule_info()` generated from the grammar model; symbol
  version `MDBPARSER_2.3`.
- Tree builder (§2.4a), arena nodes, lookup by rule name, token spans.
- Catalog fold for the DDL in §2.7, including `ALTER TABLE` sub-commands and REST objects;
  JSON outline writer with a versioned schema and a JSON Schema file under `docs/`.
- `outline` subcommand; `textDocument/documentSymbol` data prepared.
- Done when: the sakila schema and a `CREATE TABLE` corpus from mysql-test fold without
  errors into the expected model (golden JSON fixtures); every rule name the walker uses
  is listed in one test that fails if the grammar model lacks it; tree building for all
  DDL in a 256 MB dump adds *target* under 10% to `check`'s time.

### P4 — The language server (3 weeks)

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

### P6 — VS Code integration (1–2 weeks, in mariadb-shell-plugins/code_ext)

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

### P7 — Hardening and packaging (ongoing from P4; 1–2 weeks of focused work)

- ASan/UBSan and TSan CI flavours, Valgrind on Linux, fuzzing of the splitter and the
  edit path (libFuzzer, as the shell's `cmake/fuzzer.cmake`), RSS and latency gates in CI
  with the generated fixtures, static analysis as `cmake/static_analysis.cmake`.
- `cpack` packages per platform; install into the shell's `bin/`; release workflow.

Estimated total: about four months of one engineer's work to the end of P6, with P0–P2
being the part that de-risks everything else and should be done first in exactly that
order.

---

## 5. Decisions needed from you

1. **REST dispatch order.** Prefix check before the server parser (§2.2) instead of
   server-then-REST. Agree?
2. **Tree construction.** Offline from the derivation plus a small rule-info API (§2.4a),
   not inside the grammar actions. Agree?
3. **MariaDB pin.** The build fetches the server at the parser's pin (`bab03b0f`,
   13.1.0) so that one checkout serves the client library and the grammar. Moving the
   pin, and the re-extraction that comes with it, is deferred as agreed.
4. **Where the binary ships.** The plan assumes `mariadb-lsp` is installed into
   mariadb-shell's `bin/` and the extension finds it there. Alternative: the extension
   bundles per-platform binaries itself.
5. **Sync kind.** Incremental sync with a piece table (§2.6), as your step 5 asks. Agree?
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

Formatting; `ALTER` generation from two catalogs and EER rendering (the model is shaped
for them, the code is not written here); Oracle `sql_mode`; multi-version grammar pins
inside libmariadbparser (one pin now; the option is left in the API); re-extracting the
parser against a newer server tree (deferred by decision); an MCP transport; packaging
into distro repositories.
