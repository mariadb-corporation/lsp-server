# Outline JSON format

`mariadb-lsp outline FILE` writes the schema objects a SQL file defines as one JSON object.
The same data will back the language server's document outline and, later, ALTER generation
and EER diagrams. This is format 1; a change that removes or renames a member increases the
number.

## How the outline is built

The statements that can change the schema, those starting with `CREATE`, `ALTER`, `DROP`,
`RENAME` or `USE` (also inside a versioned comment), are parsed and applied in file order:
`CREATE` adds an object, `ALTER` changes it, `DROP` removes it, `RENAME` renames or moves it,
`USE` sets the schema that unqualified names belong to. Other statements are not parsed.
A statement with a syntax error, or in an sql_mode the parser does not support, is not applied
and is listed under `notes`.

Names are as written, without quotes (`` `order` `` becomes `order`), and compare without
regard to case. Positions are LSP positions: a 0-based `line` and a 0-based `character`
counted in UTF-16 code units.

## Top level

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | number | `1` |
| `file` | string | the input file, when one was given |
| `schemas` | array of schema | in order of first appearance; a schema only named by `USE` and holding nothing is left out |
| `rest` | object | MariaDB REST Service objects |
| `statements` | object | `ddl`: statements that may change the schema; `applied`: those that did |
| `notes` | array | `{start, message}` for statements that could not be applied |

Every object below carries `range`, the whole defining statement (for a column, its
definition), and `selection_range`, its name: `{start: {line, character}, end: {...}}`.

## Schema

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string or null | null for objects whose schema the file does not name |
| `declared` | boolean | created by `CREATE DATABASE`/`SCHEMA` in the file; `range` and `selection_range` only then |
| `options` | object | `charset`, `collation`, `comment` |
| `tables`, `views`, `routines`, `triggers` | arrays | see below |
| `events`, `sequences` | arrays | `{name, range, selection_range}` |

## Table

| Member | Type | Meaning |
| --- | --- | --- |
| `name` | string | |
| `temporary` | boolean | |
| `options` | object | `engine`, `charset`, `collation`, `comment`, `auto_increment`, `row_format`, ... as written |
| `columns` | array | `name`, `type` (as written, blanks collapsed: `VARCHAR(45)`, `TINYINT UNSIGNED`), `nullable`, `default` (the expression as written, or null), `auto_increment`, `comment`, `collation`, `generated` (the expression of a generated column, or null) |
| `indexes` | array | `name` (null when unnamed), `kind` (`primary`, `unique`, `index`, `fulltext`, `spatial`, `vector`), `columns` |
| `foreign_keys` | array | `name`, `columns`, `references: {schema, table, columns}`, `on_delete`, `on_update` (`CASCADE`, `RESTRICT`, `SET NULL`, `NO ACTION`, `SET DEFAULT`, or null) |
| `checks` | array | `name`, `expression` |

A column declared `PRIMARY KEY` or `UNIQUE` appears as an index too; a column in the primary
key is not nullable.

## View, routine, trigger

| Object | Members |
| --- | --- |
| view | `name`, `columns` (the explicit column list, or empty), `definition` (the `SELECT` as written) |
| routine | `name`, `kind` (`procedure`, `function`), `parameters: [{mode, name, type}]` (mode `IN`, `OUT`, `INOUT`, or null), `returns` (functions) |
| trigger | `name`, `timing` (`BEFORE`, `AFTER`), `events` (`INSERT`, `UPDATE OR DELETE`, ...), `table` |

## REST objects

`rest.services` lists `{path, schemas, content_sets}`; a schema is `{path, schema, objects}`
where `schema` is the database schema it exposes and an object is `{kind, path, object}` with
`kind` one of `view`, `procedure`, `function` and `object` the database object as written.
`rest.auth_apps`, `rest.roles` and `rest.users` (`user@app`) are lists of names. A statement
that names no service uses the one set by `USE REST SERVICE`.
