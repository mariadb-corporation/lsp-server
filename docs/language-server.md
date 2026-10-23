# The language server

`mariadb-lsp lsp` speaks the Language Server Protocol over stdin and stdout
(JSON-RPC with `Content-Length` framing). Positions are LSP positions: 0-based lines
and UTF-16 columns.

```bash
mariadb-lsp lsp [--stdio] [--mcp] [--threads N] [--log FILE] [--shell-bin-dir DIR]
                [--sql-mode M] [--delimiter D] [--version-id N]
```

`mariadb-lsp --version` prints `mariadb-lsp <version>` (the `VERSION` file's) and exits;
the VS Code extension probes it to tell a usable binary from anything else of that name.
`--threads` limits the parser threads (default: one per core). `--log` appends a log
with timestamps. `--sql-mode`, `--delimiter` and `--version-id` set the defaults for
documents; the client can override them, see below. `--shell-bin-dir` is MariaDB
Shell's binary directory, where the secret store helpers of live connections are
(default: the directory of `mariadb-shell` on `PATH`). `--mcp` reads the catalogs of live
connections through the client instead of connecting itself, see [MCP mode](#mcp-mode).

## Capabilities

| Request or notification | What the server does |
| --- | --- |
| `textDocument/didOpen`, `didChange`, `didClose` | incremental sync; the statements an edit touches are split again and checked, the rest keep their results |
| `textDocument/publishDiagnostics` | syntax errors with the expected tokens, statements that could not be checked (unsupported `sql_mode`, invalid UTF-8, an internal parser error), a string, quoted identifier or comment left open at the end, and what is wrong with [SOURCE directives](#source-includes) |
| `textDocument/documentSymbol` | schemas, tables with columns, indexes, foreign keys and checks, views, routines, triggers, events, sequences and REST objects, from the outline; an object from a file a SOURCE directive includes sits on that directive, its `detail` ending in `· file:line` |
| `textDocument/documentLink` | the path of every SOURCE directive, linked to its file |
| `textDocument/foldingRange` | statements over several lines, runs of comment lines, `BEGIN ... END` and the other compound blocks of routines |
| `textDocument/selectionRange` | the syntax nodes around a position, then its statement, then the document |
| `textDocument/semanticTokens/full`, `full/delta`, `range` | keywords, strings, numbers, comments, variables, operators, functions, parameters; `full` and `full/delta` answer `null` for documents above the limit below, and the client then asks for the visible range |
| `textDocument/completion` | the keywords the parser can take at the cursor, and the names that fit there (see [Completion](#completion)); in the path of a SOURCE directive, its folders and `.sql` files; trigger characters `.`, `` ` `` and `/` (the last for SOURCE paths only: elsewhere it answers an empty list) |
| `textDocument/hover` | Markdown for tables and views (columns, keys), columns (type, default, comment), routines (signature), built-in functions (call forms and summary from the server's help tables), aliases, CTEs, variables, schemas; for an object a file the document includes defines, that file and line |
| `textDocument/definition` | where a table, view, column, routine or schema is created: in the document, in a file it includes, or in a workspace catalog file; an alias, CTE or variable where its statement defines it; `null` for objects only the connected server knows |
| `textDocument/references` | every name in the document, and in the files it includes (in an included file: in the open script that includes it and its files), that refers to the same object, resolved statement by statement with the schema in use there; aliases, CTEs and variables within their statement |
| `textDocument/documentHighlight` | the references within the statement at the cursor; the defining one is a write |
| `textDocument/prepareRename`, `rename` | aliases, CTEs, variables, and tables, views, columns and routines the document or a file it includes defines; the edits cover the document and the files it includes, and the new name is quoted when it has to be |
| `textDocument/signatureHelp` | the call forms of a built-in function, or a stored function's or procedure's parameters, with the active parameter; trigger characters `(` and `,` |
| `textDocument/formatting`, `rangeFormatting` | lays out queries, tables and routines (see [Formatting](#formatting)): the whole document, or the statements the range touches; one edit per statement that changes |
| `workspace/symbol` | tables, views, routines, triggers, events, sequences and schemas of the open documents (with the files they include) and the workspace catalog whose name contains the query; columns too when the query is not empty |
| `workspace/didChangeConfiguration` | `settings.mariadb` takes the same keys as the initialization options; the workspace catalog is read again when `catalogFiles` changed, the documents that did not set a `connection` of their own through `mariadb/documentContext` move to the new `connection`, and a changed `sqlMode` or `serverVersion` applies to the open documents that did not set their own; a changed `catalogRefreshSeconds` applies to the connections already read, counted from their last read |
| `textDocument/didSave` | saving one of the workspace catalog's files reads the catalog again |
| `workspace/didChangeWatchedFiles` | a changed file that open documents include is read again for their outlines; a changed workspace catalog file reads the catalog again. When the client allows dynamic registration (`workspace.didChangeWatchedFiles.dynamicRegistration`), the server registers a watcher for `**/*.sql` after `initialized` |

Diagnostics carry `source: "mariadb"` and a `code`: `syntax-error`, `not-checked`,
`unterminated`, `splitter` or `source`. Syntax errors and an unterminated string, identifier or
comment are errors (severity 1); statements not checked and splitter messages are
warnings (2). For `source`, see [SOURCE includes](#source-includes). An unsupported `sql_mode` is reported once, as information (3), at the
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
| `keywordCase` | `"upper"` | `"lower"` completes keywords and built-in functions in lower case, and writes the FROM and JOIN a column brings in lower case |
| `indentation` | `"editor"` | one level of indentation for formatting and for the lines completion adds: `"tab"`, `"2 spaces"` or `"4 spaces"`; `"editor"` takes the formatting request's `tabSize`/`insertSpaces`, and for completion a tab when the line's indentation has tabs, else two spaces |
| `lineWidth` | `80` | characters; the JOIN a column brings starts a line of its own when the line would grow past this, and formatting keeps lines within it where a break can; 0 = no limit |
| `indentClauses` | `true` | formatting puts the lines of a query's or DML statement's clauses (FROM, WHERE, SET, ...) one level deeper than its first line; `false`: at its level. The FROM completion adds follows it |
| `indentRoutineClauses` | `true` | the same for the statements in a routine's body |
| `completionAddsTables` | `true` | completing a column of a table the query does not have adds the table (FROM, or a JOIN) and offers the schema's tables to join; `false`: the column only |

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
| `mariadb/dbCall` | server to client, request | `--mcp` only: `{connection, tool, arguments}`, a call of one `db.*` tool of MariaDB Shell's MCP server on the connection URI; the result is the tool's result as the client decoded it, or an error whose `data.permanent` says another call would fail alike (a connection that is not configured). See [MCP mode](#mcp-mode) |
| `window/showMessage` | server to client | a connection that cannot be used ("connection not configured", a failed login, an SSH URI, a URI holding a password), once per connection and message until it is read; a `connection` that is not a URI, which is refused (and never shown as given, since it may hold a password) |

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

**From a schema to a column.** In a select list with no table to take columns from
(`SELECT |` with no FROM), completion leads to a table one name at a time: the current
schema's tables first when there is one (a `USE`, `currentSchema`, or the schema of the
connection's URI), then the schemas, user schemas before `information_schema`, `mysql`,
`performance_schema` and `sys`. A schema or table is inserted with a dot after it (`shop.`,
`customer.`) and carries the command `editor.action.triggerSuggest`, so the next list
opens at once. Picking a column of a table that is not in scope adds that table:

- the qualifiers typed become an alias, the table's first letter, numbered when taken
  (`c`, `c2`): `SELECT shop.customer.name` becomes `SELECT c.name FROM shop.customer AS c`,
  the schema left out when it is the current one;
- with no FROM yet, `FROM <table> AS <alias>` goes after the select list, on a line of its
  own;
- with one, ` LEFT OUTER JOIN <table> AS <alias> ON ...` goes after its tables, on the first
  foreign key between the new table and a table in scope (either direction, in the order
  the tables are listed); with none, through a table that has a foreign key to each (n:m:
  ` LEFT OUTER JOIN enrollment AS e ON e.student_id = s.id LEFT OUTER JOIN course AS c ON
  e.course_id = c.id`), looked for in the schemas of both; only when there is no such table
  either, ` CROSS JOIN <table> AS <alias>`;
- a table already in scope is only renamed to its alias.

`completionAddsTables: false` turns all of this off. The added clauses follow the statement's layout. A FROM always starts a line of its own:
one level deeper than the first line of the query's statement (`indentClauses`,
`indentRoutineClauses` in a routine's body), at the indentation of the line holding the
query's `SELECT` for a query in parentheses or with the setting off. A JOIN stays on the
line, one space apart, unless the statement already spans lines or the line would pass
`lineWidth` (the column being inserted counts, taken at the table's longest column name);
then it starts a line of its own one level deeper than the FROM line, or at the indentation of the last line of the
tables when its joins are on lines of their own; each JOIN of an n:m pair gets its own line, and an `ON` that would pass
the width goes on the next line one level deeper (`indentation`; by default two spaces, a
tab when the indentation has tabs). Lines end in `\r\n` when the statement's do. The keywords follow `keywordCase`.
Only the inserted text is placed; text after the insert point (`;`, ` WHERE ...`) stays
where it is, after the last inserted line. Moving existing text is formatting.

The edits are `additionalTextEdits`. One that would insert exactly where the item's own text
ends (FROM after the last select item) is applied before the item by VS Code, so for a
client that takes snippets it is part of the item's text instead (`name$0 FROM ...`, the
caret after the column), with `insertTextMode` 1 (as is) when that text has a line break,
since VS Code would otherwise indent it again. The FROM is added only when the whole statement parses, so the
FROM clause after the cursor is known.

Once a table has an alias, the select list offers its columns qualified (`c.name`), the
first table's first, then the other tables of its schema to join, those with a foreign
key to or from a table in scope first.

## Formatting

`textDocument/formatting` and `rangeFormatting` lay out queries, the statements built
around them, table definitions, routines, access control and REST SQL, when they parse
whole: `SELECT`, `WITH` (or a parenthesized query), `EXPLAIN`/`ANALYZE` of a query or DML
statement, `INSERT`/`REPLACE` (`... SELECT`, `... VALUES`, `... SET`), `UPDATE`,
`DELETE`, `CREATE`/`ALTER VIEW`, `CREATE TABLE` with its columns or `... AS SELECT`,
`ALTER TABLE`, `CREATE PROCEDURE`/`FUNCTION`/`TRIGGER`/`EVENT`, `ALTER
EVENT`/`PROCEDURE`/`FUNCTION`, compound statements (`BEGIN NOT ATOMIC`, `IF`, `CASE`,
the loops), `GRANT`, `REVOKE`, `CREATE`/`ALTER USER`, and MariaDB Shell's REST SQL
(`CREATE`/`ALTER REST SERVICE`, `SCHEMA`, `VIEW`, `PROCEDURE`, `FUNCTION`,
`CONTENT SET`/`FILE`, `AUTH APP`, `USER`, `ROLE`, ...). Every other statement
(`CREATE TABLE ... LIKE`, the rest of DDL, `SET`, `CALL`, ...), any statement that holds a
versioned comment (`/*!NNNNN`), and `INSERT ... VALUES`/`SET` over 64 KB, or any of
them when the text formatted is over 4 MB, are left as they are, so the data and
versioned parts of dumps keep their own layout (a dump's CREATE TABLEs, already one
column per line, only get their keywords recased); a scan of the first words skips those
without parsing them. Routines in `sql_mode=ORACLE` are not parsed, so not laid out. Inside a routine's body every statement
is laid out, by the rules below. A range formats each
statement it touches, whole. `keywordCase` sets the keywords' case, `lineWidth` the
width, and `indentation` one level of indentation, by default the request's
`tabSize`/`insertSpaces` (a tab when `insertSpaces` is false). A line comment that ends
a clause keyword's line puts what follows one level deeper than the keyword
(`SELECT -- c`, then the items indented on the next line).

```sql
SELECT f.title, c.name AS category, count(r.rental_id) AS rentals,
       sum(p.amount) revenue
  FROM film f
    JOIN film_category fc ON fc.film_id = f.film_id
    LEFT JOIN inventory i ON i.film_id = f.film_id
  WHERE f.rating IN ('PG', 'PG-13') AND f.length > 90
  GROUP BY f.title, c.name
  ORDER BY revenue DESC
  LIMIT 20;
```

- **Clauses** (FROM, WHERE, GROUP BY, HAVING, WINDOW, ORDER BY, LIMIT, INTO, FOR UPDATE,
  UNION and its kin) stay on one line when the query fits there and the statement was on
  one line; otherwise each starts a line of its own, one level deeper than the
  statement's first line, and what they hold (joins, AND chains) deeper from there. With
  `indentClauses` (`indentRoutineClauses` in a routine's body) off, they start at the
  statement's indentation instead. This covers queries, INSERT, REPLACE, UPDATE, DELETE,
  views and a cursor's query; the other statements keep their kept line breaks one level
  deeper either way.
- **Other statements** follow the same rules for their clauses: the query of an
  `INSERT ... SELECT` and of a view starts a line of its own under the header
  (`INSERT INTO t (a, b)`, `CREATE VIEW v AS`); `SET`, `ON DUPLICATE KEY UPDATE`,
  `RETURNING`, `WITH CHECK OPTION`, and an `UPDATE`'s or `DELETE`'s WHERE, ORDER BY and
  LIMIT are clauses; `DELETE FROM t` keeps FROM on its line, while the FROM or USING
  after a multi-table DELETE's tables starts one. The tables of an `UPDATE` or `DELETE`
  join as a FROM clause's do.
- **Tables:** `CREATE TABLE` puts each column, key and constraint on a line of its own
  one level deeper, whatever its length, and the closing parenthesis and the table
  options on the next line (`) ENGINE=InnoDB`), filling it up to the width and continuing
  one level deeper; the partitioning on a line after that, its partitions (and their
  subpartitions) one per line like the columns. A `[AS] SELECT` after the columns starts
  a line of its own, the query and its clauses one level deeper (`) AS`, then the query),
  as without columns; with `indentClauses` off, at the statement's indentation.
  `ALTER TABLE` with several changes puts each on a line one level deeper; a single
  change stays on the statement's line. Inside a column the words keep their spacing.
- **Routines:** the header stays on its line (`CREATE PROCEDURE p(IN a INT)`, a
  function's `RETURNS` too), its parameters as well when they fit, else one per line
  one level deeper with `)` back at the header's level. The characteristics
  (`DETERMINISTIC`, `READS SQL DATA`, `COMMENT ...`) and an event's clauses
  (`ON SCHEDULE`, `ON COMPLETION`, `ENABLE`, `COMMENT`) go one level deeper, each on a
  line of its own, when the statement breaks. A `BEGIN ... END` body starts a line at
  the header's level (an event's after `DO`, `DO BEGIN`); a body of one other statement
  stays on the header's line when the routine fits there, else goes one level deeper.
- **Blocks:** in `BEGIN ... END`, `IF`, `CASE`, `LOOP`, `WHILE`, `REPEAT` and `FOR`
  each declaration and statement goes on a line of its own one level deeper, laid out on
  its own by these rules (its clauses break when it does not fit or was on several
  lines); `END`, `ELSEIF`, `ELSE` and `UNTIL ... END REPEAT` come back to the block's
  level, a CASE's `WHEN` arms go one level deeper with their statements one more. When an
  `IF`, `ELSEIF`, `WHEN` or `WHILE` condition breaks, `THEN` or `DO` starts a line of its
  own at the keyword's level, and so does `END REPEAT` after an `UNTIL` condition that
  breaks. A handler's block starts on its line
  (`DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN`), a cursor's query like a view's, and a
  `FOR` loop's `IN (SELECT ...)` like a subquery. One empty line between statements (and
  between table columns) stays, none right after `BEGIN`, `THEN` or `(` or before an
  `ALTER TABLE`'s first change; comments before
  `END` or `ELSE` stay with the statements before them.

```sql
CREATE PROCEDURE p(IN a INT)
  READS SQL DATA
BEGIN
  DECLARE n INT DEFAULT 0;
  IF a > 0 THEN
    UPDATE film_text
      SET title = 'x'
      WHERE film_id = a;
  ELSE
    SET n = -1;
  END IF;
END
```
- **Lists** of select items, GROUP BY and ORDER BY items, `SET` and `ON DUPLICATE KEY
  UPDATE` assignments, the column lists of INSERT and CREATE VIEW, INSERT's rows
  (`VALUES (1, 'a'), (2, 'b'), ...`), privileges and users continue after the comma on the
  same line up to the width, then on the next, aligned with the first item. A row on one
  line is written as it is, its keywords recased.
- **Access control:** `GRANT`/`REVOKE` put `ON`, `TO`/`FROM`, `REQUIRE` and `WITH` on lines
  of their own when the statement breaks; `CREATE`/`ALTER USER` its `REQUIRE`, `WITH`
  and `PASSWORD`/`ACCOUNT` clauses.
- **REST SQL** is laid out as MariaDB Shell's `SHOW CREATE REST ...` writes it: the header
  on its line; `ON SERVICE ... SCHEMA ...`, `AS ...` (with its `CLASS` and `{`), `FROM`,
  `VENDOR`, `NEW REQUEST PATH`, `PARAMETERS`, each `RESULT`, `[BINARY] CONTENT` and each
  option one level deeper on lines of their own when the statement breaks (always, with
  a block); `{ }` blocks (a class's fields, `OPTIONS`/`METADATA` JSON, JSON arrays) one entry
  per line, one level deeper, the brace closing at its line's level; `AUTHENTICATION`'s
  `PATH`, `REDIRECTION`, ... one level deeper still. Request paths, names, vendors and
  JSON's `true`/`false`/`null` keep their case.
- **Joins** go on lines of their own one level under FROM when the query breaks; a join
  in parentheses stays inline when it fits. `ON` stays on the JOIN's line, or goes one
  level deeper on the next line when that line would pass the width.
- **AND and OR** chains that do not fit break before each operator, one level deeper
  than the clause or JOIN they belong to.
- **Subqueries** stay inline when they fit; otherwise the opening parenthesis ends the
  line, the subquery's clauses follow one level deeper, and the closing parenthesis
  gets a line of its own.
- **Kept as they are:** names (a keyword used as a name keeps its case), strings, the
  spacing inside expressions (`x=1` stays `x=1`; runs of blanks become one), and line
  breaks inside other constructs (CASE, function arguments), which continue one level
  deeper. A comment stays before the token after it: at the end of its line when it
  ended one, on a line of its own when it had one.
- **Safety:** a layout that would not keep exactly the statement's tokens and comments,
  or would not parse again, is dropped and the statement left as it is.

Formatting a layout again changes nothing: a statement on several lines keeps its
clauses apart, which is also what makes completion's inserted FROM and JOIN agree with
the formatter (FROM where the clause settings put it, JOIN one level under FROM, `ON` on
its line). Completion's inserts take two
spaces (a tab when the statement's indentation has tabs) as the level; formatting takes
the editor's `tabSize`.

## SOURCE includes

MariaDB Schema Management (MSM) scripts break their sections out into files and include
them with `SOURCE '<path>'[<start>:<end>];`, which msm_plugin's `prepare_release` replaces
by the file's text before it cuts a release. The server reads them the same way, so the
outline of such a script holds its own objects and those of the files it includes, in its
context: a section without a `USE` gets the script's schema.

- A directive is a line `SOURCE` (upper case), a path in `'` or `"`, an optional slice and
  `;`; the rest of the line goes with it. It is matched as text, so also inside a routine
  body or a comment. A relative path is relative to the document's folder (an untitled
  document resolves absolute paths only).
- The slice selects part of the file: none or `[:]` is the whole file; a bound is a
  character offset as in a Python slice (code points, `\r\n` counting as one, negative from
  the end) or a line number `lnN` (1-based, the end line included, `ln-1` the last line).
  `[ln4:]` drops a three-line header.
- One level only, as msm: a directive inside an included text stays as it is.
- An included file is read from the editor when it is open there (unsaved edits count),
  else from disk. Editing it, closing it, or a `workspace/didChangeWatchedFiles` for it
  folds the outlines of the documents that include it again, in the background.
- A statement that is a directive is not parsed. A statement with directive lines in it
  (a procedure whose body includes a file) is parsed with those lines blanked. The
  statements of an included file are checked where that file is open, not in the
  documents that include it.
- Diagnostics with code `source`, on the directive: a file that cannot be read (error, on
  the path), a malformed slice such as `ln0` (error, on the slice), a slice that selects
  nothing or an empty file (warning), a directive in the included text that msm does not
  expand (warning). They come from the document's outline, folded in the background after
  each check; after an edit their positions may be off by it until the next one.
- Navigation spans the includes: definition goes into the included file, hover names it,
  and references and rename cover the document and every file it includes (each file
  once, also when it is included twice), searched in the expanded text so each name is
  resolved in the script's context. Requested from an included file while an open script
  includes it (the one whose state it takes, below), they run in that script the same
  way, the request moved into its expanded text; with no such script, or at a place the
  slice leaves out, they cover the file alone.
- An included file opened on its own takes the state of an open script that includes it,
  where the included text begins: its `sql_mode`, delimiter (`;` inside a routine body)
  and schema in use, unless the client set them through `mariadb/documentContext`. With
  several such scripts, the first by URI. A section with no `USE` then has its objects in
  the script's schema, and one included under `DELIMITER $$` is split at `$$`. When no
  open script includes it any more, it goes back to its own.
- In a directive's path, completion lists the folders and `.sql` files there, folders
  first, and goes on into a folder that is chosen.
- `mariadb/outline` lists the includes and gives objects from them a `source` and
  positions in their file ([outline-format.md](outline-format.md)). `mariadb-lsp check` and
  `outline` expand directives too; `--no-source` reads them as SQL.
- `--source-dir DIR` (`check`, `outline`, `format`) is the folder relative SOURCE paths are
  taken from, in place of the file's own; stdin (`-`) has none otherwise, so MariaDB Shell's
  MCP `sql` tools pass their script's folder this way. A DIR that is not a folder, or the
  option with another command, is a usage error (exit 2); `format` only checks it, and
  `--no-source` still wins.

## Live connections

A connection is named by its URI, in MariaDB Shell's syntax
(`[scheme://][user@]host[:port][/schema][?options]`, or a socket path), and compared
in the MCP plugin's normal form: scheme `mariadb` when none, no password, the host in
lower case, port 3306 added. The password is read from MariaDB Shell's secret store,
directly, through its helper program (`mariadb-secret-store-<helper> get|list`, JSON on
stdin and stdout): the GUI's entry (`GUI:CONN:<uri>`), then the MCP server's
(`MCP:CONN:<uri>`, and the older `...:Connection:` keys), then one the shell stored
when it connected itself (secret type `password`, `user@host:port`). A URI holding a
password is refused (`connection`, `documentContext`, the CLI's `--connection`): it
would end up in settings files, shell histories, `ps` and the client's message traces.
The CLI's `-p` asks for the password on the terminal instead. The helper is the one
the shell would use: the variable `MARIADB_SHELL_CREDENTIAL_STORE_HELPER`, else
`credentialStore.helper` in the shell's `options.json`, else `keychain` (macOS),
`windows-credential` (Windows) or `login-path`. Helpers run one at a time, so none inherits another's pipes, and one that
has not finished after 60 s (a keychain prompt nobody answers) is killed.

Each connection has a thread of its own. It connects (5 s connect and 30 s read
timeouts, TLS when the server offers it, the URI's `ssl-mode`, `ssl-ca`, `ssl-cert`,
`ssl-key`), makes the session read-only, reads `@@sql_mode`, `VERSION()` and
`DATABASE()`, then schemas, tables, columns, indexes, foreign keys, views, routines
with parameters, triggers and events from `information_schema` (not
`performance_schema`), and disconnects. It does so when the connection is first named,
on `mariadb/refreshCatalog`, every `catalogRefreshSeconds`, and after a failure again
after 5 s, 10 s, 20 s, ... up to five minutes. Shutting down stops a reading between
its queries. The password is read from the store on every attempt, so one stored or
changed meanwhile is used by the next: a missing password is retried three times on the
schedule, then only when asked, and `mariadb/refreshCatalog` or a change of
`shellBinary`, `shellBinDir` or `credentialStoreHelper` tries at once. When the server refuses the login, the store is
read again at once and a changed password tried before the failure is reported; the
refused password is not tried again on the retry schedule (that could lock the account
under `max_password_errors`), only when it changes or on `mariadb/refreshCatalog`. An SSH
URI (`mariadb+ssh://`, not supported) and a URI holding a password are not retried. No
request waits for a connection: requests use the last catalog read. The password is held
only until the connect call returns, and never logged.

## MCP mode

With `--mcp`, the server does not connect to any database itself: it asks its client,
which has MariaDB Shell's MCP server (the VS Code extension starts one for its own
work), to call the `db.*` tools for it, one `mariadb/dbCall` request per call. The
client resolves the connection URI to the connection it has open on the MCP server, or
opens one, so the credentials stay with the client and no second MCP server runs. The
secret store, `--shell-bin-dir`, `shellBinary`, `shellBinDir` and `credentialStoreHelper`
are not used.

A reading makes these calls: `db.execute_sql` (`SELECT @@sql_mode, VERSION(),
DATABASE()`), `db.list_schemas`, then per schema `db.list_objects` for tables and views,
and for the user schemas (`schema_type` "User Schema") also procedures, functions,
triggers, events and sequences. It reads names only: the tables, views and routines of
the user schemas are stubs. `performance_schema` is left out, as it is in direct mode.

**Details on demand.** A completion, hover or signature help that meets a stub (the
columns of a table in scope or after `schema.table.`, a routine's parameters) has the
live catalog read its details, one `db.get_object_details` each, on the catalog's own
thread, and answers again with them once they are in, or after 3 s without them; no
thread waits meanwhile, so other requests are answered as usual. A completion answered
without them is marked incomplete, so the client asks again. A table's details give its columns,
primary and unique keys (`constraints`), its own foreign keys (the `n:1` `references`), and
the foreign keys of other tables pointing at it (`1:1` and `1:n`), which are kept on those
tables while they are stubs: a table related to one in use ranks as related before it is
read. Details read stay until the next reading; one that failed is not asked for again
until then. A call the client does not answer within 120 s fails the reading;
the retry schedule and `mariadb/refreshCatalog` are as in direct mode, and an error
marked `permanent` is not retried. Shutting down releases a reading waiting on the
client.

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
Debug and sanitizer ones. By hand:

```bash
MARIADB_LSP=bld/bin/mariadb-lsp python3 -m unittest discover -s tests/e2e -v
```

The live-connection tests deploy a throwaway server with MariaDB Shell
(`mariadb-shell -- sandbox deploy`, with the `mariadbd` on `PATH` or `MDBLS_TEST_MARIADBD`,
a binary or an installation directory), run their SQL through the shell, and keep a
private secret store through the shell's `mariadb-secret-store-login-path` helper, with a
login file of its own (`MYSQL_TEST_LOGIN_FILE`), never `~/.mylogin.cnf`. The shell is
found in `MDBLS_SHELL_BIN_DIR` (a CMake cache variable; unless set, looked up at every
configure: the directory of the `mariadb-shell` on `PATH`, else
`../mariadb-shell/build/bin`). Without the shell or a
`mariadbd`, they are skipped; without the shell, so are the unit tests of the secret
store. No MariaDB client tools are needed.

### Sanitizers and fuzzing

`-DWITH_ASAN=ON -DWITH_UBSAN=ON` builds the whole tree, the parser included, with
AddressSanitizer and UndefinedBehaviorSanitizer; ctest then runs every test under them,
the end-to-end tests too. Undefined behaviour aborts. The binaries carry their defaults
(`cmake/sanitizer_defaults.cc`): leaks are reported, on macOS as well, except two threads
detached on purpose: the stdin reader the server leaves when it exits with its input still
open, and libFuzzer's RSS monitor. `ASAN_OPTIONS`, `LSAN_OPTIONS` and `UBSAN_OPTIONS` override them. RapidJSON's
SIMD string scan is off in these builds: it reads the rest of an aligned 16-byte block past
a string's end, which ASan reports.

`-DWITH_FUZZERS=ON` (clang) turns both sanitizers on, instruments the tree for coverage
and builds the libFuzzer targets of `fuzz/`:

| Target | What it checks |
| --- | --- |
| `splitter_fuzz` | the statement index of any text: statements in order, inside the text, not overlapping, `find()` agreeing; the parallel split equal to the sequential one |
| `edit_fuzz` | a text and up to 64 edits: `Statement_index::update()` equal to `build()` after each, with the splice it reports; a `Document` given the same edits as LSP positions |
| `format_fuzz` | every statement checked by the parser; the whole text and a range formatted with options from the input, the result formatted again; the edits in order, inside the text |

Each is a ctest test (label `fuzz`) that runs for `MDBLS_FUZZ_SECONDS` (default 10) from
`tests/fixtures/sql` and the inputs earlier runs found, kept in `<build>/fuzz/corpus/`. A
failing input is written to `<build>/fuzz/artifacts/<target>/`; the target run on that file
reproduces it. The fuzz tests and the end-to-end tests share the ctest resource lock
`mdbls_timing`, so they run one at a time: fuzzing would keep cores busy while the
latency bounds are measured. A longer run by hand, four processes:

```bash
bld/bin/edit_fuzz -fork=4 -max_total_time=900 -max_len=8192 bld/fuzz/corpus/edit_fuzz tests/fixtures/sql
```

`.github/workflows/sanitizers.yml` builds so (clang, `-DWITH_FUZZERS=ON`) and runs the
tests and each fuzz target for a minute: on a push to `release/candidate`, as the release
build, and by hand on any branch (`gh workflow run sanitizers.yml --ref <branch>`). Pull
requests run `ci.yml` only.
