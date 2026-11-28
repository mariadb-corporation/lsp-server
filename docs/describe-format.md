# Statement descriptions: the `describe` output (format 1)

`mariadb-lsp describe FILE|-` prints what each top-level statement of a SQL text does, read
from its parse tree: the verb, the kind of object, `OR REPLACE` / `IF [NOT] EXISTS` /
`TEMPORARY`, the objects it names, and for REST SQL the service, schema and path it acts on.
msm_plugin (mariadb-shell-plugins) uses it to route the statements of a schema edit, DROPs
above all: the outline cannot show a DROP of an object the text never created. The plan is
[plans/describe-statements.md](plans/describe-statements.md).

```text
mariadb-lsp describe [--sql-mode LIST] [--delimiter D] [--version-id N] FILE|-
mariadb-lsp describe --statements LIST.json [--sql-mode LIST] [--version-id N]
```

- **A text** (`FILE`, or `-` for stdin) is split as `split` splits it (`DELIMITER` and
  `SET sql_mode` followed). There is one entry per SQL statement, in file order;
  comment-only entries are left out.
- **A list** (`--statements`, `-` for stdin) is a JSON array of strings, one statement per
  item: entry `i` describes item `i` (see [Statement lists](#statement-lists)).
- The output is always JSON, one object on stdout. The exit status is 0 whenever the input
  could be read, also when statements do not parse (that is in the entries), and 2 for usage
  errors and unreadable input.
- **Detection:** a line starting `  describe ` in `mariadb-lsp --help`. Binaries without the
  command exit 2 on it.

## Output

```json
{"format": 1,
 "statements": [
  {"begin": 0, "body_end": 25, "end": 26,
   "start": {"line": 0, "character": 0}, "stop": {"line": 0, "character": 26},
   "status": "ok", "verb": "drop", "kind": "table",
   "or_replace": false, "if_exists": true, "if_not_exists": false, "temporary": false,
   "objects": [{"schema": null, "name": "t"}, {"schema": "s", "name": "u"}],
   "current_schema": null, "rest": null},
  {"begin": 27, "body_end": 75, "end": 76, "...": "...",
   "status": "ok", "verb": "drop", "kind": "rest_view",
   "or_replace": false, "if_exists": false, "if_not_exists": false, "temporary": false,
   "objects": [], "current_schema": null,
   "rest": {"service": {"path": "/app", "developers": [], "written": true},
            "schema": {"path": "/main", "written": true},
            "path": "/n", "new_path": null, "database_schema": null}}
 ]}
```

Members are only ever added; one changing its meaning or going away needs format 2. New
values of `verb`, `kind` and `status` may appear in format 1: read one you do not know as
`other` (for `status`: as not `ok`).

### A statement

| Member | Type | Meaning |
| --- | --- | --- |
| `index` | number | list input only: the item this entry describes |
| `begin`, `body_end`, `end` | number | byte offsets, as `split --format json` gives them: the statement's first byte, the end of its SQL, the end of its delimiter; for list input, within the item |
| `start`, `stop` | position | LSP positions of `begin` and `end` |
| `status` | string | `ok`, or why there is no description (below) |
| `message` | string | not `ok` only: why, for a person |
| `error_range` | range | `syntax-error` only: the offending token |
| `verb` | string or null | `create`, `alter`, `drop`, `rename`, `use`, `other`; null when not `ok` |
| `kind` | string or null | the kind of object (below); `other` for anything else; null when not `ok` |
| `or_replace`, `if_exists`, `if_not_exists`, `temporary` | bool | as written; false when the statement cannot have them or is not `ok` |
| `objects` | array | the objects it names (below); `[]` when it names none, names only REST paths, or is not `ok` |
| `current_schema` | string or null | the schema the last `USE` before this statement selected; null for none |
| `rest` | object or null | for a `rest_*` kind (below); null for every other statement, REST SQL of kind `other` included (`GRANT REST`, `SHOW REST`, `USE REST METADATA`, `DROP REST DAEMON`) |

Nothing is guessed from the words of a statement that does not parse.

### `status`

| Value | When |
| --- | --- |
| `ok` | the statement parses under its `sql_mode` |
| `syntax-error` | it does not; `message` is the one `check` gives |
| `unsupported` | the parser cannot read it: invalid UTF-8, an `sql_mode` it does not support (ORACLE, MSSQL) |
| `source-directive` | an MSM `SOURCE '<path>'` directive, or a statement holding one (not parsed, as in `check`) |
| `several-statements` | list input: the item holds more than one statement |
| `empty` | list input: the item is blank or only comments |

### `kind`

- **SQL:** `table`, `sequence`, `view`, `procedure`, `function` (stored or `SONAME`),
  `trigger`, `event`, `index`, `schema` (written `DATABASE` or `SCHEMA`). A `USE` is
  `schema`.
- **REST:** `rest_service`, `rest_schema` (also `REST DATABASE`), `rest_view` (with or without
  `DATA MAPPING`), `rest_procedure`, `rest_function`, `rest_content_set`,
  `rest_content_file`, `rest_auth_app`, `rest_user`, `rest_role`. `USE REST SERVICE s` is
  `rest_service`; `USE REST [SERVICE s] SCHEMA p` is `rest_schema`.
- **`other`:** everything else, such as users, roles, servers, packages, `GRANT`, `SHOW`,
  `USE REST METADATA`, `CONFIGURE REST METADATA`, DML.

### `objects`

Each is `{schema, name}`: unquoted, in the case written. `schema` is null when the name is
not qualified; it is never filled in from `current_schema`.

| Statement | `objects` |
| --- | --- |
| `DROP TABLE`, `DROP VIEW`, `DROP SEQUENCE` | every name, in order |
| `CREATE TRIGGER ... ON t` | the trigger, with `table: {schema, name}` |
| `CREATE INDEX i ON t`, `DROP INDEX i ON t` | the index (`schema` null), with `table` |
| `RENAME TABLE a TO b, c TO d` | one per pair, with `new_schema` and `new_name` |
| `ALTER TABLE t ... RENAME TO t2`, `ALTER EVENT e ... RENAME TO e2` | the object, with `new_schema` and `new_name` |
| `USE db`, `CREATE`/`ALTER`/`DROP SCHEMA db` | `{schema: null, name: "db"}` |
| REST auth apps, roles, users | the app, the role, or `user@app` (`schema` null) |
| the other kinds | the object |

`table`, `new_schema` and `new_name` are present only where the table above names them.

### `rest`

```text
{"service": {"path", "developers", "written"} | null,
 "schema":  {"path", "written"} | null,
 "path": string | null,
 "new_path": string | null,
 "database_schema": string | null}
```

- **`service`, `schema`:** the REST service and schema the statement acts on, or acts in.
  - For a `rest_service` statement, `service` is the statement's subject. For a
    `rest_schema` statement, `schema` is its subject and `service` the service it is on.
  - `written: true`: the statement names it (`ON SERVICE`, `ON SCHEMA`, `FROM SERVICE ...
    SCHEMA`, `USE REST ...`). A `CREATE REST SCHEMA` without a request path has
    `/<database schema>`, as in MariaDB Shell, and that counts as written.
  - `written: false`: taken from what is in use before the statement in this text, resolved
    exactly as the outline resolves it ([outline-format.md](outline-format.md), "REST
    objects"; feature `rest_current_schema`): only `USE REST SERVICE` sets the service, and
    it clears the schema; `USE REST SCHEMA` sets the schema; the first schema a `CREATE REST
    SCHEMA` gives the current service becomes current.
  - Null when the statement names none and none is in use. That is no error here: MariaDB
    Shell would fail such a statement, and the outline gives it a `rest-none-in-use` note.
  - Services, schemas and objects of a `CREATE REST USER`, `AUTH APP` or `ROLE` are null.
- **`path`:** the request path of the statement's own item: a view, procedure, function,
  content set or content file. Null for service and schema statements, whose paths are in
  `service` and `schema`.
- **`new_path`:** the `NEW REQUEST PATH` of an `ALTER`, as written; for a service, with its
  developers when it writes any.
- **`database_schema`:** the database schema of `CREATE`/`ALTER REST SCHEMA ... FROM db` and
  of `DROP REST SCHEMA ... FOR SCHEMA db`, unquoted.
- **Paths** are as written, quotes kept, blanks collapsed, as the outline's REST `path`.
  `developers` are unquoted and sorted as MariaDB Shell sorts them (without regard to case),
  as in the outline's `rest_settings`: `mike, anna@/svc` gives `{"path": "/svc",
  "developers": ["anna", "mike"]}`. MSM placeholders (`/*<msm:NAME>*/`) are comments and do
  not appear.

## Statement lists

With `--statements`, each item of the array is one statement:

- A trailing `;` is optional, and a routine body's `;` needs no `DELIMITER`: the items are
  joined, each followed by a delimiter that none of them contains, so an item always ends
  where it ends. `body_end` stops before a trailing `;`, as in a text.
- The items are described in order as one text, so `SET sql_mode`, `USE` and `USE REST` in
  one item apply to the items after it.
- An item that is not one statement gets a status instead of a description:
  `several-statements` when it does not parse as one statement but splits at `;` into
  statements that each parse (`DROP VIEW a; DROP VIEW b`), `empty` when it is blank or only
  comments, `unsupported` when it changes the delimiter (`DELIMITER //`), and `syntax-error`
  when it leaves a string or comment open. Such an item does not take part in the `sql_mode`
  and `USE` carried to the next ones. Its positions cover the whole item.
- A file that is not a JSON array of strings is a usage error (exit 2).
