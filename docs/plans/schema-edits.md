# Schema edits: `mariadb-lsp edit` (P10)

Structured schema edits applied to an MSM development script and the files its `SOURCE`
directives include, returned as text edits plus the migration DDL and the outline of the
edited texts. The binary never writes files: msm (`msm.edit_schema`, mariadb-shell-plugins)
checks every include path, runs the command, applies the edits, appends the migration to the
next release's update script (Section 240) and makes the snapshot again from the returned
outline. The VS Code extension's diagram viewer is the client of that.

Request and output formats: `docs/edit-format.md` (to be written with the code). The command
in `docs/language-server.md` and `--help`; msm detects support by `edit` in `--help`.

## 1. Invocation

```
mariadb-lsp edit --source-dir <project>/development --edits REQUEST.json
                 [--indent tab|2|4] [--line-width N] - < script
```

As `outline` is called by msm: the script on stdin, relative `SOURCE` paths from
`--source-dir`, the request JSON in the file `--edits` names. Section 160 arrives blanked
(lines kept). Output: one JSON object on stdout. Failure: exit 1 and
`{"error": {"edit": <index or null>, "kind": "...", "message": "..."}}`, no edits (all or
nothing); exit 2 for usage errors and unreadable input as today.

## 2. Design

### 2.1 Where the code goes

- `include/mdbls/schema_edit.h`, `src/schema_edit.cc`: the engine, JSON-free. `Edit_request`
  (`schema`, `std::vector<Edit>`; an `Edit` holds the op and the outline structs `Table`,
  `Column`, `Index`, `Foreign_key` it carries), `Edit_result` (`files`: per file the
  `Text_edit`s in that file's original text; `migration`; `notes`; the final `Outline` and
  the final texts), `Edit_error {edit, kind, message}`, and
  `apply_schema_edits(script_text, source_dir, reader, request, options) -> expected`.
  `Edit_options`: `Format_options` (indent, line width, version id), `Index_options`.
- `src/schema_edit_json.cc` (same header): `parse_edit_request(json) -> Edit_request or
  Edit_error` (outline shapes read back: a member left out takes the outline's default) and
  `edit_result_json(result, script_file_name, source_dir)`. RapidJSON is already a public
  dependency of the library (`json_position.h`), and `outline_json` lives in the library too.
- `src/mariadb-lsp/main.cc`: the `edit` command and `--edits FILE`; `--source-dir`,
  `--indent`, `--line-width` accepted for it. One `--help` line starting `  edit `.
- Tests: `unittest/mariadblanguageserver/schema_edit_t.cc` (in-memory files, as
  `source_include_t.cc`), `tests/e2e/test_edit.py`.

### 2.2 The pipeline (one request)

1. **Session texts.** A table of file texts: source 0 the script, then every include by its
   resolved path, read once (a file included twice has one entry). `expand_sources` runs with
   a reader over this table, so after an edit the expansion sees the edited texts.
2. **Input outline** of the expansion (`build_outline(Source_expansion, ...)`, as `outline`
   does). Its unapplied notes are remembered for the self-check. An include that cannot be
   read stays a note, as in `outline`; an edit that needs an object from it cannot find one.
3. **For each edit, in order:** resolve its objects in the current outline (§2.3); run the
   checks (§2.9); compute the change as `Text_edit`s in the *current* text of the files
   concerned (§2.4–2.6); record the migration statements and notes; apply the edits to the
   session texts through the per-file edit log (§2.7); expand and fold the outline again;
   verify that the new outline shows the edit (§2.10). Every failure stops the request with
   the edit's index.
4. **Slices** (§2.8): after the last edit, every directive whose include changed length gets
   its bounds recomputed, as one more edit of the script (or of the file that holds the
   directive).
5. **Self-check** (§2.10) on the final outline; then the output (§2.11).

Rebuilding the outline after each edit costs one fold per edit (35 ms on the MRS project);
a request has a handful of edits.

### 2.3 Resolving names and finding references

- `schema` is the request's schema; objects are looked up in that schema and, when not found
  there, in the outline's `""` schema (statements before any `USE`), as msm's snapshot merges
  both. Names compare case-insensitively (`iequals`), for columns as for tables.
- The `Object_id` of a table, view, column or routine comes from
  `Name_resolver::resolve_at` on the definition's `origin.name` in the expanded text, with a
  `Catalog_view` of one document layer (the outline's catalog, as the CLI's `complete`
  builds it) and the schema in use at that statement. That is exactly what the server's
  rename resolves, so `find_references(expanded->text, *expanded->index, id, ...)` returns
  the same hits the server's rename rewrites, including trigger and routine bodies, views,
  `INSERT`s, `GRANT`s (the e2e test of P9 shows `170_roles.sql` hits) and `DROP ... IF EXISTS`.
- Hits are mapped back with `Outline::map` to `(source, range)`, one per file position (a
  file included twice: one hit per position, as `references_of` dedupes), and grouped by
  file.
- **Not rewritten, noted instead** (§2.9 notes): the name inside a string literal of a
  statement that mentions it (found by lexing those statements with `mdbp_lex_sql`, as
  `references_in` does for long statements, and matching the name as a whole word in string
  tokens: dynamic SQL in `PREPARE`/`EXECUTE IMMEDIATE`); a REST SQL object naming the table
  (`Rest_object::object` compared by name: `CREATE REST VIEW ... AS t`, whose name the
  classifier marks `other`, not a table); after a drop, every statement that still refers to
  the dropped object.

### 2.4 Text style, detected per file and statement

- **Quoting of a renamed name:** as the token it replaces was written: backticks when it had
  them, else `quote_identifier` (only when needed). **Quoting in new text** (a new column,
  element or statement): backticks when the CREATE TABLE's own name token has them (MSM
  scripts quote everything), else `quote_identifier`.
