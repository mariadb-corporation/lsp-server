# Outline JSON format

`mariadb-lsp outline FILE` writes the schema objects a SQL file defines as one JSON object.
The same data backs the language server's `mariadb/outline` request and
`textDocument/documentSymbol`, and will later back ALTER generation and EER diagrams. This
is format 1; a change that removes or renames a member increases the number.

## How the outline is built

The statements that can change the schema, those whose first word after any leading comments
is `CREATE`, `ALTER`, `DROP`, `RENAME` or `USE` (also inside a versioned comment), are parsed
and applied in file order: `CREATE` adds an object, `ALTER` changes it, `DROP` removes it,
`RENAME` renames or moves it, `USE` sets the schema that unqualified names belong to. REST
statements (`CREATE`/`DROP REST ...`, `USE REST SERVICE`) go the same way. Other statements,
and comment-only statements, are not parsed. A `CREATE` of a name that exists replaces the
earlier object; `IF NOT EXISTS` keeps an existing database, table, view or sequence.

A statement with a syntax error, or in an sql_mode the parser does not support, is not applied
and is listed under `notes` ("not read: the statement has a syntax error", "not read: sql_mode
X is not supported"). So are an `ALTER TABLE` or `CREATE INDEX` on a table the file does not
create ("ALTER TABLE t: the script does not create this table", "CREATE INDEX on table t, which
the script does not create") and a `CREATE TABLE ... LIKE t` of such a table ("CREATE TABLE ...
LIKE t, which the script does not create"); the `LIKE` statement is still applied and creates
the table. A statement that parses but changes nothing (`CREATE`/`DROP USER`, `ROLE` or
`SERVER`, `CREATE PACKAGE`, an `ALTER` of anything but a database, view or table, a `RENAME` of
a table the file does not create, `USE REST SERVICE`) is not applied and gets no note.

Names are as written, without quotes (`` `order` `` becomes `order`), and compare without
regard to case. Keywords (`timing`, `events`, `mode`, `on_delete`, `on_update`) are
upper-cased; `kind`, `charset` and `collation` values are lower-cased; everything else is as
written, with runs of blanks collapsed to one space. Positions are LSP positions: a 0-based
`line` and a 0-based `character` counted in UTF-16 code units.

## Top level

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | number | `1` |
| `file` | string | the input file as given on the command line (`-` for stdin); for `mariadb/outline`, the document URI |
| `schemas` | array of schema | in order of first mention (a statement naming a schema, such as `ALTER TABLE s.t`, `CREATE INDEX ... ON s.t` or `CREATE TABLE ... LIKE s.t`, makes its entry); an undeclared schema holding no objects is left out, whatever its options |
| `rest` | object | MariaDB REST Service objects |
| `statements` | object | `ddl`: statements that may change the schema; `applied`: those the fold took (a `USE`, or a `DROP` of an object the file never created, counts; a `CREATE USER` or `USE REST SERVICE` does not) |
| `notes` | array | `{start: {line, character}, message}` for statements that were not read, or that refer to a table the file does not create (see above), sorted by `start` |

Objects carry `range` and `selection_range`, each `{start: {line, character}, end: {...}}`:
`range` is the whole defining statement and `selection_range` the object's name as written,
quotes included. Exceptions: a column's `range` is its definition, also when added by `ALTER
TABLE`; an index or foreign key declared in a column has the column definition as `range`,
with the column name as `selection_range` for a `PRIMARY KEY` or `UNIQUE` and the definition
(with its `REFERENCES` clause) again for a foreign key; a table-level index or foreign key has
its definition as `range` and, when no name follows the key word (`PRIMARY KEY (...)`,
`CONSTRAINT c FOREIGN KEY (...)`), as `selection_range` too; a check has its definition as both;
a REST schema the file never declared (`CREATE REST VIEW ... ON SCHEMA /x` without a `CREATE
REST SCHEMA /x`) has the statement that first named it as both. Without both: an undeclared
schema, routine `parameters` entries, a foreign key's `references`, and `notes` entries
(`start` only).

## Schema

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string or null | null for objects whose schema the file does not name |
| `declared` | boolean | created by `CREATE DATABASE`/`SCHEMA` in the file; `range` and `selection_range` only then |
| `options` | object | `charset`, `collation` (lower-cased), `comment` (the literal's contents, quotes removed and escapes resolved), those given; `ALTER DATABASE` updates `charset` and `collation` only |
| `tables`, `views`, `routines`, `triggers` | arrays | see below |
| `events`, `sequences` | arrays | `{name, range, selection_range}` |

## Table

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string | |
| `temporary` | boolean | |
| `options` | object | `engine` (as written, quotes removed), `charset`, `collation` (lower-cased), `comment` (the literal's contents, quotes removed and escapes resolved); any other option under its lower-cased keyword (`auto_increment`, `row_format`, ...) with the value as written; `ALTER TABLE ... CONVERT TO CHARACTER SET` sets `charset` |
| `columns` | array | `name`, `type` (as written, blanks collapsed: `VARCHAR(45)`, `TINYINT UNSIGNED`; `SERIAL` for a `SERIAL` column), `nullable`, `default` (the expression as written, or null), `auto_increment`, `comment` (the literal's contents, escapes resolved, or null), `collation` (lower-cased, or null), `generated` (the expression of a generated column, or null) |
| `indexes` | array | `name` (null when unnamed), `kind` (`primary`, `unique`, `index`, `fulltext`, `spatial`, `vector`), `columns` (the column name of each key part, without prefix length or ordering; for an expression key part the first identifier in it, or the expression when it has none) |
| `foreign_keys` | array | `name` (null when unnamed), `columns`, `references: {schema, table, columns}` (`schema` null when the reference is unqualified; the `USE` schema is not filled in), `on_delete`, `on_update` (`CASCADE`, `RESTRICT`, `SET NULL`, `NO ACTION`, `SET DEFAULT`, or null) |
| `checks` | array | `name` (null when unnamed), `expression` (as written, blanks collapsed) |

A column declared `PRIMARY KEY` or `UNIQUE` appears as an unnamed index too; a column in the
primary key is not nullable. `SERIAL` gives `type` `SERIAL`, `nullable` false, `auto_increment`
true and no index. A second `PRIMARY KEY` replaces the first; a `CREATE INDEX` or a named `KEY`
with the name of an existing index replaces it.

## View, routine, trigger

| Object | Members |
| --- | --- |
| view | `name`, `columns` (the explicit column list, or empty), `definition` (the `SELECT` as written, blanks collapsed) |
| routine | `name`, `kind` (`procedure`, `function`), `parameters: [{mode, name, type}]` (`mode` `IN`, `OUT`, `INOUT`, or null when not written; `type` is everything after the name to the end of the parameter, so with a `DEFAULT` clause when written), `returns` (the `RETURNS` type of a function; null for a procedure) |
| trigger | `name`, `timing` (`BEFORE`, `AFTER`), `events` (`INSERT`, `UPDATE OR DELETE`, ...), `table` (the table name, without schema); the trigger belongs to the schema of its qualified name, else to the table's |

## REST objects

`rest` is `{services, auth_apps, roles, users}`. A service is `{path, range, selection_range,
schemas, content_sets}`; a schema is `{path, schema, range, selection_range, objects}` where
`schema` is the database schema it exposes, null when the file has no `CREATE REST SCHEMA` for
that path; an object is `{kind, path, object, range, selection_range}` with `kind` one of
`view`, `procedure`, `function` and `object` the qualified database object as written, quotes
kept, blanks collapsed. `content_sets`, `auth_apps`, `roles` and `users` are arrays of
`{name, range, selection_range}`; a content set's name is its request path, a user's is
`user@app`. A statement that names no service uses the one set by `USE REST SERVICE`. A
`DROP REST VIEW`, `PROCEDURE` or `FUNCTION` removes the object of that kind and path from the
schema named by `FROM [SERVICE ...] DATABASE`, or from every schema of the service when
none is named; a `DROP` on a service the file never created changes nothing.
