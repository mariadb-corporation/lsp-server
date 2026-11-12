# Outline JSON format

`mariadb-lsp outline FILE` writes the schema objects a SQL file defines as one JSON object.
The same data backs the language server's `mariadb/outline` request and
`textDocument/documentSymbol`, and will later back ALTER generation and EER diagrams. This
is format 1; a change that removes or renames a member increases the number, added members
keep it. The outline keeps every piece of schema information the SQL states, as MSM schema
projects store it as a schema snapshot. One change of meaning within format 1 (2026-11): a
column's `type` no longer holds `CHARACTER SET`/`CHARSET` and `COLLATE`; they are `charset` and
`collation`.

Members added within format 1 are listed as features: `mariadb-lsp outline --features` prints
`{"format": 1, "features": [...]}`, and `mariadb-lsp --help` has the same names on its
`outline features:` line. A binary before the list has no `--features` (it exits 2).

| Feature | What it adds |
| --- | --- |
| `view_reads` | `views[].reads` |
| `rest_auth_app_links` | `rest.services[].auth_apps`; `ALTER REST SERVICE` is applied |
| `rest_declared` | `declared` on REST services and REST schemas |
| `rest_drop_for_database` | `DROP REST SCHEMA ... FOR SCHEMA db` parses, and drops the REST schema only when its `schema` is `db` |
| `rest_current_schema` | `USE REST SCHEMA` is followed, and the service and schema a REST statement does not name are resolved as MariaDB Shell does, with a note where the shell fails |
| `rest_object_details` | a REST object's database object unquoted, data mapping, `PARAMETERS`/`RESULT` and options ([REST object details](#rest-object-details)); `CREATE REST VIEW`/`PROCEDURE`/`FUNCTION IF NOT EXISTS` keeps an object the file created |
| `rest_settings` | the settings of REST services and schemas ([REST settings](#rest-settings)); `ALTER REST SCHEMA` is applied; services with developers are told apart as MariaDB Shell does |
| `rest_statements` | `statements` on REST services, schemas, objects and content sets: every statement about each ([REST statements](#rest-statements)) |

## How the outline is built

The statements that can change the schema, those whose first word after any leading comments
is `CREATE`, `ALTER`, `DROP`, `RENAME` or `USE` (also inside a versioned comment), are parsed
and applied in file order: `CREATE` adds an object, `ALTER` changes it, `DROP` removes it,
`RENAME` renames or moves it, `USE` sets the schema that unqualified names belong to. REST
statements (`CREATE`/`DROP REST ...`, `ALTER REST SERVICE`, `USE REST`) go the same way. Other statements,
and comment-only statements, are not parsed. A `CREATE` of a name that exists replaces the
earlier object; `IF NOT EXISTS` keeps an existing database, table, view or sequence.

A statement with a syntax error, or in an sql_mode the parser does not support, is not applied
and is listed under `notes` (kind `syntax-error` or `unsupported-sql-mode`, see [Notes](#notes)).
So are an `ALTER TABLE` or `CREATE INDEX` on a table the file does not create and a `CREATE
TABLE ... LIKE t` of such a table (`unknown-table`); the `LIKE` statement is still applied and
creates the table. So are a REST statement that names no service or schema when none is in use
(`rest-none-in-use`), an `ALTER REST SERVICE` of a service the file does not create
(`unknown-rest-service`) and an `ALTER REST SCHEMA` of a schema it does not name
(`unknown-rest-schema`), a REST object statement of the wrong kind (`rest-object-kind`) and a
REST rename or plain `CREATE` onto a path in use (`rest-path-in-use`), see [REST objects](#rest-objects). A statement that parses but changes nothing (`CREATE`/`DROP USER`, `ROLE` or
`SERVER`, `CREATE PACKAGE`, an `ALTER` of anything but a database, view, table, procedure,
function, event or sequence, an `ALTER PROCEDURE`, `FUNCTION`, `EVENT` or `SEQUENCE` of an
object the file does not create, a `RENAME` of a table, view or sequence the file does not
create, a `USE REST` that names its service or follows one) is not applied and gets no note. `RENAME TABLE` renames or moves a
table, a view or a sequence, as MariaDB does; a statement that would move a view to another
schema renames nothing (MariaDB refuses it). `ALTER PROCEDURE`/`FUNCTION` changes the
characteristics it writes, `ALTER EVENT` what it writes (schedule, completion, status, comment,
body, definer; `RENAME TO` renames or moves the event), `ALTER SEQUENCE` the options it writes.

MSM `SOURCE '<path>'[<start>:<end>];` directives are expanded first, as msm_plugin's
`prepare_release` does ([language-server.md](language-server.md#source-includes)): the
included text is folded where its directive stands, in the script's context. A note on an
included statement has `source` and its `start` in that file, and its message starts with the
file and line (`140_tables.sql:13: ALTER TABLE missing: ...`); a file that cannot be read gets
a note at its directive (`include-unreadable`), and a directive in an included text, which msm
does not expand, one where it stands (`include-nested`). `mariadb-lsp outline --no-source`
does not expand them.

Names are as written, without quotes (`` `order` `` becomes `order`), and compare without
regard to case. Keywords (`timing`, `events`, `mode`, `on_delete`, `on_update` of a foreign
key, `match`, `using`, `order`, `algorithm`, `security`, `check_option`, `data_access`,
`status`, `generated_storage`, `generated_row`) are upper-cased; `kind`, `charset` and
`collation` values are lower-cased; the `body` of a routine, trigger or event is exactly as
written (line breaks, comments and blanks kept); everything else is as written, with runs of
blanks collapsed to one space. A `definer` is `user@host` with the quotes of both parts
removed (`` `root`@`localhost` `` and `root@'localhost'` give `root@localhost`),
`CURRENT_USER`, `CURRENT_ROLE`, or a role name; null when no `DEFINER` is written. Positions are LSP positions: a 0-based
`line` and a 0-based `character` counted in UTF-16 code units.

## Top level

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | number | `1` |
| `file` | string | the input file as given on the command line (`-` for stdin); for `mariadb/outline`, the document URI |
| `schemas` | array of schema | in order of first mention (a statement naming a schema, such as `ALTER TABLE s.t`, `CREATE INDEX ... ON s.t` or `CREATE TABLE ... LIKE s.t`, makes its entry); an undeclared schema holding no objects is left out, whatever its options |
| `rest` | object | MariaDB REST Service objects |
| `includes` | array | only when the file has SOURCE directives: one `{file, range, slice, error?}` per directive, in order; `file` is the path as resolved (absolute, `/` separators), `range` the directive, `slice` its `[...]` as written or null, `error` why nothing was included |
| `statements` | object | `ddl`: statements that may change the schema; `applied`: those the fold took (a `USE`, or a `DROP` of an object the file never created, counts; a `CREATE USER` or `USE REST` does not) |
| `notes` | array | `{kind, applied, source?, start: {line, character}, message}` for statements that were not read, or that refer to a table the file does not create: see [Notes](#notes) |

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

## Notes

Each entry of `notes` is a remark on one statement, in the order of the statements (a note on
an included statement where its directive stands):

| Member | Type | Meaning |
| --- | --- | --- |
| `kind` | string | one of the identifiers below; stable, so consumers match it rather than `message` |
| `applied` | boolean | whether the statement changed the outline nonetheless; `false` means the outline misses what it defines, which fails an MSM release |
| `source` | number | only for a statement in an included file: its index in `includes`; the directive is `includes[source].range` |
| `start` | position | where the statement begins: in the file at `source`, else in the input |
| `message` | string | for people; starts with `FILE:LINE: ` when `source` is set |

| Kind | `applied` | Meaning |
| --- | --- | --- |
| `syntax-error` | `false` | the statement has a syntax error and was not read |
| `unsupported-sql-mode` | `false` | the statement is in an sql_mode the parser does not support (`ORACLE`) and was not read |
| `unknown-table` | `false` for `ALTER TABLE` and `CREATE INDEX`, `true` for `CREATE TABLE ... LIKE` | the statement names a table the file does not create; the `LIKE` still creates its table, empty |
| `include-unreadable` | `false` | a `SOURCE` file cannot be read; the note is on the directive |
| `include-nested` | `false` | a `SOURCE` directive in an included text, which msm does not expand; the note is on that directive, in its file |
| `rest-none-in-use` | `false` | a REST statement names no service (or no schema) and none is in use, where MariaDB Shell fails ("No REST SERVICE specified.", "No REST schema given."); see [REST objects](#rest-objects) |
| `unknown-rest-service` | `false` | an `ALTER REST SERVICE` names a service the file does not create |
| `unknown-rest-schema` | `false` | an `ALTER REST SCHEMA` names a schema the file does not create or name |
| `rest-object-kind` | `false` | a `DROP REST VIEW`, `PROCEDURE` or `FUNCTION` names a path where the file has an object of another kind, where MariaDB Shell fails (with `IF EXISTS`, does nothing); or a `CREATE ... IF NOT EXISTS` of one kind finds an object of another kind there and does nothing, as in the shell; see [REST objects](#rest-objects) |
| `rest-path-in-use` | `false` | an `ALTER REST SERVICE` or `SCHEMA ... NEW REQUEST PATH` names the path of another service, or another schema of the service; or a plain `CREATE REST SERVICE`, `SCHEMA`, `CONTENT SET`, `VIEW`, `PROCEDURE` or `FUNCTION` (no `OR REPLACE`, no `IF NOT EXISTS`) names the path of one the file created; in an enabled service, where MariaDB Shell fails ("The request_path is already used by another entity.") |

A kind is never removed or renamed within format 1; new kinds may be added, so a consumer that
does not know a kind should go by `applied`.

An object defined in an included file has `source`, its index in `includes`, and its
`range` and `selection_range` are positions in that file. Objects of the file itself have
no `source`. An object's members (columns, keys) carry their own `source`.

## Schema

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string or null | null for objects whose schema the file does not name |
| `declared` | boolean | created by `CREATE DATABASE`/`SCHEMA` in the file; `range` and `selection_range` only then |
| `options` | object | `charset`, `collation` (lower-cased), `comment` (the literal's contents, quotes removed and escapes resolved), those given; `ALTER DATABASE` updates `charset` and `collation` only |
| `tables`, `views`, `routines`, `triggers` | arrays | see below |
| `events`, `sequences` | arrays | see below |

## Table

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string | |
| `temporary` | boolean | |
| `options` | object | `engine` (as written, quotes removed), `charset`, `collation` (lower-cased), `comment` (the literal's contents, quotes removed and escapes resolved); any other option under its lower-cased first keyword (`auto_increment`, `row_format`, `data` for `DATA DIRECTORY`, `index` for `INDEX DIRECTORY`, `union`, ...) with the value as written after the `=` or the keywords (`UNION=(a, b)` gives `(a, b)`); an engine-defined option (`PAGE_COMPRESSED=1`) under its lower-cased name; `ALTER TABLE ... CONVERT TO CHARACTER SET` sets `charset`, and `collation` when `COLLATE` follows. `WITH SYSTEM VERSIONING` is `system_versioning`, not an option |
| `columns` | array | see [Column](#column) |
| `indexes` | array | see [Index](#index) |
| `foreign_keys` | array | `name` (the `CONSTRAINT` name when one is written, else the name after `FOREIGN KEY`, null when neither is), `index_name` (the name after `FOREIGN KEY`, or null), `columns`, `references: {schema, table, columns}` (`schema` null when the reference is unqualified; the `USE` schema is not filled in), `match` (`FULL`, `PARTIAL`, `SIMPLE`, or null), `on_delete`, `on_update` (`CASCADE`, `RESTRICT`, `SET NULL`, `NO ACTION`, `SET DEFAULT`, or null) |
| `checks` | array | `name` (null when unnamed), `expression` (as written, blanks collapsed), `column` (the column of a column-level check such as `j JSON CHECK (JSON_VALID(j))`, whose `name` is null; null for a table-level check) |
| `partitioning` | string or null | the `PARTITION BY ...` clause as written, blanks collapsed, partition definitions included; `ALTER TABLE ... PARTITION BY` replaces it, `REMOVE PARTITIONING` makes it null |
| `system_versioning` | boolean | `WITH SYSTEM VERSIONING` as a table option; `ALTER TABLE ... ADD SYSTEM VERSIONING` sets it, `DROP SYSTEM VERSIONING` clears it and drops the `AS ROW START`/`END` columns and the `SYSTEM_TIME` period |
| `periods` | array | `{name, start, end}` of each `PERIOD FOR`: `PERIOD FOR SYSTEM_TIME (s, e)` has the name `SYSTEM_TIME`, `PERIOD FOR p (s, e)` the name `p`; `ALTER TABLE ... ADD PERIOD` / `DROP PERIOD` change them |

A column declared `PRIMARY KEY` or `UNIQUE` appears as an unnamed index too; a column in the
primary key is not nullable. `SERIAL` gives `type` `SERIAL`, `nullable` false, `auto_increment`
true and no index. A second `PRIMARY KEY` replaces the first; a `CREATE INDEX` or a named `KEY`
with the name of an existing index replaces it.

`ALTER TABLE ... MODIFY` and `CHANGE` replace the column with the new definition, all members
as for `CREATE TABLE`; the column's own check goes with it (a new `CHECK` in the definition
replaces it, none removes it). `CHANGE` to a new name and `RENAME COLUMN` rename the column in
the `columns` and `key_parts` of indexes, the `columns` of foreign keys, the `column` of checks
and the periods (expressions are not rewritten). `ALTER COLUMN ... SET DEFAULT` / `DROP
DEFAULT` change `default` only; `DROP COLUMN` removes the column's own check too, and the column from the `key_parts` of its
indexes (an index left without key parts goes with it, as on the server). `ALTER INDEX
i [NOT] IGNORED` sets `ignored`. `ADD INDEX`, `ADD KEY`, `ADD CONSTRAINT ... FOREIGN KEY` and
`CREATE INDEX` fill every member, as in `CREATE TABLE`.

### Column

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string | |
| `type` | string | as written, blanks collapsed, without `CHARACTER SET`/`CHARSET`, `ASCII`, `UNICODE` and `COLLATE` (those are `charset` and `collation`): `VARCHAR(45)`, `TINYINT UNSIGNED`, `INT UNSIGNED ZEROFILL`, `ENUM('a','b')`; `SERIAL` for a `SERIAL` column. The attributes `BINARY` (`CHAR(3) BINARY`, also the MySQL form `COLLATE BINARY`, which leaves `BINARY`), `BYTE`, `COMPRESSED`, `UNSIGNED`, `SIGNED` and `ZEROFILL` stay in it |
| `nullable` | boolean | |
| `default` | string or null | the expression as written |
| `auto_increment` | boolean | |
| `comment` | string or null | the literal's contents, escapes resolved |
| `charset` | string or null | lower-cased; `ASCII` gives `latin1` and `UNICODE` `ucs2`, as the server reads them |
| `collation` | string or null | lower-cased; from `COLLATE` in the type or as a column attribute; `COLLATE DEFAULT` gives `default` |
| `generated` | string or null | the expression of a generated column (`AS (expr)`), parentheses included |
| `generated_storage` | string or null | `VIRTUAL`, `PERSISTENT` or `STORED` as written; null when not written (the server's default is `VIRTUAL`) or when the column is not generated |
| `generated_row` | string or null | `START` for `AS ROW START`, `END` for `AS ROW END` (system-versioned tables), else null |
| `on_update` | string or null | the `ON UPDATE` expression as written: `CURRENT_TIMESTAMP(6)` |
| `invisible` | boolean | the `INVISIBLE` attribute |
| `system_versioning` | boolean or null | `true` for `WITH SYSTEM VERSIONING`, `false` for `WITHOUT SYSTEM VERSIONING`, null when not written |
| `options` | object | engine-defined attributes (`ENCRYPTED=YES`) under their lower-cased name, the value as written |

### Index

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string or null | null when unnamed |
| `kind` | string | `primary`, `unique`, `index`, `fulltext`, `spatial`, `vector` |
| `columns` | array of strings | the column name of each key part, without prefix length or ordering |
| `key_parts` | array | `{column, length, order, expression}` per key part: `length` the prefix length (`a(10)` gives 10) or null, `order` `ASC`/`DESC` or null when not written, `expression` always null (MariaDB's grammar has no expression key parts; the member is there for when it does) |
| `using` | string or null | `BTREE`, `HASH` or `RTREE`, from `USING` or `TYPE` before or after the key parts |
| `comment` | string or null | the literal's contents, escapes resolved |
| `ignored` | boolean | `IGNORED` (true) or `NOT IGNORED` / not written (false) |
| `without_overlaps` | string or null | the period of `UNIQUE (..., p WITHOUT OVERLAPS)` |
| `options` | object | `key_block_size`, `parser` (`WITH PARSER`), engine-defined options under their lower-cased name; values as written |

`INVISIBLE` on an index is MySQL syntax, a syntax error for MariaDB.

## View, routine, trigger, event, sequence

| Object | Members |
| --- | --- |
| view | `name`, `columns` (the explicit column list, or empty), `definition` (the `SELECT` as written, blanks collapsed, with its `WITH ... CHECK OPTION` when written), `algorithm` (`UNDEFINED`, `MERGE`, `TEMPTABLE`), `security` (`DEFINER`, `INVOKER`), `definer`, `check_option` (`LOCAL`, or `CASCADED` for `WITH [CASCADED] CHECK OPTION`); the last four null when not written; `reads` (see below) |
| routine | `name`, `kind` (`procedure`, `function`), `parameters: [{mode, name, type}]` (`mode` `IN`, `OUT`, `INOUT`, or null when not written; `type` is everything after the name to the end of the parameter, so with a `DEFAULT` clause when written), `returns` (the `RETURNS` type of a function; null for a procedure; upper-cased for a UDF), `body` (exactly as written, from the first word of the body to its end; null for a UDF), `deterministic` (true for `DETERMINISTIC`, false for `NOT DETERMINISTIC`, null when not written), `data_access` (`CONTAINS SQL`, `NO SQL`, `READS SQL DATA`, `MODIFIES SQL DATA`, or null), `security` (`DEFINER`, `INVOKER`, or null), `definer`, `comment` (the literal's contents, or null), `aggregate` (`CREATE AGGREGATE FUNCTION`), `soname` (a UDF's `SONAME` library, else null) |
| trigger | `name`, `timing` (`BEFORE`, `AFTER`), `events` (`INSERT`, `UPDATE OR DELETE`, ...), `table` (the table name, without schema), `update_columns` (the columns of `UPDATE OF a, b`, else empty), `body` (exactly as written), `order` (`{kind, trigger}` for `FOLLOWS t` / `PRECEDES t`, `kind` upper-cased, or null), `definer`; the trigger belongs to the schema of its qualified name, else to the table's |
| event | `name`, `schedule` (`AT ...` or `EVERY ... [STARTS ...] [ENDS ...]` as written, blanks collapsed), `on_completion_preserve` (true for `ON COMPLETION PRESERVE`, false for `ON COMPLETION NOT PRESERVE`, null when not written), `status` (`ENABLE`, `DISABLE`, `DISABLE ON SLAVE`, or null), `comment` (the literal's contents, or null), `body` (exactly as written), `definer` |
| sequence | `name`, `type` (`AS INT UNSIGNED` gives `INT UNSIGNED`), `start`, `increment`, `minvalue`, `maxvalue`, `cache` (the numbers as written; null when not written, and `NO MINVALUE`/`NOMINVALUE`, `NO MAXVALUE`/`NOMAXVALUE` give null too, the server's default; `NOCACHE` gives `"0"`), `cycle` (true for `CYCLE`, false for `NOCYCLE`, null when not written), `options` (table options such as `ENGINE`, as for a table) |

### A view's `reads`

`reads` lists what the view's query reads, as `[{kind, schema, name}]`:

| `kind` | What counts |
| --- | --- |
| `table_or_view` | every table or view named in `FROM`, `JOIN`, `TABLE t`, in subqueries and scalar subqueries anywhere, and in the bodies of common table expressions; not a common table expression's name where it is in scope (an element of a `WITH` without `RECURSIVE` sees only the elements before it), not an alias, not a column's qualifier |
| `function` | a call of a function that is qualified (`crm.f(x)`), or not qualified and not the name of a built-in function (`f(x)`); calls with grammar of their own (`CONCAT`, `CAST`, ...) and unqualified built-in names are not reads |
| `sequence` | the sequence of `NEXTVAL(s)`, `NEXT VALUE FOR s`, `LASTVAL(s)`, `PREVIOUS VALUE FOR s`, `SETVAL(s, ...)`; not a `table_or_view` |

`schema` is the qualifier as written, or null when the name is not qualified; `name` is
unquoted, its case as written. Entries are unique by `(kind, schema, name)`, in text order;
`[]` for a view that reads nothing. `reads` is an array on every view read from SQL (only a
statement that parses makes a view); a reader can take its presence as the feature:
binaries before it have no `reads` member.

## REST objects

`rest` is `{services, auth_apps, roles, users}`. A service is `{path, declared, range,
selection_range, schemas, content_sets, auth_apps}`; a schema is `{path, schema, declared,
range, selection_range, objects}` where `schema` is the database schema it exposes, null when
the file has no `CREATE REST SCHEMA` for that path; `declared` is true when the file has a
`CREATE REST SERVICE` (`CREATE REST SCHEMA`/`DATABASE`) for it, false for one a statement only
names (`ON SERVICE /shared`), which a release must not drop. An object is `{kind, path,
object, range, selection_range}` with `kind` one of `view`, `procedure`, `function` and `object` the qualified database object as written,
quotes kept, blanks collapsed. `content_sets`, `auth_apps`, `roles` and `users` are arrays of
`{name, range, selection_range}`; a content set's name is its request path, a user's is
`user@app`. A service's `auth_apps` are the names of the auth apps linked to it, unquoted, its
case as in the `ADD AUTH APP` that linked it, its range that of the last statement that linked
it. A `CREATE REST SCHEMA` without a request path has `/` and the database schema's name, as
in MariaDB Shell. Paths and names compare without regard to case, as on the server. A service
named with developers (`mike, anna@/svc`) is the same service however its developers are
quoted, spaced or ordered, as in MariaDB Shell; its `path` is as the statement that made it
wrote it.

`ADD AUTH APP` links an app, `REMOVE AUTH APP` (with or without `IF EXISTS`) unlinks it, in
`CREATE` and `ALTER REST SERVICE`. A `CREATE OR REPLACE REST SERVICE` of a service the file has
updates it in place, as MariaDB Shell does: its schemas, content sets and the links the
statement does not name stay. `CREATE REST SERVICE IF NOT EXISTS` of a service
the file creates changes nothing, as does `CREATE REST SCHEMA IF NOT EXISTS` of such a schema;
of one a statement only named, it creates it. A plain `CREATE` (no `OR REPLACE`, no `IF NOT
EXISTS`) of a service, schema, content set or object the file created fails in MariaDB Shell
(its metadata triggers: "The request_path is already used by another entity."), where the
service is enabled: the statement is not applied and gets a `rest-path-in-use` note. A plain
`CREATE` of one a statement only named creates it. `ALTER REST SERVICE ... NEW REQUEST PATH` renames
the service. `ALTER REST SCHEMA [p] [ON SERVICE s] [NEW REQUEST PATH q] [FROM db]` renames the
schema and changes the database schema it exposes; one of a schema the file does not create or
name is not applied and gets an `unknown-rest-schema` note. A rename onto the path of another
service, or of another schema of the service, fails in MariaDB Shell: the statement is not
applied (nor its other clauses) and gets a `rest-path-in-use` note. As in the shell, only
enabled services count: a service that is (or the statement makes) `DISABLED` neither blocks a
rename nor is blocked, and the schemas of such a service do not block each other. `DROP REST AUTH APP`
also unlinks the app from every service.

The service and schema a statement does not name are resolved as MariaDB Shell's MRS DDL
executor does (`mysqlshdk/libs/mrs/ddl_executor*.cc`): only `USE REST SERVICE` sets the
current service, and it clears the current schema; `USE REST SCHEMA` (or `USE REST SERVICE s
SCHEMA p`) sets the current schema; the first schema a `CREATE REST SCHEMA` makes on the
current service becomes the current schema. Creating a service does not make it current. A
statement with `ON SERVICE` or `ON SCHEMA` (`FROM ...` for a `DROP`) uses what it names, the
current service when it names only a schema; a `CREATE`/`DROP REST VIEW`, `PROCEDURE` or
`FUNCTION` without one uses the current service and schema. Where there is none, the shell
fails, and the statement is not applied and gets a `rest-none-in-use` note. A `DROP` removes
the object at that path from that one schema; where the object there is of another kind (a
`DROP REST VIEW /v` of a procedure `/v`), the shell fails (with `IF EXISTS` it does nothing), and
the statement is not applied and gets a `rest-object-kind` note; a `DROP` of something the file never
created, or on a service it never created, changes nothing. `DROP REST SCHEMA p [FROM SERVICE
s] FOR SCHEMA db` (`FOR DATABASE` is the same) removes `p` only when its `schema` is `db`, names
compared without regard to case, or when the file only named `p` (its `schema` is null);
otherwise it changes nothing, and is still applied, with no note. `DROP REST SERVICE` or `SCHEMA` of
the current one clears it.

An object is its request path in its schema, of any kind, as in MariaDB Shell. A `CREATE OR
REPLACE REST VIEW`, `PROCEDURE` or `FUNCTION` at a path where the file has an object replaces
it with the statement's definition, also one of another kind; a plain `CREATE` there fails (see
above), except in a disabled service.
`CREATE ... IF NOT EXISTS` at such a path changes nothing; when the object there is of another
kind, the statement gets a `rest-object-kind` note (it is very likely a mistake).

### REST statements

With feature `rest_statements`, every service, schema, object and content set has
`statements`: each statement about it, in file order, so that a program can remove an item or
replace its statement without parsing SQL. An entry is:

| Member | Type | Meaning |
| --- | --- | --- |
| `kind` | string | `create`, `alter`, `use` or `drop` |
| `range`, `selection_range`, `source` | | the statement (from its first keyword, before its delimiter, without comments) and the item's name in it, as an item's own; `source` for a statement in an included file |
| `leading_comments` | range \| null | the comment lines right above the statement, with no blank line between, in its file; null when there are none, or when they reach an `-- MSM Section` marker line (they are then the section's banner) |
| `shared` | boolean | the statement is also another item's: `USE REST SERVICE s SCHEMA p` is listed under `s` and under `p` |
| `auth_apps_only` | boolean | an `ALTER REST SERVICE` whose only clauses are `ADD` / `REMOVE AUTH APP` |

A statement is the item's when it creates (each `CREATE`, also one a later `CREATE OR REPLACE`
replaced, or an `IF NOT EXISTS` that changed nothing), alters, uses or drops it (also a `DROP
REST SCHEMA ... FOR SCHEMA db` that dropped nothing). Naming an item as a place (`ON SERVICE`,
`ON SCHEMA`) does not make a statement that item's. An item lists its own statements only: a
service's schemas, content sets and the objects of its schemas list theirs. The list follows the
item across a `DROP` and a new `CREATE` and across a rename (`NEW REQUEST PATH`): it holds the
statements written under the earlier path too. A rename onto the path of an item dropped
earlier does not take that item's statements (nor those of the items that were under it):
they are no item's then. An object's statements are those at its path,
of any kind: a `CREATE REST VIEW IF NOT EXISTS /v` that found a procedure `/v` is the
procedure's. A statement not applied because it names no service or schema, or one the file
does not have (`rest-none-in-use`, `unknown-rest-service`, `unknown-rest-schema`), is no item's;
one that fails on an item (`rest-object-kind`, `rest-path-in-use`) is that item's.

### REST settings

With feature `rest_settings`, services and schemas also have the members below, all of them
always there, null where not written. A `CREATE` (also `OR REPLACE`, in place) sets every
setting to what it writes, as MariaDB Shell writes the service or schema anew; an `ALTER`
changes only what it writes. Where a statement writes an option more than once, the last one
counts.

A service:

| Member | Type | Meaning |
| --- | --- | --- |
| `request_path` | string | the request path, unquoted (`/svc`) |
| `developers` | string[] | the developers before `@`, unquoted, sorted as MariaDB Shell does (without regard to case); `[]` for none |
| `enabled` | boolean \| null | `ENABLED` / `DISABLED` |
| `published` | boolean \| null | `PUBLISHED` / `UNPUBLISHED` |
| `protocol` | string \| null | `PROTOCOL HTTP` or `HTTPS` |
| `comment` | string \| null | `COMMENT` |
| `auth_path`, `auth_redirection`, `auth_validation`, `auth_page_content` | string \| null | `AUTHENTICATION PATH`, `REDIRECTION`, `VALIDATION`, `PAGE CONTENT`; null for `DEFAULT` |
| `options`, `merge_options`, `metadata` | | as for objects, below |

A schema:

| Member | Type | Meaning |
| --- | --- | --- |
| `enabled` | string \| null | `ENABLED`, `DISABLED` or `PRIVATE` |
| `requires_auth` | boolean \| null | `AUTHENTICATION [NOT] REQUIRED` |
| `items_per_page` | number \| null | `ITEMS PER PAGE` |
| `comment` | string \| null | `COMMENT` |
| `options`, `merge_options`, `metadata` | | as for objects, below |

`MERGE OPTIONS` in a `CREATE` sets the options as written, as in MariaDB Shell. In an `ALTER`
it is merged into the options there (JSON merge patch, RFC 7396, as the server's
`JSON_MERGE_PATCH`; where the options are null, they become the patch as written) when the
file creates the service or schema, or wrote its whole `OPTIONS` since; otherwise the file does not know the options it patches, and `options` is the patch,
with `merge_options` true.

### REST object details

With feature `rest_object_details`, each object also has the members below. All of them are
always there; one that does not apply to the object's kind is null, `[]` or `false`. Names are
unquoted, as elsewhere; keywords and `@` options are upper case, without the `@`. Where the
statement writes an option more than once, the last one counts.

| Member | Type | Meaning |
| --- | --- | --- |
| `object_schema` | string \| null | the database object's schema, null when unqualified |
| `object_name` | string | the database object's name |
| `class` | string \| null | a view's `CLASS` name |
| `crud` | string[] | a view's `@` options after `CLASS` (`INSERT`, `UPDATE`, `DELETE`, `NOCHECK`, the `NO...` forms), as written |
| `fields` | Field[] \| null | a view's `{ ... }`; null when the statement has none (the server then makes one field per column), `[]` for `{}` |
| `parameters` | `{name, fields}` \| null | a routine's `PARAMETERS`; `name` is the class name written after the keyword, or null |
| `results` | `{name, fields}`[] | a routine's `RESULT`s in order (a function has at most one) |
| `enabled` | string \| null | `ENABLED`, `DISABLED` or `PRIVATE` |
| `requires_auth` | boolean \| null | `AUTHENTICATION [NOT] REQUIRED` |
| `items_per_page` | number \| null | `ITEMS PER PAGE` |
| `comment` | string \| null | `COMMENT` |
| `media_type` | string \| null | `MEDIA TYPE '...'` |
| `media_type_autodetect` | boolean | `MEDIA TYPE AUTODETECT` (then `media_type` is null) |
| `format` | string \| null | `FORMAT FEED`, `ITEM` or `MEDIA` |
| `auth_procedure` | string \| null | `AUTHENTICATION PROCEDURE`, `schema.name` or `name` |
| `options` | object \| null | `OPTIONS` / `MERGE OPTIONS`, as JSON |
| `merge_options` | boolean | `options` is a JSON merge patch to options the file does not know (see [REST settings](#rest-settings)). Always false for objects: a `CREATE` stores `MERGE OPTIONS` as the whole options, as MariaDB Shell does, and `ALTER REST VIEW`/`PROCEDURE`/`FUNCTION` is not applied |
| `metadata` | object \| null | `METADATA`, as JSON |
| `force` | boolean | a routine's `FORCE` |

A Field is one `key: value` pair of a `{ ... }`:

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string | the JSON key |
| `column` | string \| null | the column or parameter it maps (`schema.name` joined when written qualified); null for a reference |
| `reference` | `{schema, table, fields}` \| null | a pair with its own `{ ... }`: `key: [schema.]table @... { ... }`, `schema` null when unqualified. The join columns are not in the SQL |
| `options` | string[] | its `@` options in written order, but `DATATYPE` (`KEY`, `SORTABLE`, `NOFILTERING`, `NOCHECK`, `NOUPDATE`, `ROWOWNERSHIP`, `IN`, `OUT`, `INOUT`, `UNNEST`, ...) |
| `datatype` | string \| null | `@DATATYPE("...")`'s text; the argument of any other `@` option is dropped |
| `json_schema` | object \| null | `JSON SCHEMA {...}`, as JSON |

The JSON values the statement writes (`OPTIONS`, `METADATA`, `JSON SCHEMA`) are reported as
JSON: single-quoted strings become JSON strings, `.5` becomes `0.5`, a leading `+` goes.
