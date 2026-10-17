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
| `textDocument/publishDiagnostics` | syntax errors with the expected tokens, statements that could not be checked (unsupported `sql_mode`, invalid UTF-8), a string or comment left open at the end |
| `textDocument/documentSymbol` | schemas, tables with columns, indexes, foreign keys and checks, views, routines, triggers, events, sequences and REST objects, from the outline |
| `textDocument/foldingRange` | statements over several lines, runs of comment lines, `BEGIN ... END` and the other compound blocks of routines |
| `textDocument/selectionRange` | the syntax nodes around a position, then its statement, then the document |
| `textDocument/semanticTokens/full`, `full/delta`, `range` | keywords, strings, numbers, comments, variables, operators, functions, parameters; `full` answers `null` for documents above the limit below, and the client then asks for the visible range |

Diagnostics carry `source: "mariadb"` and a `code`: `syntax-error`, `not-checked`,
`unterminated` or `splitter`.

## Initialization options

`initializationOptions` of the `initialize` request:

| Option | Default | Meaning |
| --- | --- | --- |
| `sqlMode` | `""` | the `sql_mode` documents start in |
| `serverVersion` | `"13.1.0"` | judges versioned comments (`/*!100500 ... */`); `"11.4.2-MariaDB"` or `"110402"` |
| `delimiter` | `";"` | the delimiter documents start with |
| `debounceMs` | `150` | quiet time after an edit before its statements are checked |
| `maxDiagnostics` | `1000` | per document |
| `semanticTokensFullLimit` | `4194304` | bytes; larger documents get `null` for `semanticTokens/full` |
| `progressThreshold` | `4194304` | bytes; larger documents report check progress |

## MariaDB messages

| Message | Direction | Content |
| --- | --- | --- |
| `mariadb/documentContext` | client to server, notification | `{textDocument: {uri}, sqlMode?, serverVersion?, delimiter?, currentSchema?, connection?}`: the state a document starts in. A change of `sqlMode`, `serverVersion` or `delimiter` indexes and checks the document again. `connection` (a connection URI) and `currentSchema` are accepted and ignored until the live catalog (P5) reads them. |
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
- **Index updates.** After an edit the splitter starts at the last statement before the
  edit that ended cleanly and stops at the first statement that starts where an old one
  did, shifted by the edit, in the same splitter state; the old statements after it are
  kept with shifted offsets. Large texts are indexed in parallel when opened
  (`Statement_index::build_parallel`).
- **Checks.** Checking runs as passes over a snapshot, a chunk of statements per job.
  Each job takes the statements on screen or around the last edit first. Results that
  arrive after further edits are moved to where those edits put their statements, or
  dropped when an edit replaced them.

## Measured

On an 8-core Apple M-series machine with a generated 256 MB mysqldump file
(`scripts/gen_fixture_dump.py 256`), through the e2e client:

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
256 MB one. By hand:

```bash
MARIADB_LSP=bld/bin/mariadb-lsp python3 -m unittest discover -s tests/e2e -v
```
