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

### Ops

| op | Members | Status | What changes |
| --- | --- | --- | --- |
| `rename_table` | `table`, `new_name` | done | the definition and every reference |
| `rename_column` | `table`, `column`, `new_name` | done | the definition, also when an `ALTER TABLE` adds the column, and every reference |
| `rename_view` | `view`, `new_name` | done | the definition and every reference |
| `rename_routine` | `routine`, `new_name`, `kind` (`procedure` or `function`; needed when both kinds have the name) | done | the definition and every reference |
| `add_table`, `drop_table`, `add_column`, `modify_column`, `drop_column`, `set_table_options` | | not yet | |
| `add_index`, `drop_index`, `add_foreign_key`, `drop_foreign_key` | | not yet | |

An op that is not done yet is refused with `unsupported`.

A rename finds its references as the language server's rename does: every statement of the
expanded script that spells the name is parsed and each name in it resolved in the
script's context (its schema in use, aliases, the tables in scope), so `h.name` of `FROM
url_host h` is a reference to `url_host.name` and `name` of another table is not. That
covers DDL (foreign keys, indexes, `ALTER TABLE`, `CREATE INDEX`, `RENAME TABLE`, triggers'
`ON`), DML, views, routine and trigger bodies, `GRANT`s, and statements in versioned
comments, in the script and in every included file (a file included twice is edited once).
Each reference is written as it was: in backticks or double quotes when it was, else with
quotes only when the new name needs them.

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

## Notes

| Kind | Meaning |
| --- | --- |
| `name-in-string` | a string literal holds the old name as a word (for a column: and its table's name), outside a `COMMENT`; it was not changed. Dynamic SQL in `PREPARE` or `EXECUTE IMMEDIATE`, or data such as an audit log's table name |
| `rest-reference` | a REST object (`CREATE REST VIEW ... AS t`, `CREATE REST PROCEDURE ... AS p`) names the renamed table, view or routine; its REST SQL was not changed |
| `routine-recreated` | no statement renames a routine: the migration drops it and creates it again, and a `CREATE PROCEDURE` or `FUNCTION` cannot run inside a stored procedure, so msm or a person must place it |

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
| `bad-request` | the request is not JSON of format 1, an op is unknown, a member is missing, empty or of the wrong type, a new name is too long, or a routine name is ambiguous without `kind` |
| `unsupported` | an op this binary does not do yet, or a routine renamed while a routine of the other kind has its name (references do not tell the two apart) |
| `unknown-object` | the table, column, view or routine is not in the outline |
| `name-taken` | the new name is taken: by a table or view for a table or view (they share a namespace), by a column of the table, by a routine of the same kind; a change of case only is a rename |
| `edit-outside-slice` | an edit of an included file would cross the bound of a directive's slice, lie outside every slice of the file, or leave a slice that cannot be written to select the edited text |
| `self-check` | the outline of the edited texts does not show the edit (the table under its new name, the old name gone from every foreign key, trigger and index), or has a statement it cannot read that the input's outline read (an unapplied note the input did not have) |

The self-check is the safety net, as the formatter's re-parse is: it rebuilds the outline
after every edit and refuses the request rather than return edits that would lose an object.
