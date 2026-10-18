# The language server

`mariadb-lsp lsp` speaks the Language Server Protocol over stdin and stdout
(JSON-RPC with `Content-Length` framing). Positions are LSP positions: 0-based lines
and UTF-16 columns.

```bash
mariadb-lsp lsp [--stdio] [--threads N] [--log FILE] [--shell-bin-dir DIR]
                [--sql-mode M] [--delimiter D] [--version-id N]
```

`mariadb-lsp --version` prints `mariadb-lsp <version>` (the `VERSION` file's) and exits;
the VS Code extension probes it to tell a usable binary from anything else of that name.
`--threads` limits the parser threads (default: one per core). `--log` appends a log
with timestamps. `--sql-mode`, `--delimiter` and `--version-id` set the defaults for
documents; the client can override them, see below. `--shell-bin-dir` is MariaDB
Shell's binary directory, where the secret store helpers of live connections are
(default: the directory of `mariadb-shell` on `PATH`).

## Capabilities

| Request or notification | What the server does |
| --- | --- |
| `textDocument/didOpen`, `didChange`, `didClose` | incremental sync; the statements an edit touches are split again and checked, the rest keep their results |
| `textDocument/publishDiagnostics` | syntax errors with the expected tokens, statements that could not be checked (unsupported `sql_mode`, invalid UTF-8, an internal parser error), a string, quoted identifier or comment left open at the end |
| `textDocument/documentSymbol` | schemas, tables with columns, indexes, foreign keys and checks, views, routines, triggers, events, sequences and REST objects, from the outline |
| `textDocument/foldingRange` | statements over several lines, runs of comment lines, `BEGIN ... END` and the other compound blocks of routines |
| `textDocument/selectionRange` | the syntax nodes around a position, then its statement, then the document |
| `textDocument/semanticTokens/full`, `full/delta`, `range` | keywords, strings, numbers, comments, variables, operators, functions, parameters; `full` and `full/delta` answer `null` for documents above the limit below, and the client then asks for the visible range |
| `textDocument/completion` | the keywords the parser can take at the cursor, and the names that fit there (see [Completion](#completion)); trigger characters `.` and `` ` `` |
| `textDocument/hover` | Markdown for tables and views (columns, keys), columns (type, default, comment), routines (signature), built-in functions (call forms and summary from the server's help tables), aliases, CTEs, variables, schemas |
| `textDocument/definition` | where a table, view, column, routine or schema is created: in the document, or in a workspace catalog file; an alias, CTE or variable where its statement defines it; `null` for objects only the connected server knows |
| `textDocument/references` | every name in the document that refers to the same object, resolved statement by statement with the schema in use there; aliases, CTEs and variables within their statement |
| `textDocument/documentHighlight` | the references within the statement at the cursor; the defining one is a write |
| `textDocument/prepareRename`, `rename` | aliases, CTEs, variables, and tables, views, columns and routines the document itself defines; the edits cover the document, and the new name is quoted when it has to be |
| `textDocument/signatureHelp` | the call forms of a built-in function, or a stored function's or procedure's parameters, with the active parameter; trigger characters `(` and `,` |
| `workspace/symbol` | tables, views, routines, triggers, events, sequences and schemas of the open documents and the workspace catalog whose name contains the query; columns too when the query is not empty |
| `workspace/didChangeConfiguration` | `settings.mariadb` takes the same keys as the initialization options; the workspace catalog is read again when `catalogFiles` changed, the documents that did not set a `connection` of their own through `mariadb/documentContext` move to the new `connection`, and a changed `sqlMode` or `serverVersion` applies to the open documents that did not set their own; a changed `catalogRefreshSeconds` applies to the connections already read, counted from their last read |
| `textDocument/didSave` | saving one of the workspace catalog's files reads the catalog again |

Diagnostics carry `source: "mariadb"` and a `code`: `syntax-error`, `not-checked`,
`unterminated` or `splitter`. Syntax errors and an unterminated string, identifier or
comment are errors (severity 1); statements not checked and splitter messages are
warnings (2). An unsupported `sql_mode` is reported once, as information (3), at the
first statement of each run in that mode.

## Initialization options

`initializationOptions` of the `initialize` request:

| Option | Default | Meaning |
| --- | --- | --- |
| `sqlMode` | `""` | the `sql_mode` documents start in |
| `serverVersion` | `"13.1.0"` | judges versioned comments (`/*!100500 ... */`); `"11.4.2-MariaDB"` or `"110402"` |
| `delimiter` | `";"` | the delimiter documents start with |
| `debounceMs` | `150` | quiet time after an edit before its statements are checked |
| `maxDiagnostics` | `1000` | per document, for the per-statement diagnostics; the `unterminated` and `splitter` ones come on top |
| `semanticTokensFullLimit` | `4194304` | bytes; larger documents get `null` for `semanticTokens/full` and `full/delta` |
| `progressThreshold` | `4194304` | bytes; documents at least this large report check progress |
| `catalogFiles` | `[]` | SQL files whose schema objects every document sees, folded in this order as one script; a directory stands for its `*.sql` files, sorted by path; relative to the first workspace folder (else `rootUri`, `rootPath`) |
| `connection` | none | a connection URI every document uses until `mariadb/documentContext` says otherwise |
| `shellBinary` | none | the path of `mariadb-shell`; its directory holds the secret store helpers |
| `shellBinDir` | `--shell-bin-dir` | that directory directly |
| `credentialStoreHelper` | the shell's | the secret store helper's name (`keychain`, `login-path`, `windows-credential`, `plaintext`) |
| `catalogRefreshSeconds` | `0` | read a connection's catalog again this often; 0 reads it on demand only |
| `keywordCase` | `"upper"` | `"lower"` completes keywords and built-in functions in lower case |

The client's `textDocument.completion.completionItem.snippetSupport` makes function
completions snippets (`CONCAT($0)`).

## MariaDB messages

| Message | Direction | Content |
| --- | --- | --- |
| `mariadb/documentContext` | client to server, notification | `{textDocument: {uri}, sqlMode?, serverVersion?, delimiter?, currentSchema?, connection?}`, sent after `didOpen` (before it, the message is ignored). A change of `sqlMode`, `serverVersion` or `delimiter` indexes and checks the document again. `connection` is a connection URI (`null` or `""` drops it): the document then sees that server's catalog, and its `sql_mode` and version unless the client set them in this message. `currentSchema` is the schema unqualified names belong to before the document's first `USE`. |
| `mariadb/viewport` | client to server, notification | `{textDocument: {uri}, range}`: the visible lines; their statements are checked first |
| `mariadb/outline` | client to server, request | `{textDocument: {uri}}`; the result is the outline JSON of [outline-format.md](outline-format.md), with `file` set to the URI |
| `mariadb/refreshCatalog` | client to server, request | `{textDocument?: {uri}}`: reads the workspace catalog again, and the catalog of the document's connection (of every connection without `textDocument`), in the background; the result is `null` at once |
| `mariadb/parseProgress` | server to client, notification | `{uri, version, checked, total, done}` while a large document is checked |
| `window/showMessage` | server to client | a connection that cannot be used ("connection not configured", a failed login, an SSH URI), once per connection and message; a `connection` that is not a URI, which is refused (and never shown as given, since it may hold a password) |

When the client announces `window.workDoneProgress`, a large document's check is also
reported as work done progress ("Checking SQL").

## Completion

The cursor's statement is parsed on the request thread, in three ways:

1. **Keywords.** The text from the statement's start to the word being typed is parsed;
   the parser's expected set at its end (LAC, as for diagnostics) is the set of
   keywords that can come next, spelled from MariaDB's keyword table. When that text
   is already a complete statement, its tokens are parsed again with a token that is
   never valid appended, which gives the same set. Where a name may stand, every
   non-reserved keyword is in that set; such a keyword is offered only when the parser
   would go on differently after it than after a name. That test costs a parse each, so
   it is made only for the keywords that match the letters typed, its answers are kept
   for the next keystroke in the same word, and with nothing typed yet non-reserved
   keywords are left out and the list is marked incomplete.
2. **Names.** The word at the cursor is replaced by the identifier `mdb_cursor_` and the
   statement parsed with its rest; if that is rejected, it is parsed cut after the
   sentinel; if that is rejected too, the partial tree and the tokens before the cursor
   decide. The sentinel's place in the tree says what is wanted: a table (`FROM`,
   `JOIN`, `INTO`, `UPDATE`, `table_ident`), a column (`simple_ident`: of `a` in
   `a.|`, of the tables in scope otherwise), a schema (`USE`), a procedure (`CALL`), a
   function, or nothing (a name being defined). The tables in scope are those of the
   enclosing query and the queries around it, with their aliases; derived tables and
   CTEs bring their select list's names. An `INSERT` column list, `CREATE TABLE`'s keys,
   `ALTER TABLE`, `REFERENCES` and `NEW.`/`OLD.` in triggers name their table directly.
3. **Candidates** come from the catalog layers, in order: the document's own DDL (the
   outline, possibly a few edits old; a newer one is folded in the background), the
   workspace catalog (`catalogFiles`), and the connection's catalog. Unqualified names
   belong to the schema of the last `USE` before the statement, else `currentSchema`,
   else the connection's default schema. Built-in functions come from
   `src/builtin_functions.inc`, written by `scripts/gen_builtin_functions.py` from the
   server's function registries and help tables.

Items are ranked columns, then aliases and variables, then functions, then keywords. A
name that needs quoting is inserted in backticks; typing an opening backtick completes
inside it.

## Live connections

A connection is named by its URI, in MariaDB Shell's syntax
(`[scheme://][user@]host[:port][/schema][?options]`, or a socket path), and compared
in the MCP plugin's normal form: scheme `mariadb` when none, no password, the host in
lower case, port 3306 added. The password is read from MariaDB Shell's secret store,
directly, through its helper program (`mariadb-secret-store-<helper> get|list`, JSON on
stdin and stdout): the GUI's entry (`GUI:CONN:<uri>`), then the MCP server's
(`MCP:CONN:<uri>`, and the older `...:Connection:` keys), then one the shell stored
when it connected itself (secret type `password`, `user@host:port`). A password
written into the URI is used as it is. The helper is the one the shell would use: the
variable `MARIADB_SHELL_CREDENTIAL_STORE_HELPER`, else `credentialStore.helper` in the
shell's `options.json`, else `keychain` (macOS), `windows-credential` (Windows) or
`login-path`. Helpers run one at a time, so none inherits another's pipes, and one that
has not finished after 60 s (a keychain prompt nobody answers) is killed.

Each connection has a thread of its own. It connects (5 s connect and 30 s read
timeouts, TLS when the server offers it, the URI's `ssl-mode`, `ssl-ca`, `ssl-cert`,
`ssl-key`), makes the session read-only, reads `@@sql_mode`, `VERSION()` and
`DATABASE()`, then schemas, tables, columns, indexes, foreign keys, views, routines
with parameters, triggers and events from `information_schema` (not
`performance_schema`), and disconnects. It does so when the connection is first named,
on `mariadb/refreshCatalog`, every `catalogRefreshSeconds`, and after a failure again
after 5 s, 10 s, 20 s, ... up to five minutes. Shutting down stops a reading between
its queries. A missing password or an SSH URI
(`mariadb+ssh://`, not supported) is not retried. No request waits for a connection:
requests use the last catalog read. The password is held only until the connect call
returns, and never logged.

## How it works

- **Threads.** A reader thread reads and parses messages and acts on `$/cancelRequest`
  at once. An event loop thread owns the documents. Requests run on a small request
  pool, statement checks on the parser pool (one parser cache per thread), and a writer
  thread writes the answers. The transport reads and writes file descriptors: with
  `std::cin`, reading a 256 MB `didOpen` took four seconds. The workspace catalog is
  read on the request pool; each live connection has its own thread.
- **Documents.** A document keeps its text, line map and statement index current edit
  by edit; jobs work on immutable snapshots. The text and line map are changed in place
  unless a job still holds a snapshot of them, so a mid-file edit costs a `memmove`.
  The plan's piece table was not needed for that.
- **Index updates.** After an edit the splitter starts after the last statement before
  the edit that ended with a delimiter in a clean state, and stops at the first statement
  after the inserted text that starts where an old one did, shifted by the edit, in the
  same splitter state; the old statements after it are kept with shifted offsets. If a
  statement that sets `sql_mode` was involved, or the splitter reported a message (in
  the old or the new index), the whole text is split again. A document is indexed in
  parallel (`Statement_index::build_parallel`, one thread per core regardless of
  `--threads`) when it is opened, replaced whole (`didChange` without a range) or given
  a new context.
- **Checks.** Checking runs as passes over a snapshot, a chunk of statements per job.
  Each job takes the statements on screen or around the last edit first. Results that
  arrive after further edits are moved to where those edits put their statements, or
  dropped when an edit replaced them.
- **References.** Only statements whose text contains the name are parsed, in parallel;
  a statement over 64 KiB is parsed only up to a little after the last token spelling
  the name, so a dump's long `INSERT`s cost a lex each.

## Measured

On an 8-core Apple M-series machine with a generated 256 MB mysqldump file
(`scripts/gen_fixture_dump.py 256 dump.sql`), through the e2e client:

| Step | Time |
| --- | --- |
| Parse the 283 MB `didOpen` message | 135 ms |
| Index (parallel) and line map | 170 ms |
| `semanticTokens/range` for 100 lines in the middle, after the message was read | about 370 ms |
| Full syntax check of the dump, after the message was read | about 1 s |
| Typing in the middle of the dump: edit to published diagnostics | 4 ms median, 5 ms max |

Completion in a 1 MB document, 300 requests at positions spread over it (after `a.`,
after `FROM`, in a `WHERE`, one letter into a word), round trip through the e2e client
with a Release build: 0.15 ms median, 0.21 ms p95, 9.5 ms max (the first keyword test at
a new position).

## Tests

`tests/e2e` holds a small LSP client and end-to-end tests, run by ctest
(`mariadb-lsp.e2e`) on a 16 MB generated dump; `-DMDBLS_LARGE_TESTS=ON` adds a run on a
256 MB one. `MDBLS_E2E_LARGE_MB` sets the size of the dump the large-file tests open
(ctest passes 16; run by hand, the default is 64). `MDBLS_COMPLETION_P95_MS` is the
bound of the completion latency test: ctest passes 20 for optimized builds and 100 for
Debug ones. By hand:

```bash
MARIADB_LSP=bld/bin/mariadb-lsp python3 -m unittest discover -s tests/e2e -v
```

The live-connection tests start a throwaway server with the `mariadbd` on `PATH` (or
`MDBLS_TEST_MARIADBD`), as MariaDB Shell's sandboxes do, and keep a private secret store
through MariaDB Shell's `mariadb-secret-store-plaintext` helper, found in
`MDBLS_SHELL_BIN_DIR` (a CMake cache variable, default `../mariadb-shell/build/bin`).
Without either, they are skipped; so are the unit tests of the secret store.
