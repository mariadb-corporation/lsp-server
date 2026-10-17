# The language server

`mariadb-lsp lsp` speaks the Language Server Protocol over stdin and stdout
(JSON-RPC with `Content-Length` framing). Positions are LSP positions: 0-based lines
and UTF-16 columns.

```bash
mariadb-lsp lsp [--stdio] [--threads N] [--log FILE] [--sql-mode M] [--delimiter D] [--version-id N]
```

`--threads` limits the parser threads (default: one per core). `--log` appends a log
with timestamps. `--sql-mode`, `--delimiter` and `--version-id` set the defaults for
documents; the client can override them, see below.

## Capabilities

| Request or notification | What the server does |
| --- | --- |
| `textDocument/didOpen`, `didChange`, `didClose` | incremental sync; the statements an edit touches are split again and checked, the rest keep their results |
| `textDocument/publishDiagnostics` | syntax errors with the expected tokens, statements that could not be checked (unsupported `sql_mode`, invalid UTF-8, an internal parser error), a string, quoted identifier or comment left open at the end |
| `textDocument/documentSymbol` | schemas, tables with columns, indexes, foreign keys and checks, views, routines, triggers, events, sequences and REST objects, from the outline |
| `textDocument/foldingRange` | statements over several lines, runs of comment lines, `BEGIN ... END` and the other compound blocks of routines |
| `textDocument/selectionRange` | the syntax nodes around a position, then its statement, then the document |
| `textDocument/semanticTokens/full`, `full/delta`, `range` | keywords, strings, numbers, comments, variables, operators, functions, parameters; `full` and `full/delta` answer `null` for documents above the limit below, and the client then asks for the visible range |

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

## MariaDB messages

| Message | Direction | Content |
| --- | --- | --- |
| `mariadb/documentContext` | client to server, notification | `{textDocument: {uri}, sqlMode?, serverVersion?, delimiter?, currentSchema?, connection?}`: the `sqlMode`, `serverVersion` and `delimiter` of an open document, sent after `didOpen` (before it, the message is ignored; the initial state comes from the command line and `initializationOptions`). A change of any of the three indexes and checks the document again. `connection` (a connection URI) and `currentSchema` are accepted and ignored until the live catalog (P5) reads them. |
| `mariadb/viewport` | client to server, notification | `{textDocument: {uri}, range}`: the visible lines; their statements are checked first |
| `mariadb/outline` | client to server, request | `{textDocument: {uri}}`; the result is the outline JSON of [outline-format.md](outline-format.md), with `file` set to the URI |
| `mariadb/parseProgress` | server to client, notification | `{uri, version, checked, total, done}` while a large document is checked |

When the client announces `window.workDoneProgress`, a large document's check is also
reported as work done progress ("Checking SQL").

## How it works

- **Threads.** A reader thread reads and parses messages and acts on `$/cancelRequest`
  at once. An event loop thread owns the documents. Requests run on a small request
  pool, statement checks on the parser pool (one parser cache per thread), and a writer
  thread writes the answers. The transport reads and writes file descriptors: with
  `std::cin`, reading a 256 MB `didOpen` took four seconds.
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

## Tests

`tests/e2e` holds a small LSP client and end-to-end tests, run by ctest
(`mariadb-lsp.e2e`) on a 16 MB generated dump; `-DMDBLS_LARGE_TESTS=ON` adds a run on a
256 MB one. `MDBLS_E2E_LARGE_MB` sets the size of the dump the large-file tests open
(ctest passes 16; run by hand, the default is 64). By hand:

```bash
MARIADB_LSP=bld/bin/mariadb-lsp python3 -m unittest discover -s tests/e2e -v
```