- **Keyword case** of new text: that of the first keyword of the statement edited (`CREATE`
  vs `create`), or of the neighbouring statement for a new one; it sets
  `Format_options::upper_case_keywords` for formatted statements and the words written by
  hand (`NOT NULL`, `CONSTRAINT`, ...).
- **Line break:** `eol_of` of the file.
- **Indentation and commas of an element** added to a CREATE TABLE: the indentation of the
  last element's line (`indentation`), and the comma style of the siblings: trailing (`a,`
  at the end of the line, the usual) or leading (a line starting with `,`).
- **New statements** (`add_table`'s CREATE TABLE, an `ALTER TABLE ... ADD CONSTRAINT`, an
  `ALTER TABLE ... ADD COLUMN ... AFTER x` when `x` was itself added by an ALTER TABLE) are
  generated as one line, then laid out by `format_sql` with the `--indent`/`--line-width`
  given (the statement alone in a one-statement index), and end with the delimiter in force
  at that point of the expanded script (`Statement::active_delimiter` of the statement
  before the insertion point: `%%` inside a `DELIMITER %%` block).
- **Column text** is `name` + `Column::definition()`, with `COMMENT '...'` added to
  `definition()` (escaped with the string's own quoting rules) and `options`
  (`ENCRYPTED=YES`) appended; `Column::definition` is the shared helper and hover gains the
  comment too. Index text: `PRIMARY KEY (...)`, `[UNIQUE|FULLTEXT|SPATIAL|VECTOR] INDEX name
  (a ASC, b(10))` with `USING`, `COMMENT`, `KEY_BLOCK_SIZE`, `WITH PARSER`, `IGNORED`,
  `WITHOUT OVERLAPS`. Foreign key text: `CONSTRAINT name FOREIGN KEY [index] (cols)
  REFERENCES t (cols) [MATCH] [ON DELETE] [ON UPDATE]`, one line, laid out by the formatter
  where it is a statement of its own. Check text: `[CONSTRAINT name] CHECK (expr)`.

### 2.5 Where things go

- **A column** (`add_column`): after the last column *defined in the CREATE TABLE* (before
  the first key or constraint), or after `after`, or first. When `after` names a column that
  an `ALTER TABLE ... ADD COLUMN` defines, the new column cannot go into the CREATE TABLE
  without changing the order the outline shows, so a new `ALTER TABLE t ADD COLUMN ... AFTER
  x` statement follows that ALTER TABLE (same file, formatted). An element in a CREATE TABLE
  is inserted as a new line after its predecessor's line (trailing-comma style: after the
  predecessor's comma; when the predecessor was the last element, a comma is added to it).
- **A table** (`add_table`): after the CREATE TABLE of the schema's last table (the greatest
  `origin.statement.begin` in the expanded text), in that statement's file, after its
  delimiter, preceded by as many line breaks as separate that statement's line from the end
  of the statement before it, counting only the blank lines before any comment line (MRS:
  `;\n\n\n-- banner`: two blank lines). No banner is copied. Its foreign keys go into the
  CREATE TABLE when the referenced table exists earlier in the expanded script or is the table
  itself, else (`add_table` referencing a table created later, or `add_foreign_key` where the
  referenced table comes after the referencing one) as an `ALTER TABLE t ADD CONSTRAINT ...`
  after the last `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY` statement of the script when
  one stands after both tables (the Workbench import's `140-20_table_additions.sql`), else
  right after the later table's CREATE TABLE. A referenced table the script does not create
  is `unknown-object`.
- **An index or foreign key** (`add_index`, `add_foreign_key` into the CREATE TABLE): after
  the last element of its kind (index after the last index, else after the last column;
  foreign key after the last foreign key, else after the last index, else after the last
  column).
- **An element defined by a later statement** (a column from `ALTER TABLE ... ADD COLUMN` or
  `CHANGE`, an index from `ALTER TABLE ... ADD INDEX` or `CREATE INDEX`, a foreign key from
  `ALTER TABLE ... ADD CONSTRAINT`) is edited where its outline `range` says: the definition
  text is replaced or removed there. Removing one change of an ALTER TABLE with several
  removes the `alter_list_item` and its comma (the statement is parsed, the items are tree
  nodes); removing the only change removes the whole statement; a `CREATE INDEX` statement
  is removed whole.
- **A statement removed** (`drop_table`: the CREATE TABLE, every ALTER TABLE and CREATE INDEX
  on it, its triggers' CREATE TRIGGER and the `DROP TRIGGER [IF EXISTS]` / `DROP TABLE [IF
  EXISTS]` of those names) goes with its lines: from the start of its first line to the end
  of its delimiter's line, plus the comment lines directly above it that name the object
  (a Workbench banner), plus one run of blank lines so the neighbours keep their spacing.
- **A column removed** (`drop_column`): its definition line(s) with the comma (the previous
  element's comma when it was last), its key parts in every index of the table (`(a, b)` to
  `(b)`; an index left with none is removed as the outline removes it), its own CHECK (part
  of the definition text), a column-level `PRIMARY KEY`/`UNIQUE` index (part of the
  definition). A column defined by ALTER TABLE: that change is removed (above).
- **`modify_column`**: the definition text at the column's `range` is replaced by the new
  text; a column-level CHECK the table has for that column is kept at the end of the new
  text (a Column carries no check); a different `name` is also a rename (references
  rewritten first, then the definition).
- **`set_table_options`**: the CREATE TABLE is parsed, its `create_table_option` nodes
  found; an option given replaces that node's text (keeping `ENGINE = X` vs `ENGINE=X` as
  the neighbours write it), `null` removes it (and the blank before it), a new one is
  appended after the last option, or after `)` when there is none.
- **`drop_index`** of a column-level `PRIMARY KEY`/`UNIQUE`: the attribute is removed from the
  column definition (tree-based); `drop_foreign_key` of a column-level `REFERENCES`
  likewise. Unnamed indexes are named by `kind: primary` or by `columns`; an unnamed foreign
  key by `columns`.

### 2.6 Renames

`rename_table`, `rename_column`, `rename_view`, `rename_routine`, and `modify_column` with
a new name: every hit of §2.3 (definition and references) is replaced by the new name
written as the hit was (§2.4). Nothing else moves. `name-taken` is checked against the
schema's tables and views together (they share the namespace), the table's columns, or the
schema's routines of the same kind.

### 2.7 The per-file edit log

Edits of later ops are computed in the text as edited so far. Each file keeps a log of
edits in its *original* coordinates (sorted, non-overlapping) and the session text. Applying
an edit given in current coordinates maps its range back through the log; a range that
touches text an earlier edit inserted merges with that entry (the union in original
coordinates, the final text as the new text), so the log stays non-overlapping and, applied
back to front to the file on disk, yields the session text. A unit test checks that for
random edit sequences. The output lists each file's log.

### 2.8 Slices

After all edits, for every directive of every include that changed: the bytes the slice
selected before, mapped through the file's changes (an edit inside the selection moves its
end; one before it moves both bounds), is what it must select after. Each written bound is
re-evaluated: a character offset becomes the new offset (positive: counted from the start;
negative: from the new end), a `lnN` the new line number (sign kept); an empty bound stays
empty. A bound that still selects the same point is left as written. A changed bound is a
text edit of the slice (`Source_directive::slice`) in the including file. So `[89:]` and
`[695:-115]` need nothing for an edit inside the selection, `[ln5:ln300]` and `[89:1200]`
get a new end, and a file included twice gets both directives checked.

`edit-outside-slice`: an edit whose range crosses a selection boundary of any directive that
includes the file, or lies outside every selection of it. (Both can only happen when a
statement straddles a slice boundary.)

### 2.9 Checks, errors and notes

Errors (the whole request fails, `kind` stable):

| kind | when |
| --- | --- |
| `bad-request` | the JSON is not a request of format 1, an op lacks a member, a name is empty |
| `unsupported` | an op the binary does not do yet (per PR, below) |
| `unknown-object` | the table, column, view, routine, index, foreign key or referenced table is not in the outline |
| `name-taken` | rename or add to an existing name (§2.6), a second primary key |
| `referenced` | `drop_table` while another table's foreign key references it; `drop_column` while a foreign key of this or another table uses it (an index that uses it merely loses the part), unless an earlier edit of the request removed that |
| `edit-outside-slice` | §2.8 |
| `self-check` | §2.10 |

Notes (`{edit, kind, source?, start, message}`, the edit goes on):

| kind | when |
| --- | --- |
| `name-in-string` | the old name appears in a string literal of a statement (dynamic SQL); not rewritten |
| `rest-reference` | a REST object (`CREATE REST VIEW ... AS t`) names the renamed or dropped table; not rewritten |
| `still-referenced` | after a drop, a statement still refers to the object: a view's SELECT, a trigger or routine body, an INSERT |
| `routine-recreated` | a routine rename has no DDL: the migration holds `DROP PROCEDURE|FUNCTION old` and the new CREATE from the script, which cannot run inside the update procedure (MariaDB 1303); msm or a person must place it |
| `no-migration` | an op with nothing to run: `drop_foreign_key` of an unnamed key (the server's generated name is unknown), `set_table_options` removing an option |

### 2.10 Self-check

After each edit, the outline is rebuilt from the session texts and must show the edit:
`rename_*`: the new name present, the old absent; `add_table`: a table of that name with the
requested column names, index and foreign key count; `drop_table`: absent; `add_column`: the
column present at the position requested (index in `columns`); `modify_column`: the column's
members equal the request's (type and expressions compared collapsed, case-insensitively);
`drop_column`: absent; `add_index`/`add_foreign_key`: present by name or columns;
`drop_*`: absent; `set_table_options`: the options given equal. After the last edit, the
outline's unapplied notes must be a sub-multiset of the input's (compared by kind and message
without its `FILE:LINE: ` prefix, so a note that only moved still matches). Any failure is
`self-check` with the edit index, and nothing is returned.

### 2.11 Migration, per op

No schema qualifier, no delimiter, one statement per entry, in edit order:

| op | SQL |
| --- | --- |
| `rename_table`, `rename_view` | `RENAME TABLE a TO b` |
| `rename_column` | `ALTER TABLE t RENAME COLUMN a TO b` |
| `rename_routine` | `DROP PROCEDURE|FUNCTION old`, then the CREATE from the edited script (note `routine-recreated`) |
| `add_table` | the CREATE TABLE (foreign keys included) |
| `drop_table` | `DROP TABLE t` |
| `add_column` | `ALTER TABLE t ADD COLUMN c <definition> [FIRST | AFTER x]` |
| `modify_column` | `ALTER TABLE t MODIFY COLUMN c <definition>` (`CHANGE COLUMN old new ...` when renamed); when a foreign key of this or another table uses the column: `ALTER TABLE x DROP FOREIGN KEY fk` for each before, the ALTER, then `ALTER TABLE x ADD CONSTRAINT fk FOREIGN KEY ...` for each after (MariaDB 1833) |
| `drop_column` | `ALTER TABLE t DROP COLUMN c` |
| `add_index` / `drop_index` | `ALTER TABLE t ADD [UNIQUE|FULLTEXT|SPATIAL|VECTOR] INDEX name (...) ...` / `ALTER TABLE t ADD PRIMARY KEY (...)`; `ALTER TABLE t DROP INDEX name` / `DROP PRIMARY KEY` |
| `add_foreign_key` / `drop_foreign_key` | `ALTER TABLE t ADD CONSTRAINT name FOREIGN KEY ...`; `ALTER TABLE t DROP FOREIGN KEY name` |
| `set_table_options` | `ALTER TABLE t ENGINE=X COMMENT='...' ...` for the options given (`null` ones: nothing, note) |

### 2.12 Output JSON

As the request specifies: `format` 1, `files` (`file` `"-"` or the include's path relative to
`--source-dir` with `/`; `edits` as LSP ranges in that file's text on disk through
`write_range`, sorted, non-overlapping), `migration` (`{edit, sql}`), `notes`, `outline`
(the `outline_json` of the edited texts, `file` `"-"`; positions in the edited files).

## 3. Steps

1. **PR 1, renames:** the module, the pipeline, edit log, references and per-token quoting,
   slices, self-check, errors, notes `name-in-string` and `rest-reference`, migration for
   the four renames; `edit` in the CLI; `docs/edit-format.md`, `language-server.md`, README,
   `--help`, PROJECT_CONTEXT ("Kept in sync with msm_plugin"); unit tests and the e2e test
   (fixture project; MRS project when present). Other ops answer `unsupported`.
2. **PR 2, tables and columns:** `add_table`, `drop_table`, `add_column`, `modify_column`
   (with the 1833 dance), `drop_column`, `set_table_options`; `Column::definition` with
   COMMENT and options; element insertion and removal, statement insertion and removal,
   ALTER items; note `still-referenced`.
3. **PR 3, indexes and foreign keys:** `add_index`, `drop_index`, `add_foreign_key`,
   `drop_foreign_key`, placement by creation order, column-level attributes.

## 4. Tests

Unit (`schema_edit_t.cc`, in-memory files): every op; renames across includes with sliced
directives (`[ln5:ln300]`, `[89:1200]` shifted, `[89:]` and `[695:-115]` untouched); a file
included twice; objects defined by ALTER TABLE and CREATE INDEX; foreign key placement both
ways; quoting and keyword-case detection (quoted vs bare, upper vs lower); CRLF files;
DELIMITER blocks (a new statement ending with `%%`); all or nothing on a failing edit in the
middle; migration text per op including the 1833 dance; the self-check (a fold that would
lose an object fails the request); the edit log with random sequences; the name in a string
literal noted.

E2E (`tests/e2e/test_edit.py`): on a copy of the fixture project, and on a copy of the MRS
project (`MDBLS_MRS_PROJECT`, default `~/git/mariadb-shell-diagrams/mysqlshdk/libs/mrs/
db_schema/mariadb_rest_service.msm.project`; skipped when absent, so CI runs the fixture
only): rename `url_host` (referenced by foreign keys, triggers, views, routines, GRANTs,
INSERTs), add a column, apply the edits with the Python side of the test, and check that
`mariadb-lsp outline` of the edited files equals the returned `outline`, the slice of
`140-10_tables.sql` still selects the same statements, and `mariadb-lsp check` finds no
error.

## 5. Decisions taken in this plan (to confirm)

1. An edit outside one directive's slice but inside another's (a file included twice with
   different slices) is allowed; only a range crossing a boundary fails
   (`edit-outside-slice`).
2. A routine rename's migration holds the DROP and the new CREATE although Section 240
   cannot run a CREATE PROCEDURE; the note `routine-recreated` says so.
3. `add_column` with `after` naming a column an ALTER TABLE added goes into a new ALTER
   TABLE after it, not into the CREATE TABLE.
4. `drop_table` also removes the Workbench banner comment above the CREATE TABLE and the
   `DROP ... IF EXISTS` statements of the dropped objects.
5. Keyword case is read from the edited statement's first keyword; quoting from the CREATE
   TABLE's own name token.
6. `drop_column` fails `referenced` only for foreign keys; an index using the column loses
   the key part (and goes when empty), as the server does.
7. Exit 1 for a refused request, 2 for usage errors and unreadable input.

## Status

**PR 1 (renames), 2026-11-05:** built as designed, with these differences:

- A procedure and a function of one name are one object to `find_references`, so renaming
  either is `unsupported` while both exist.
- `name-in-string` skips `COMMENT` literals, and for a column needs its table's name in the
  same literal too (`name` alone is in too many strings).
- `rename_routine`'s CREATE in the migration comes from the expanded text, so a body that
  includes a file (MRS `restore_roles`) holds that file's text.
- The JSON request reader and output writer live in `schema_edit.cc` too (no `schema_edit_json.cc`).
- Notes also carry `file`; `start` is in the file's text on disk, as the edit ranges are.
- `edit-outside-slice` cannot happen through a rename (hits lie inside selections); the check
  is in place for the ops that insert text. An edit before a positive start bound (a file
  included twice with different slices) moves that bound.

Tests: `unittest/mariadblanguageserver/schema_edit_t.cc` (edit log with random sequences,
slice bound round trips, every rename, sliced and twice-included files, CRLF, DELIMITER
blocks, quoting, order, all or nothing, notes, the self-check), `tests/e2e/test_edit.py`
(fixture project; the MRS project: rename `url_host`, referenced by foreign keys, and a
column of it, apply the edits, `outline` of the files equals the returned one, `check`
passes). The decisions of §5 stand until the user says otherwise.
