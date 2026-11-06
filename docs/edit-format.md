# Schema edit JSON format

`mariadb-lsp edit` applies structured schema edits to a SQL script and the files its MSM
`SOURCE` directives include, and writes what changes as one JSON object: text edits per
file, the DDL that migrates a deployed schema, and the outline of the edited texts. It
writes no file. MSM (`msm.edit_schema` of mariadb-shell-plugins) runs it for the VS Code
extension's schema diagrams: it checks the include paths, applies the text edits, appends the
migration to the next release's update script and keeps the returned outline as the new
snapshot.

This is format 1, for the request and the output alike. A change that removes or renames a
member increases the number; added members, ops and kinds keep it.

```bash
mariadb-lsp edit --source-dir <project>/development --edits REQUEST.json \
                 [--indent tab|2|4] [--line-width N] - < script.sql
```

The script comes on stdin (`-`) or as a FILE; relative `SOURCE` paths are taken from
`--source-dir` (default: the FILE's folder, none for stdin). Directives are expanded as for
`mariadb-lsp outline` ([language-server.md](language-server.md#source-includes)): slices,
line numbers, one level. msm blanks MSM Section 160 in the script it passes, its lines kept,
so the positions are those of the file. `--indent` and `--line-width` lay out the statements
an edit writes (none of the renames writes one).

Exit 0 with the result, 1 with an error object when the request is refused, 2 for a usage
error or an unreadable script or request.

## Request

```json
{"format": 1, "schema": "mariadb_rest_service", "edits": [
  {"op": "rename_table", "table": "url_host", "new_name": "url_hosts"},
  {"op": "rename_column", "table": "url_hosts", "column": "name", "new_name": "host_name"}
]}
```

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | number | `1` |
| `schema` | string or null | the outline schema the names belong to; objects are looked up there, then among the objects of the part of the script that names no schema (before the first `USE`), as msm's snapshot takes both; null for a script without `USE` |
| `edits` | array | applied in order, each to the result of the ones before (rename a table, then a column of it under the new name) |

Names compare without regard to case, as in the outline. A new name is at most 64
characters, does not end in a space and holds no NUL.

The ops that add or change an object carry it in the outline's shape
([outline-format.md](outline-format.md)): a `Column`, `Table`, `View` or `Routine` object
with the members the outline gives it. A member left out takes the outline's default
(`nullable` true, `default` null, `comment` null, ...), so `modify_column` with only `name`
and `type` drops the column's comment. An unknown member is `bad-request`, naming it; the
positions an outline object carries (`range`, `selection_range`, `source`) are ignored, so an
object read from an outline can be sent back. The ops of the renames do not check for
unknown members.

### Ops

| op | Members | Status | What changes |
| --- | --- | --- | --- |
| `rename_table` | `table`, `new_name` | done | the definition and every reference, a `DROP TABLE [IF EXISTS]` of it included |
| `rename_column` | `table`, `column`, `new_name` | done | the definition, also when an `ALTER TABLE` adds the column, and every reference |
| `rename_view` | `view`, `new_name` | done | the definition and every reference, a `DROP VIEW [IF EXISTS]` of it included |
| `rename_routine` | `routine`, `new_name`, `kind` (`procedure` or `function`; needed when both kinds have the name) | done | the definition and every reference, a `DROP PROCEDURE [IF EXISTS]` (or `FUNCTION`, by its kind) of it included |
| `add_table` | `table` (Table) | done | a `CREATE TABLE` after the schema's last one |
| `drop_table` | `table` | done | its `CREATE TABLE`, every `ALTER TABLE`, `CREATE INDEX` and `DROP INDEX` on it, its triggers, and `DROP TABLE` statements of it alone |
| `add_column` | `table`, `column` (Column), `after` or `first` | done | an element of the `CREATE TABLE`, or an `ALTER TABLE ... ADD COLUMN ... AFTER` (below) |
| `modify_column` | `table`, `column` (its name now), `new_column` (Column) | done | the column's definition; a different `new_column.name` is also a rename, references included |
| `drop_column` | `table`, `column` | done | its definition, its key parts in every index (an index left with none goes) and the `ALTER TABLE` changes of it |
| `set_table_options` | `table`, `options` (`{name: value or null}`) | done | the `CREATE TABLE`'s options: a value replaces or adds one, null removes it |
| `add_view` | `view` (View) | done | a `CREATE VIEW` after the schema's last view, else its last table |
| `drop_view` | `view` | done | its `CREATE VIEW`, `ALTER VIEW` and `DROP VIEW` statements of it alone |
| `add_routine` | `routine` (Routine) | done | a `CREATE PROCEDURE` or `FUNCTION` after the schema's last routine (below) |
| `drop_routine` | `routine`, `kind` (as for `rename_routine`) | done | its `CREATE`, `ALTER` and `DROP` statements of it alone |
| `add_index` | `table`, `index` (Index: `columns`, or `key_parts` for lengths and order) | done | an element of the `CREATE TABLE` (below) |
| `drop_index` | `table`, and `name`, `kind: "primary"`, or `columns` (an unnamed index) | done | its element, `ALTER TABLE` change or `CREATE INDEX`, or a column's `PRIMARY KEY` / `UNIQUE` |
| `set_primary_key` | `table`, `columns` | done | the primary key's columns where it is written; a column's `PRIMARY KEY` becomes an element; added when there is none |
| `add_foreign_key` | `table`, `foreign_key` (Foreign key) | done | an element of the `CREATE TABLE`, or an `ALTER TABLE ... ADD` (below) |
| `drop_foreign_key` | `table`, and `name` or `columns` (an unnamed key) | done | its element, `ALTER TABLE` change, or a column's `REFERENCES` |

An op this binary does not do is refused with `unsupported`. `mariadb-lsp --help` lists the
ops the binary does on the line `edit ops: rename_table rename_column ...` under `edit`; a
binary with `edit` but without that line does the four renames.

A rename finds its references as the language server's rename does: every statement of the
expanded script that spells the name is parsed and each name in it resolved in the
script's context (its schema in use, aliases, the tables in scope), so `h.name` of `FROM
url_host h` is a reference to `url_host.name` and `name` of another table is not. That
covers DDL (foreign keys, indexes, `ALTER TABLE`, `CREATE INDEX`, `RENAME TABLE`, triggers'
`ON`, the `DROP ... IF EXISTS` an idempotent script runs before a `CREATE`), DML, views, routine and trigger bodies, `GRANT`s, and statements in versioned
comments, in the script and in every included file (a file included twice is edited once).
Each reference is written as it was: in backticks or double quotes when it was, else with
quotes only when the new name needs them.

### Adding and removing

New text is written in the style of the statement it goes next to: names in backticks
when that statement's own name is, else quoted only when needed; keywords in the case of
its first keyword; the file's line breaks. A new statement is laid out by the formatter
(`--indent`, `--line-width`), ends with the delimiter in force where it goes (`$$` in a
`DELIMITER $$` block), and has as many blank lines before it as the statement it follows
has above it (a banner's comment lines not counted); no banner is written.

- **An MSM script** (one with `-- MSM Section NNN: ...` marker lines) keeps its sections: a
  table goes into Section 140 (after its last table there, else at the end of the section),
  a view or routine into Section 150, which every deployment runs again (below). Other
  scripts follow the rules below without sections.
- **A table** goes after the `CREATE TABLE` of the schema's last table (in the order of the
  expanded script), in that statement's file; to the end of the script when the schema has
  none. Its keys and foreign keys go into it; a foreign key's table must exist (or be the
  table itself) and its columns too (`unknown-object`), its name must be free in the schema
  (`name-taken`). No index is written for a foreign key: the server makes one when no index
  serves it. Each column's `type` must be a MariaDB data type (the built-in ones, `INET4`,
  `INET6`, `UUID`, `VECTOR`) and each definition must parse, else `bad-request` with a
  message starting with the column (`column \`price\`: ...`).
- **A column** goes after `after`, first with `first`, else after the table's last column:
  as an element of the `CREATE TABLE` after its last column (before the keys), laid out as
  its neighbours are (a line of its own, the comma moved). When that column is one an
  `ALTER TABLE ... ADD COLUMN` adds, the column goes into a new `ALTER TABLE t ADD COLUMN c
  ... AFTER x` after that statement, so the outline shows it where it was asked for.
- **`modify_column`** replaces the column's definition where its last definition is (the
  `CREATE TABLE`'s element, or an `ALTER TABLE ... MODIFY` or `CHANGE`); a column-level
  `PRIMARY KEY` or `UNIQUE` and the column's own `CHECK` stay.
- **`set_table_options`** changes the `CREATE TABLE`'s options; an option an `ALTER TABLE`
  sets later wins over it, so the self-check refuses such an edit. Option names are the
  outline's (`engine`, `charset`, `collation`, `comment`, `auto_increment`, `row_format`,
  an engine-defined one under its own name); `comment` is the text, quoted when written.
- **A view** goes after the schema's last view, else after its last table; in an MSM script
  after its last view in Section 150, else at the end of Section 150 (after its last
  statement, or after its banner when it has none). It is written as its neighbouring view
  is: `CREATE OR REPLACE VIEW`, or with a `DROP VIEW IF EXISTS` before it. In Section 150 it is
  always one of the two, `CREATE OR REPLACE` unless the neighbour drops first. Its `definition`
  is the `SELECT` alone: `check_option` writes the `WITH ... CHECK OPTION` (the outline's
  definition holds it, so remove it before sending a view of the outline back). A
  definition that is empty or does not parse is `bad-request`.
- **A routine** goes after the schema's last routine of either kind, inside its
  `DELIMITER` block; where `;` is the delimiter and the routine holds one, in a new block
  (`DELIMITER $$`, or the delimiter the script uses elsewhere). In an MSM script: after its
  last routine in Section 150, else its last view there, else at the end of Section 150. It is
  written as the routine it follows is: with `DROP PROCEDURE|FUNCTION IF EXISTS` before it, or
  `CREATE OR REPLACE`; in Section 150 always one of the two, the `DROP` unless the neighbour
  uses `OR REPLACE`. Blank lines before it are counted above the neighbour's `DROP`. The
  migration holds the `CREATE` alone. A function needs `returns`.
  Its name must be free among the routines of its kind (`name-taken`); a procedure and a
  function may share a name, as in MariaDB.
- **A statement removed** (with the `DROP ... IF EXISTS` of the object before it, which is
  also removed) goes with its lines, with the comment lines right above it when
  one of them names the object (a Workbench banner), and the blank lines above it, as
  adding put them there (those below when nothing is above it). So adding an object and
  dropping it again gives the files back as they were.
- **A key** goes into the `CREATE TABLE`: a primary key after the last column, another index
  after the last index (else the last column), a foreign key after the last foreign key (else
  the last index, else the last column). A table whose `CREATE TABLE` has no column list gets
  an `ALTER TABLE t ADD ...` after it. A foreign key to a table the script creates later goes
  into an `ALTER TABLE t ADD CONSTRAINT ...` after the last `ALTER TABLE` that adds a foreign
  key after both tables, else right after the referenced table. No index is written for a
  foreign key: the server makes one when none serves it; send `add_index` before it (a 1:1
  relationship's `UNIQUE`) when one should be written. Column types are not compared.
- **`set_primary_key`** with the columns the key has already changes nothing. Its migration is
  one statement, `DROP PRIMARY KEY, ADD PRIMARY KEY (...)`: a `DROP PRIMARY KEY` of its own
  fails when an `AUTO_INCREMENT` column is the key. Use `drop_index` and `add_index` for key
  parts with a length or an order.
- **An index still needed** cannot be dropped or replaced (`referenced`): one a foreign key of
  the table relies on (its columns lead the index) or the referenced columns of another
  table's foreign key, when no other index serves; the one an `AUTO_INCREMENT` column leads
  (MariaDB errors 1553 and 1075). Undo drops the foreign key first.
- **Dropping** a table that another table's foreign key references, or a column a foreign
  key uses (of its table or of another), is `referenced`, unless an earlier edit of the
  request removed that key or table: undo sends the inverse ops in reverse order. The last
  column of a table cannot be dropped (`bad-request`).

## Output

```json
{
  "format": 1,
  "files": [{"file": "sections/140-10_tables.sql",
             "edits": [{"range": {"start": {"line": 18, "character": 27},
                                  "end": {"line": 18, "character": 37}},
                        "new_text": "`url_hosts`"}]}],
  "migration": [{"edit": 0, "sql": "RENAME TABLE `url_host` TO `url_hosts`"}],
  "notes": [{"edit": 0, "kind": "name-in-string", "file": "sections/150-40_audit_log_triggers.sql",
             "source": 7, "start": {"line": 840, "character": 8}, "message": "..."}],
  "outline": {"format": 1, "file": "-", "schemas": [...], ...}
}
```

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | number | `1` |
| `files` | array | `{file, edits}` for each file that changes, the script first, then the included files in the order they are first included; `file` is `-` for stdin (else the script as given), an included file's path relative to `--source-dir` with `/` separators |
| `files[].edits` | array | `{range, new_text}`: LSP ranges (0-based line, UTF-16 character) in that file's text as it is on disk, not the sliced or expanded text; sorted and not overlapping, so they can be applied back to front |
| `migration` | array | `{edit, sql}`: the statements that take a deployed schema of the old shape to the new one, in edit order (see [Migration](#migration)) |
| `notes` | array | `{edit, kind, file, source?, start, message}`: what an edit could not do itself, for a person to check; they do not refuse the edit (see [Notes](#notes)). `file` as in `files`; `source` the index in the outline's `includes` of the file's first directive (left out for the script); `start` a position in that file's text on disk |
| `outline` | object | the outline ([outline-format.md](outline-format.md), format 1) of the edited texts, as `mariadb-lsp outline` of the edited files gives it; `file` as in `files` |

When an include changes length before a directive's positive end bound (`[89:1200]`), the
directive's slice is rewritten in the script so that it selects the same statements, as one
more edit of the script: the bound keeps its form (a character offset or `lnN`, its sign).
An edit before a positive start bound moves that bound too (a file included twice with
different slices). Negative bounds (`[695:-115]`) and open ends (`[89:]`) need nothing for
an edit inside the selection.

## Migration

No schema qualifier (MSM scripts run after `USE`), no `DELIMITER` and no trailing `;`: MSM
runs the statements inside a stored procedure. Names are in backticks.

| op | Statements |
| --- | --- |
| `rename_table`, `rename_view` | `RENAME TABLE a TO b` (views share the tables' namespace) |
| `rename_column` | `ALTER TABLE t RENAME COLUMN a TO b` |
| `rename_routine` | `DROP PROCEDURE a` (or `FUNCTION`), then the routine's `CREATE` from the edited script, its included text expanded; with the note `routine-recreated` |
| `add_table`, `add_view` | the `CREATE TABLE` or `CREATE VIEW` as written into the file |
| `add_routine` | the `CREATE PROCEDURE` or `FUNCTION` as written; with the note `routine-created` |
| `drop_table`, `drop_view`, `drop_routine` | `DROP TABLE t`, `DROP VIEW v`, `DROP PROCEDURE p` (or `FUNCTION`) |
| `add_column` | `ALTER TABLE t ADD COLUMN c ...`, with `FIRST` or `AFTER x` when the request gave one |
| `modify_column` | `ALTER TABLE t MODIFY COLUMN c ...`, or `CHANGE COLUMN old new ...` when renamed; when foreign keys (of the table or of others) use the column, `ALTER TABLE x DROP FOREIGN KEY fk` for each before it and `ALTER TABLE x ADD CONSTRAINT fk FOREIGN KEY ...` after it, as MariaDB refuses to change such a column (error 1833); an unnamed key cannot be dropped by name (note `no-migration`) |
| `drop_column` | `ALTER TABLE t DROP COLUMN c` |
| `set_table_options` | `ALTER TABLE t ENGINE=... COMMENT='...'` with the options given a value; a removed option has no statement (note `no-migration`) |
| `add_index` | `ALTER TABLE t ADD [UNIQUE\|FULLTEXT\|SPATIAL\|VECTOR] INDEX name (...)`, `ADD PRIMARY KEY (...)` |
| `drop_index` | `ALTER TABLE t DROP INDEX name`, `DROP PRIMARY KEY`; an unnamed index has no statement (the server named it; note `no-migration`) |
| `set_primary_key` | `ALTER TABLE t DROP PRIMARY KEY, ADD PRIMARY KEY (...)`, or `ADD PRIMARY KEY (...)` when the table had none |
| `add_foreign_key` | `ALTER TABLE t ADD CONSTRAINT name FOREIGN KEY (...) REFERENCES ...` |
| `drop_foreign_key` | `ALTER TABLE t DROP FOREIGN KEY name`; an unnamed key has no statement (note `no-migration`) |

## Notes

| Kind | Meaning |
| --- | --- |
| `name-in-string` | a string literal holds the old name as a word (for a column: and its table's name), outside a `COMMENT`; it was not changed. Dynamic SQL in `PREPARE` or `EXECUTE IMMEDIATE`, or data such as an audit log's table name |
| `rest-reference` | a REST object (`CREATE REST VIEW ... AS t`, `CREATE REST PROCEDURE ... AS p`) names the renamed table, view or routine; its REST SQL was not changed |
| `routine-recreated` | no statement renames a routine: the migration drops it and creates it again, and a `CREATE PROCEDURE` or `FUNCTION` cannot run inside a stored procedure, so msm or a person must place it |
| `routine-created` | the same for a routine `add_routine` creates: its `CREATE` in the migration cannot run inside a stored procedure. msm treats it as `routine-recreated` |
| `still-referenced` | after a drop, a statement still refers to the dropped table, view, routine or column (a view's `SELECT`, a trigger or routine body, an `INSERT`); one note per statement |
| `no-migration` | the edit has no statement for part of it: an option removed by `set_table_options`, an unnamed index or foreign key dropped, an unnamed foreign key around a `modify_column` |

A kind is never removed or renamed within format 1; a consumer that does not know a kind
should show its `message`.

## Errors

A refused request writes no edits, whatever edits before the failing one did:

```json
{"error": {"edit": 1, "kind": "unknown-object", "message": "there is no table `customer`"}}
```

`edit` is the index of the edit that failed, null for the request as a whole.

| Kind | When |
| --- | --- |
| `bad-request` | the request is not JSON of format 1, an op is unknown, a member is missing, empty, unknown or of the wrong type, a new name is too long, a routine name is ambiguous without `kind`, a column's type is not a MariaDB data type, or a definition does not parse |
| `unsupported` | an op this binary does not do, or a column added to a table whose columns the script does not list (`CREATE TABLE ... AS SELECT`, `LIKE`) |
| `unknown-object` | the table, column, view or routine is not in the outline, nor the table or column a foreign key references, nor the column `after` names |
| `name-taken` | the new name is taken: by a table or view for a table or view (they share a namespace), by a column of the table, by a routine of the same kind, by an index of the table, by a foreign key of the schema; a second primary key (use `set_primary_key`); a change of case only is a rename |
| `referenced` | `drop_table` of a table another table's foreign key references; `drop_column` of a column a foreign key uses; `drop_index` or `set_primary_key` taking away the index a foreign key or an `AUTO_INCREMENT` column needs |
| `edit-outside-slice` | an edit of an included file would cross the bound of a directive's slice, lie outside every slice of the file, or leave a slice that cannot be written to select the edited text |
| `self-check` | the outline of the edited texts does not show the edit (the table under its new name, the old name gone from every foreign key, trigger and index; an added object where it was asked for; a dropped one gone), or has a statement it cannot read that the input's outline read (an unapplied note the input did not have) |

The self-check is the safety net, as the formatter's re-parse is: it rebuilds the outline
after every edit and refuses the request rather than return edits that would lose an object.
