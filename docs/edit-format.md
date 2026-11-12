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

`mariadb-lsp edit --ops` prints the ops the binary does and exits 0, for programs (the
`edit ops:` line of `--help` lists the same, for people):

```json
{"format": 1, "ops": ["rename_table", "rename_column", ...], "merge_alters": true}
```

A binary that answers `--ops` also tags each migration statement (`action`, `object`, ...)
and answers `unsupported` for an op it does not know; older ones (#25 to #28) exit 2 there.
`merge_alters` is there when the binary understands the request member of that name (an
older one ignores it, as it ignores any member it does not know at the top level).

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
| `merge_alters` | boolean, optional | `true`: the `ALTER TABLE` statements of a table are merged into one where that runs as they do one after another ([Merged ALTER TABLE statements](#merged-alter-table-statements)); left out or `false`: the migration has one statement per change, as before |

Names compare without regard to case, as in the outline. A new name is at most 64
characters, does not end in a space and holds no NUL.

The ops that add or change an object carry it in the outline's shape
([outline-format.md](outline-format.md)): a `Column`, `Table`, `View`, `Routine`, `Trigger`, `Event` or `Sequence` object
with the members the outline gives it. A member left out takes the outline's default
(`nullable` true, `default` null, `comment` null, ...), so `modify_column` with only `name`
and `type` drops the column's comment. An unknown member is `bad-request`, naming it; the
positions an outline object carries (`range`, `selection_range`, `source`) and a view's
`reads` (derived from its `definition`) are ignored, so an object read from an outline can be
sent back. The ops of the renames do not check for
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
| `add_trigger` | `trigger` (Trigger: `name`, `timing`, `events`, `table`, `body`; `update_columns`, `order`, `definer` optional) | done | a `CREATE TRIGGER` after the schema's last trigger (below) |
| `drop_trigger` | `trigger`, `table` (optional: refused when the trigger is on another table) | done | its `CREATE` and `DROP` statements |
| `rename_trigger` | `trigger`, `new_name` | done | its `CREATE`, its `DROP TRIGGER [IF EXISTS]`, and the `FOLLOWS` / `PRECEDES` of other triggers |
| `add_event` | `event` (Event: `name`, `schedule`, `body`; `on_completion_preserve`, `status`, `comment`, `definer` optional) | done | a `CREATE EVENT` after the schema's last event (below) |
| `drop_event` | `event` | done | its `CREATE`, `ALTER` and `DROP` statements |
| `rename_event` | `event`, `new_name` | done | its `CREATE`, `ALTER EVENT` and `DROP EVENT [IF EXISTS]` statements |
| `add_sequence` | `sequence` (Sequence: `name`; `type`, `start`, `increment`, `minvalue`, `maxvalue`, `cache`, `cycle`, `options` optional) | done | a `CREATE SEQUENCE` after the schema's last sequence, else before its first table (below) |
| `drop_sequence` | `sequence` | done | its `CREATE`, `ALTER` and `DROP SEQUENCE` statements |
| `rename_sequence` | `sequence`, `new_name` | done | the definition and every reference (`NEXT VALUE FOR`, `NEXTVAL()`, `LASTVAL()`, `SETVAL()`, `PREVIOUS VALUE FOR`, `FROM s`, `RENAME TABLE`), as `rename_table` finds them |
| `rename_schema` | `schema`, `new_name` (both may hold MSM placeholders), `comments` (optional boolean) | done | every name that resolves to the schema (below); no migration |
| `replace_view` | `view` (View) | done | the view's last `CREATE VIEW`, rewritten where it stands; its `ALTER VIEW` statements after it go (below) |
| `replace_routine` | `routine` (Routine) | done | the routine's last `CREATE PROCEDURE` or `FUNCTION`, of its `kind`, where it stands; its `ALTER` statements after it go |
| `replace_trigger` | `trigger` (Trigger) | done | the trigger's last `CREATE TRIGGER`, where it stands |
| `replace_event` | `event` (Event) | done | the event's last `CREATE EVENT`, where it stands; its `ALTER EVENT` statements after it go |

An op this binary does not do, or does not know (one added after it was built), is refused
with `unsupported`. `mariadb-lsp edit --ops` lists the ops the binary does as JSON (see
above), and `mariadb-lsp --help` on the line `edit ops: rename_table rename_column ...` under
`edit`; a binary with `edit` but without that line does the four renames.

A rename finds its references as the language server's rename does: every statement of the
expanded script that spells the name is parsed and each name in it resolved in the
script's context (its schema in use, aliases, the tables in scope), so `h.name` of `FROM
url_host h` is a reference to `url_host.name` and `name` of another table is not. That
covers DDL (foreign keys, indexes, `ALTER TABLE`, `CREATE INDEX`, `RENAME TABLE`, triggers'
`ON`, the `DROP ... IF EXISTS` an idempotent script runs before a `CREATE`), DML, views, routine and trigger bodies, `GRANT`s, and statements in versioned
comments, in the script and in every included file (a file included twice is edited once).
Each reference is written as it was: in backticks or double quotes when it was, else with
quotes only when the new name needs them.

### Renaming the schema

`rename_schema` finds the schema's names as the other renames find theirs, through the
parser and the resolver: the name of `CREATE`, `ALTER` and `DROP DATABASE` or `SCHEMA`
(`IF [NOT] EXISTS` too), `USE`, every schema qualifier (DDL and its foreign keys, `CREATE
INDEX`, `RENAME TABLE`, the `DROP ... IF EXISTS` before a `CREATE`, the names of routines,
triggers, events and sequences, DML, views, routine, trigger and event bodies), `GRANT` and
`REVOKE ... ON s.*` and `ON s.t`, REST SQL's `CREATE`/`ALTER REST SCHEMA ... FROM s` and a
REST object's `AS s.o`, and statements in versioned comments, in the script and every
included file. A table, column or alias of the same spelling is left alone: in `SELECT
shop.x FROM t AS shop`, `shop` is the alias. Each name is written as it was, quoted only when
the new name needs it. The edit is refused with `unknown-object` when nothing names the
schema, and with `name-taken` when the outline has a schema of the new name.

**MSM placeholders.** A project's schema name may hold placeholders that msm substitutes,
`/*<msm:NAME>*/` with NAME matching `[A-Za-z_][A-Za-z0-9_]*`, written against the identifier
with nothing between (`/*<msm:schema_prefix>*/shop/*<msm:schema_postfix>*/`). `schema` and
`new_name` may hold them before and after the identifier. A name in the script matches when
the placeholders written against it are exactly those of `schema` (none when `schema` has
none): `/*<msm:schema_prefix>*/shop` and a bare `shop` are different schemas once msm has
substituted them. The match is rewritten as a whole, with `new_name`'s placeholders as
given and its identifier never quoted; a `new_name` with placeholders whose identifier
would need quotes is `bad-request`. A `new_name` with the identifier of `schema` but other
placeholders is `name-taken` when the script already names it.

**Comments**, with `"comments": true`: the name in backticks in a comment, by itself or as a
qualifier (`` `shop` ``, `` `shop`.`t` ``, not `` `t`.`shop` ``), as MSM's banners write it.
In backticks with placeholders, they must be those of `schema`, and the new name keeps
`new_name`'s; in backticks without placeholders, the new identifier is written without them
too. Comments are what lies between the tokens of the statements and between the statements,
in the text the directives include. Without `comments` no comment is changed.

String literals holding the name get `name-in-string` notes. The migration is empty: a
schema cannot be renamed in place, so renaming a deployed one is the caller's to build
(`CREATE DATABASE`, `RENAME TABLE` per table, the routines, ...). The request's `schema`
names the renamed schema by its new name for the edits after it, once the outline has no
schema of the old name left (renaming a bare `shop` leaves the one with placeholders).

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
- **A trigger** goes after the schema's last trigger, else its last routine, else its last
  view, else its last table, never before its own table's `CREATE TABLE` and never after the
  schema's first event (nor that event's `DROP EVENT IF EXISTS`); an **event**
  after the schema's last event, else its last trigger, routine, view or table. In an MSM script
  both go into Section 150: after the last object of that order there, else at the end of the
  section. They are written as their neighbour of their own kind is, dropped first or `CREATE OR
  REPLACE`, and in Section 150 always one of the two, the `DROP TRIGGER|EVENT IF EXISTS` unless
  the neighbour uses `OR REPLACE`; inside a `DELIMITER` block when the body holds `;`, as a
  routine. A trigger's `events` is the outline's string (`INSERT`, `UPDATE OR DELETE`), not an
  array; `update_columns` needs `UPDATE` among them. Its table must exist and be a table
  (`unknown-object`), as must the columns of its `update_columns` and the trigger its `order`
  names: on the same table, with the same timing, firing on every event of the new one (else
  MariaDB refuses it, error 4031). Trigger names and event names must be free in the schema
  (`name-taken`). A trigger that another trigger
  follows or precedes cannot be dropped (`referenced`): that one's `CREATE` fails without it.
  A trigger or event rename changes the statements about it by name (no reference search: SQL
  names neither elsewhere), with `name-in-string` notes as the other renames.
- **A sequence** goes after the schema's last sequence, else before its first table (its
  `DROP TABLE IF EXISTS` included), whose column defaults may take values from it: after the
  last statement above that table, else after the script's (or the section's) opening
  comments; in an MSM script in Section 140. Its numbers (`start`, `increment`, `minvalue`,
  `maxvalue`, `cache`) are strings of an integer, as the outline writes them; `cache` `"0"` writes
  `NOCACHE`. Sequences share the namespace of tables and views (`name-taken`). A sequence a
  `CREATE TABLE` or `ALTER TABLE` of another table names (a column default, also one a later
  statement changed) cannot be dropped (`referenced`): that statement fails without it
  (MariaDB error 1146).
- **A replaced view, routine, trigger or event** (`replace_*`, which msm sends for an object
  the project has, as `add_*` for a new one) must exist under its name, a routine under its
  `kind` too (`unknown-object`); a different name is not a rename (`rename_*` is), and the
  name is written as the script spells it, also when the request spells it in another case
  (view names may tell case apart). Its last
  `CREATE` is written anew where it stands, in its file (an included one too), with the
  layout and style of an add, and in its form: `CREATE OR REPLACE` stays one, `CREATE ... IF NOT
  EXISTS` too, a `CREATE` after its `DROP ... IF EXISTS` stays after it, one in a `DELIMITER` block stays in it, and
  a body that now holds `;` where `;` is the delimiter gets a block of its own, as an add
  does. An earlier `CREATE` of it stays: the last one decides. The `ALTER` statements of it
  after that `CREATE` (`ALTER VIEW`, which redefines the view, `ALTER PROCEDURE|FUNCTION`
  characteristics, `ALTER EVENT`) go, as a drop removes statements, with an `alter-removed`
  note each: the object sent is whole (one of the outline holds what they changed), and
  they would change it again. An event an `ALTER EVENT ... RENAME TO` named, or renamed
  after its last `CREATE`, is `unsupported`: its `CREATE` names it otherwise (`rename_event`
  it first); so is an object whose last `CREATE` a `DROP` of it follows (a `RENAME TABLE`
  named the view). A `CREATE` whose
  text is not one file's (a routine with a `SOURCE` in its body, as MRS's `restore_roles`) is
  `edit-outside-slice`. A trigger is
  checked as `add_trigger` checks one (it may keep its own name, and not follow or precede
  itself); its table's `CREATE TABLE` and the `CREATE` of the trigger it follows or precedes
  must come before its `CREATE` (`unknown-object`), and a
  trigger that follows or precedes it needs it on its table, with its timing and its events
  (`referenced`: that `CREATE` fails without, error 4031). Tables and sequences have no
  `replace_*`: their structured ops change them. Not checked: that the tables and views a
  replaced view's query reads are created before it (the edit succeeds, a fresh deployment
  fails with error 1146).
- **A statement removed** (with the `DROP ... IF EXISTS` of the object before it, which is
  also removed) goes with its lines, with the comment lines right above it when
  one of them names the object (a Workbench banner), and the blank lines above it, as
  adding put them there (those below when nothing is above it). A `DELIMITER` block left
  without statements goes too, its two `DELIMITER` lines with the blank lines above it. So
  adding an object and dropping it again gives the files back as they were.
- **A key** goes into the `CREATE TABLE`: a primary key after the last column, another index
  after the last index (else the last column), a foreign key after the last foreign key (else
  the last index, else the last column). A table whose `CREATE TABLE` has no column list gets
  an `ALTER TABLE t ADD ...` after it. A foreign key to a table the script creates later goes
  into an `ALTER TABLE t ADD CONSTRAINT ...` after the last `ALTER TABLE` that adds a foreign
  key after both tables, else right after the referenced table. No index is written for a
  foreign key: the server makes one when none serves it; send `add_index` before it (a 1:1
  relationship's `UNIQUE`) when one should be written. Column types are not compared.
- **A key without a name** (in `add_index`, `add_foreign_key`, `add_table`) is given one, so
  that the script and the server agree on it and a later drop can name it: an index its first
  column's name (`customer_id`, then `customer_id_2`, ... among the table's indexes, as the
  server names one), a foreign key `fk_<table>_<referenced table>` (then `_2`, ... among the
  schema's foreign keys); at most 64 characters. A primary key needs none (`PRIMARY`).
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
  "migration": [{"edit": 0, "sql": "RENAME TABLE `url_host` TO `url_hosts`",
                 "action": "rename", "object": "table", "name": "url_host", "new_name": "url_hosts"}],
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
| `migration` | array | `{edit, sql, action, object, name, table?, new_name?}`: the statements that take a deployed schema of the old shape to the new one, in edit order, each with what it does (see [Migration](#migration)); with `merge_alters`, an `ALTER TABLE` also has `edits`, `spec` and `clauses` |
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

Each statement says what it does, so a program need not read its SQL:

| Member | Values |
| --- | --- |
| `action` | `create`, `drop` (a table, view, routine, trigger, event or sequence); `add`, `drop` (a member of a table); `alter`; `rename` |
| `object` | `table`, `view`, `procedure`, `function`, `trigger`, `event`, `sequence`; `column`, `index`, `primary_key`, `foreign_key`, `table_options` |
| `name` | the object's name before the edit (for a `create` or `add`, the new object's); `PRIMARY` for a primary key; for `table_options`, the table's |
| `table` | a member's table (as named when the statement runs); left out for a table, view, routine, trigger, event or sequence |
| `new_name` | the name after a `rename`, or after an `alter` of a column that `CHANGE COLUMN` renames; left out otherwise |

| op | Statements | Tags (`action` `object`) |
| --- | --- | --- |
| `rename_table`, `rename_view` | `RENAME TABLE a TO b` (views share the tables' namespace) | `rename` `table` or `view` |
| `rename_column` | `ALTER TABLE t RENAME COLUMN a TO b` | `rename` `column` |
| `rename_routine` | `DROP PROCEDURE a` (or `FUNCTION`), then the routine's `CREATE` from the edited script, its included text expanded; with the note `routine-recreated` | `drop` `procedure` or `function` (the old name), then `create` of the same kind (the new name) |
| `add_table`, `add_view` | the `CREATE TABLE` or `CREATE VIEW` as written into the file | `create` `table` or `view` |
| `add_routine` | the `CREATE PROCEDURE` or `FUNCTION` as written; with the note `routine-created` | `create` `procedure` or `function` |
| `drop_table`, `drop_view`, `drop_routine` | `DROP TABLE t`, `DROP VIEW v`, `DROP PROCEDURE p` (or `FUNCTION`) | `drop` `table`, `view`, `procedure` or `function` |
| `add_column` | `ALTER TABLE t ADD COLUMN c ...`, with `FIRST` or `AFTER x` when the request gave one | `add` `column` |
| `modify_column` | `ALTER TABLE t MODIFY COLUMN c ...`, or `CHANGE COLUMN old new ...` when renamed; when foreign keys (of the table or of others) use the column, `ALTER TABLE x DROP FOREIGN KEY fk` for each before it and `ALTER TABLE x ADD CONSTRAINT fk FOREIGN KEY ...` after it, as MariaDB refuses to change such a column (error 1833); an unnamed key cannot be dropped by name (note `no-migration`) | `drop` `foreign_key` for each key dropped first, `alter` `column`, then `add` `foreign_key` for each added back |
| `drop_column` | `ALTER TABLE t DROP COLUMN c` | `drop` `column` |
| `set_table_options` | `ALTER TABLE t ENGINE=... COMMENT='...'` with the options given a value; a removed option has no statement (note `no-migration`) | `alter` `table_options` |
| `add_index` | `ALTER TABLE t ADD [UNIQUE\|FULLTEXT\|SPATIAL\|VECTOR] INDEX name (...)`, `ADD PRIMARY KEY (...)` | `add` `index` or `primary_key` |
| `drop_index` | `ALTER TABLE t DROP INDEX name`, `DROP PRIMARY KEY`; an unnamed index has no statement (the server named it; note `no-migration`) | `drop` `index` or `primary_key` |
| `set_primary_key` | `ALTER TABLE t DROP PRIMARY KEY, ADD PRIMARY KEY (...)`, or `ADD PRIMARY KEY (...)` when the table had none | `alter` `primary_key`, or `add` when the table had none |
| `add_foreign_key` | `ALTER TABLE t ADD CONSTRAINT name FOREIGN KEY (...) REFERENCES ...` | `add` `foreign_key` |
| `drop_foreign_key` | `ALTER TABLE t DROP FOREIGN KEY name`; an unnamed key has no statement (note `no-migration`) | `drop` `foreign_key` |
| `add_trigger`, `add_event` | the `CREATE TRIGGER` or `CREATE EVENT` as written; with the note `trigger-created` or `event-created` | `create` `trigger` or `event` |
| `drop_trigger`, `drop_event` | `DROP TRIGGER t`, `DROP EVENT e` | `drop` `trigger` or `event` |
| `rename_trigger` | `DROP TRIGGER a`, then the trigger's `CREATE` from the edited script, its included text expanded; with the note `trigger-recreated`. MariaDB fires a table's triggers of one timing and event in the order they were created, so the triggers the script creates after it on its table, at its timing, on an event it or one of them fires on, are dropped after it and created again after it, each with a `trigger-recreated` note | `drop` `trigger` (the old name, then those after it), then `create` `trigger` (the new name, then those after it) |
| `rename_event` | `ALTER EVENT a RENAME TO b`; none for a change of case (event names do not tell case apart) | `rename` `event` |
| `add_sequence` | the `CREATE SEQUENCE` as written | `create` `sequence` |
| `drop_sequence` | `DROP SEQUENCE s` | `drop` `sequence` |
| `rename_sequence` | `RENAME TABLE a TO b`, then for each column whose default takes values from it (in its definition, or set by `ALTER COLUMN c SET DEFAULT`) `ALTER TABLE t ALTER COLUMN c SET DEFAULT ...` with the default as the edited script writes it: a deployed default still names the old sequence after `RENAME TABLE` | `rename` `sequence`, then `alter` `column` for each such column |
| `rename_schema` | none | |
| `replace_view`, `replace_routine`, `replace_trigger`, `replace_event` | `CREATE OR REPLACE VIEW`, `PROCEDURE`, `FUNCTION`, `TRIGGER` or `EVENT` with the new definition, as written but `OR REPLACE` (the object is on a deployed server); a routine, trigger or event with the note `routine-created`, `trigger-created` or `event-created` | `create` `view`, `procedure`, `function`, `trigger` or `event` |

### Merged ALTER TABLE statements

With `"merge_alters": true` in the request, the `ALTER TABLE` statements of one table become
one statement, `ALTER TABLE t clause, clause, ...`, wherever that gives what running them one
after another gives, so that a table is altered (and perhaps copied) once. MSM runs each
`ALTER TABLE` as an online ALTER and adds `ALGORITHM` and `LOCK` itself; the migration never
has them. The text edits, notes and outline do not change.

A statement joins the last statement of its table when:

- only `ALTER TABLE` statements of other tables lie between them, and none of those depends
  on it nor it on them: neither is a statement of the other's table nor has a foreign key
  referencing (or, when dropped, having referenced) the other's table. Any other statement
  (`RENAME TABLE`, a `CREATE` or `DROP`, ...) in between keeps them apart, so the order
  between tables holds: around `modify_column`, another table's `DROP FOREIGN KEY` still runs
  before the column's change and its `ADD CONSTRAINT` after;
- none of its clauses conflicts with a clause already in that statement.

Two clauses conflict unless what both name is used in one of these ways (each checked on
MariaDB 12.3 against two statements, comparing `SHOW CREATE TABLE`, with
`foreign_key_checks` 1 and 0 and under each `ALGORITHM`/`LOCK` clause MSM adds: the merged
statement applies wherever the two do, or is refused with 1845 or 1846):

| First clause | Then | Example |
| --- | --- | --- |
| adds a column, or renames one to it | names it in a key or `AFTER` | `ADD COLUMN c INT, ADD INDEX ic (c)`; `RENAME COLUMN a TO c, ADD COLUMN d INT AFTER c` |
| changes a column in place (`MODIFY`) | names it in a key | `MODIFY COLUMN b BIGINT, ADD INDEX ib (b)` |
| names a column in a key or `AFTER` | names it in a key or `AFTER` | `ADD COLUMN c INT AFTER a, ADD COLUMN d INT AFTER a` |
| renames a column away | adds a column of the old name | `RENAME COLUMN a TO c, ADD COLUMN a INT` |
| drops an index or the primary key | adds one of the same kind and name | `DROP INDEX ia, ADD INDEX ia (a, b)`; `DROP PRIMARY KEY, ADD PRIMARY KEY (id, a)` |
| drops a foreign key | drops the index of its name | `DROP FOREIGN KEY fk, DROP INDEX fk` |

Clauses that name nothing in common merge, table options with anything among them. These stay
apart, as MariaDB refuses them in one statement or runs them differently: a column changed
twice (`MODIFY` and `MODIFY`, `MODIFY` and `ALTER COLUMN ... SET DEFAULT`), renamed and then
changed under either name, added and then dropped, dropped and then added again; a clause
that names a column by its old name after a rename; a key added and then dropped; a foreign
key dropped and then added again under its name (with `foreign_key_checks=0`, as MSM deploys,
MariaDB refuses that with error 1826 unless `ALGORITHM=COPY`), so around `modify_column` its
`ADD CONSTRAINT` is a statement of its own; a column `DEFAULT` that is not a literal, a
`CHECK` or a generated column next to a clause that renames or drops a column (an
expression naming the renamed column fails); a foreign key referencing its own table next to
a change of a column it references (errno 150). Column names compare without regard to case,
table names with it.

A statement made of several clauses is tagged `alter` `table` with the table's name, and lists
its clauses:

```json
{"edit": 0, "edits": [0, 1, 2],
 "sql": "ALTER TABLE `orders` ADD COLUMN `qty` INT, ADD INDEX `ix_qty` (`qty`), COMMENT='all orders'",
 "action": "alter", "object": "table", "name": "orders", "table": "orders",
 "spec": "ADD COLUMN `qty` INT, ADD INDEX `ix_qty` (`qty`), COMMENT='all orders'",
 "clauses": [
   {"edit": 0, "spec": "ADD COLUMN `qty` INT", "action": "add", "object": "column", "name": "qty", "table": "orders"},
   {"edit": 1, "spec": "ADD INDEX `ix_qty` (`qty`)", "action": "add", "object": "index", "name": "ix_qty", "table": "orders"},
   {"edit": 2, "spec": "COMMENT='all orders'", "action": "alter", "object": "table_options", "name": "orders", "table": "orders"}]}
```

| Member | Meaning |
| --- | --- |
| `edit` | its first clause's edit; the statement stands where that clause's own statement would have stood |
| `edits` | the edits it holds clauses of, in order, each once |
| `table` | the table, as named when the statement runs |
| `spec` | the text after `` ALTER TABLE `t` ``: the clauses, comma-separated |
| `clauses` | `{edit, spec, action, object, name, table, new_name?}` per clause, in order: the tags its own statement would have had and its text (`set_primary_key`'s `DROP PRIMARY KEY, ADD PRIMARY KEY (...)` is one clause) |

An `ALTER TABLE` that nothing joined keeps its own tags and has `edits`, `spec` and a
`clauses` list of one too, so a program can read every `ALTER TABLE` the same way. `RENAME
TABLE` and the other statements are left as they are, without these members. Without
`merge_alters` no statement has them.

## Notes

| Kind | Meaning |
| --- | --- |
| `name-in-string` | a string literal holds the old name as a word (for a column: and its table's name), outside a `COMMENT`; it was not changed. Dynamic SQL in `PREPARE` or `EXECUTE IMMEDIATE`, or data such as an audit log's table name |
| `rest-reference` | a REST object (`CREATE REST VIEW ... AS t`, `CREATE REST PROCEDURE ... AS p`) names the renamed table, view or routine; its REST SQL was not changed |
| `routine-recreated` | no statement renames a routine: the migration drops it and creates it again, and a `CREATE PROCEDURE` or `FUNCTION` cannot run inside a stored procedure, so msm or a person must place it |
| `routine-created` | the same for a routine `add_routine` creates or `replace_routine` writes anew: its `CREATE` in the migration cannot run inside a stored procedure. msm treats it as `routine-recreated` |
| `trigger-created`, `trigger-recreated` | the same for a trigger `add_trigger` creates (or `replace_trigger` writes anew) or `rename_trigger` drops and creates again: MariaDB refuses `CREATE TRIGGER` inside a stored procedure (error 1303) |
| `event-created` | the same for an event `add_event` creates or `replace_event` writes anew: MariaDB refuses `CREATE EVENT` with its body inside a stored procedure (error 1576). `ALTER EVENT ... RENAME TO` runs there |
| `alter-removed` | a `replace_*` removed an `ALTER` of the object after its `CREATE`: the new definition replaces what it changed; one note per statement |
| `still-referenced` | after a drop, a statement still refers to the dropped table, view, routine or column (a view's `SELECT`, a trigger, event or routine body, an `INSERT`); one note per statement |
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
| `bad-request` | the request is not JSON of format 1, a member is missing, empty, unknown or of the wrong type, a new name is too long, a routine name is ambiguous without `kind`, a column's type is not a MariaDB data type, or a definition does not parse |
| `unsupported` | an op this binary does not do or does not know (a newer binary may), or a column added to a table whose columns the script does not list (`CREATE TABLE ... AS SELECT`, `LIKE`) |
| `unknown-object` | the table, column, view, routine, trigger, event or sequence is not in the outline, nor the table or column a foreign key references, nor the column `after` names, nor a trigger's table (a view is not one) or the trigger its `order` names; `drop_trigger`'s `table` is not the trigger's |
| `name-taken` | the new name is taken: by a table, view or sequence for a table, view or sequence (they share a namespace), by a column of the table, by a routine of the same kind, by an index of the table, by a foreign key of the schema, by a trigger or an event of the schema for a trigger or event; a second primary key (use `set_primary_key`); a change of case only is a rename |
| `referenced` | `drop_table` of a table another table's foreign key references; `drop_column` of a column a foreign key uses; `drop_index` or `set_primary_key` taking away the index a foreign key or an `AUTO_INCREMENT` column needs; `drop_trigger` of a trigger another one follows or precedes; `drop_sequence` of a sequence a `CREATE TABLE` or `ALTER TABLE` names |
| `edit-outside-slice` | an edit of an included file would cross the bound of a directive's slice, lie outside every slice of the file, or leave a slice that cannot be written to select the edited text; the `CREATE` a `replace_*` rewrites spans more than one file's text |
| `self-check` | the outline of the edited texts does not show the edit (the table under its new name, the old name gone from every foreign key, trigger and index; an added object where it was asked for; a dropped one gone), or has a statement it cannot read that the input's outline read (an unapplied note the input did not have) |

The self-check is the safety net, as the formatter's re-parse is: it rebuilds the outline
after every edit and refuses the request rather than return edits that would lose an object.
