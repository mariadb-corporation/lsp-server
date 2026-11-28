# Statement descriptions: `mariadb-lsp describe` (P11)

What each top-level statement of a SQL text does, one entry per statement, read from the
parse tree. Requested by msm_plugin (mariadb-shell-plugins), 2026-11-28. Status: plan, no code.

## 1. Why

mcp_plugin will merge `msm.set_schema_statements`, `msm.set_rest_statements`,
`msm.drop_schema_object` and `msm.drop_rest_object` into one tool. That tool takes only a list
of SQL statements (`CREATE`, `CREATE OR REPLACE`, `DROP`, schema and REST mixed) and applies
them as one all-or-nothing edit, so that weak local LLMs only have to write plain SQL. msm has
to route each statement to the right `edit` op.

Today msm can route a `CREATE`: it outlines the statement alone and reads the object it
defines. It cannot route a `DROP`. `outline` folds the text, and a `DROP` of an object the text
never created changes nothing, so `DROP VIEW v;`, `DROP TABLE IF EXISTS t, u;` or
`DROP REST VIEW /n FROM SERVICE /app SCHEMA /main;` outlined alone give no item and no note.
`split` gives only offsets and kind `sql`. `tree` shows that the parser knows everything, but
it is a debug dump. msm must not parse SQL with regexes (its rule), so the description has to
come from mariadb-lsp. It also lets msm drop two regexes in `msm_plugin/lib/rest_editing.py`
(`_CREATE`, `_IF_NOT_EXISTS`), which check `OR REPLACE` / `IF NOT EXISTS` on a REST CREATE's
text.

## 2. Invocation

```text
mariadb-lsp describe [--sql-mode LIST] [--delimiter D] [--version-id N] FILE|-
mariadb-lsp describe --statements LIST.json [--sql-mode LIST] [--version-id N]
```

- **Text input** (`FILE` or `-`): split as `split` splits it (`DELIMITER`, `SET sql_mode`
  followed). One entry per `sql` statement of the split, in file order. Comment-only entries
  are left out. Each entry carries `split`'s positions.
- **List input** (`--statements`): a JSON array of strings, one statement per item, which is
  what the merged tool receives. Entry `i` describes item `i`. A trailing delimiter (`;`) is
  optional, and a routine body's `;` needs no `DELIMITER`, since an item is one statement by
  definition. An item holding more than one statement is `several-statements`; a blank or
  comment-only item is `empty`. `SET sql_mode` in one item applies to the items after it, as in
  a text.
- Output: always JSON, one object on stdout. Exit 0 whenever the input could be read, even
  when statements do not parse (that is in the entries); exit 2 for usage errors and unreadable
  input, as for the other commands.
- **Detection:** a `--help` line starting `  describe ` (msm already detects `edit` this way).
  Older binaries print no such line and exit 2 on the command.

**Why a new command rather than `split --describe` or an outline feature:**

- `split` never parses, so it stays fast for its other uses.
- `outline` folds and expands `SOURCE`, which is what the `DROP`s above lose.
- A command of its own has its own `format` number, its own help line for detection, and can
  take the list input.

## 3. Output (format 1)

```json
{"format": 1,
 "statements": [
  {"begin": 0, "body_end": 27, "end": 28,
   "start": {"line": 0, "character": 0}, "stop": {"line": 0, "character": 28},
   "status": "ok",
   "verb": "drop", "kind": "table",
   "or_replace": false, "if_exists": true, "if_not_exists": false, "temporary": false,
   "objects": [{"schema": "s", "name": "t"}, {"schema": null, "name": "u"}],
   "current_schema": null,
   "rest": null},
  {"begin": 29, "body_end": 77, "end": 78, "...": "...",
   "status": "ok", "verb": "drop", "kind": "rest_view",
   "or_replace": false, "if_exists": false, "if_not_exists": false, "temporary": false,
   "objects": [], "current_schema": null,
   "rest": {"service": {"path": "/app", "developers": [], "written": true},
            "schema": {"path": "/main", "written": true},
            "path": "/n", "new_path": null, "database_schema": null}}
 ]}
```

### 3.1 Per entry

| Member | Type | Meaning |
| --- | --- | --- |
| `begin`, `body_end`, `end`, `start`, `stop` | | text input: as `split --format json` gives them |
| `index` | number | list input: the item's index (the positions are then within the item) |
| `status` | string | `ok`, or why there is no description (§3.4) |
| `message`, `error_range` | string, range | for `syntax-error`: the parser's message and where |
| `verb` | string | `create`, `alter`, `drop`, `rename`, `use`, `other` |
| `kind` | string | §3.2; `other` for statements about anything else, also for `verb: other` |
| `or_replace`, `if_exists`, `if_not_exists`, `temporary` | bool | as written; false when the statement cannot have them |
| `objects` | array | §3.3; `[]` when the statement names no object (or only REST paths) |
| `current_schema` | string or null | the schema the last `USE` before this statement in this input selected; null when there is none |
| `rest` | object or null | §3.5; null for statements that are not REST SQL |

When `status` is not `ok`, `verb`, `kind` and `rest` are null, `objects` is `[]` and the flags
are false. Nothing is guessed from the words of a statement that does not parse: a weak LLM's
broken SQL is to be refused, not routed.

### 3.2 `kind`

- **SQL:** `table`, `sequence`, `view`, `procedure`, `function` (stored or `SONAME`), `trigger`,
  `event`, `index`, `schema` (both `DATABASE` and `SCHEMA`).
- **REST:** `rest_service`, `rest_schema` (also `REST DATABASE`), `rest_view` (with or without
  `DATA MAPPING`), `rest_procedure`, `rest_function`, `rest_content_set`, `rest_content_file`,
  `rest_auth_app`, `rest_user`, `rest_role`.
- **`other`:** everything else: users, roles, servers, packages, `GRANT`, `SHOW`,
  `CONFIGURE REST METADATA`, DML.

New kinds may be added in format 1 (packages, for one). msm must treat a kind it does not
know as `other`, and likewise a verb.

### 3.3 `objects`

Each entry is `{schema, name}`, unquoted, the case as written, `schema` null when the name is
not qualified (it is not filled in from `current_schema`). Optional members:

- `table: {schema, name}`: the trigger's table in `CREATE TRIGGER ... ON t`, and the index's
  in `CREATE`/`DROP INDEX ... ON t`. Never resolved: `DROP TRIGGER` has none.
- `new_schema`, `new_name`: for `RENAME TABLE a TO b` (one entry per pair) and for
  `ALTER TABLE ... RENAME TO`, `ALTER EVENT ... RENAME TO`.

`DROP TABLE` / `DROP VIEW` / `DROP SEQUENCE` list every name. For `USE db` and
`CREATE`/`ALTER`/`DROP SCHEMA`, the one entry is `{schema: null, name: "db"}`. For
`rest_auth_app`, `rest_role` and `rest_user`, the entry is the app, role or `user@app`, with
`schema: null`.

### 3.4 `status`

| Value | When |
| --- | --- |
| `ok` | the statement parses |
| `syntax-error` | it does not parse under its `sql_mode`; `message`, `error_range` |
| `unsupported` | the lexer cannot read it (invalid UTF-8, an `sql_mode` the parser does not model) |
| `source-directive` | an MSM `SOURCE '<path>'` directive (not parsed, as in `check`) |
| `several-statements` | list input: the item splits into more than one statement |
| `empty` | list input: the item is blank or only comments |

### 3.5 `rest`

```text
{"service": {"path", "developers", "written"} | null,
 "schema":  {"path", "written"} | null,
 "path": string | null,
 "new_path": string | null,
 "database_schema": string | null}
```

- **`service`, `schema`:** the REST service and schema the statement acts on or in.
  - For a `rest_service` statement, `service` is its subject; for a `rest_schema` statement,
    `schema` is its subject and `service` the service it is on.
  - `written: true` when the statement names it (`ON SERVICE`, `FROM SERVICE ... SCHEMA`,
    `USE REST SERVICE`).
  - `written: false` when it is taken from what is in use at that statement in this input,
    resolved exactly as the outline's REST fold does (feature `rest_current_schema`; rules in
    `docs/outline-format.md`, "REST objects"): only `USE REST SERVICE` sets the service, and
    it clears the schema; `USE REST SCHEMA` sets the schema; the first `CREATE REST SCHEMA` on
    the current service makes it current.
  - Null when the statement names none and none is in use. That is not a note: msm's
    statements are standalone, so most will report null for what they do not name.
- **`path`:** the request path of the statement's own item (view, procedure, function, content
  set, content file). Null for service and schema statements, whose paths are in `service` /
  `schema`, and for auth apps, users and roles (in `objects`).
- **`new_path`:** `NEW REQUEST PATH` of an `ALTER`.
- **`database_schema`:** the database schema in `CREATE`/`ALTER REST SCHEMA ... FROM db` and
  in `DROP REST SCHEMA ... FOR SCHEMA db`.
- **Paths:** as written, quotes kept, as the outline's REST `path`. A `CREATE REST SCHEMA`
  without a path reports `/<database schema>`, as the shell and the outline do.
- **`developers`:** unquoted and sorted as MariaDB Shell sorts them (without regard to case),
  as the outline's `rest_settings` does; `[]` for none. `mike, anna@/svc` gives
  `{"path": "/svc", "developers": ["anna", "mike"]}`.
- **MSM placeholders** (`/*<msm:NAME>*/`) are comments to the lexer and do not appear in names
  or `database_schema`.

## 4. Design

- **Engine:** `include/mdbls/describe.h` and `src/describe.cc` in `libmariadblanguageserver`,
  JSON-free (`Statement_description`, `describe_statements(text, index, options)`). The JSON
  writer goes beside it, as `outline_json` does.
- **Parsing:** each statement is parsed through `Parser_cache` with its own `sql_mode` and the
  version id, so the verdict equals `check`'s.
- **Reading the tree:** keyed by grammar symbol through `catalog_symbols.inc` (`SYM()`), as
  `catalog.cc` does, never by words.
  - The statement node is `verb_clause`'s child (`drop`, `create`, `alter`, `rename`,
    `use`, `mrs_statement` → `mrs_*_statement`).
  - The flags come from `create_or_replace`, `opt_if_exists`, `opt_if_not_exists`,
    `opt_temporary`, and for REST from `mrs_opt_if_exists`, `mrs_opt_if_not_exists` and the
    `OR_SYM REPLACE` terminals the REST CREATE rules spell out.
  - Names come from `table_ident`/`sp_name` under `table_list` etc.
  - REST selectors come from `mrs_opt_from_service_schema_selector` /
    `mrs_service_schema_selector` and the `ON SERVICE`/`SCHEMA` rules.
- **What is in use:** `describe` folds its input through a `Catalog`, one statement at a
  time with `Catalog::apply`, as `build_outline` does, and reads it before each statement:
  - `Catalog::rest_context()` for the REST service and schema. The fold keeps them in a
    `Rest_context` (`catalog.h`), whose operations are the shell's rules (step 1, done), so
    `describe` and the outline cannot resolve differently.
  - `Catalog::current_schema()` for `current_schema`.

  A catalog is needed, not just the `USE` statements: the "first schema becomes current"
  rule depends on whether the service already has a schema, and a service gets schemas
  from `CREATE REST SCHEMA`, from objects that only name a schema, and loses them by `DROP`.
  The fold's results (objects, notes) are not used.
- **Shared:** `schema_edit.cc`'s `subject_of` is a word scan of the same facts (next step 4
  of the context wants it gone). Once `describe` exists, `subject_of` should read
  `Statement_description` for statements that parse.
- **CLI:** `src/mariadb-lsp/main.cc`: the `describe` command, `--statements FILE`, and a help
  line starting `  describe `.
- **Docs:** `docs/describe-format.md` (the output format, as `docs/edit-format.md` is for
  `edit`); the command in `docs/language-server.md`.
- **Tests:**
  - `unittest/mariadblanguageserver/describe_t.cc`: every verb and kind; flags; multi-name
    DROPs; RENAME pairs; REST resolution across `USE REST`; developers; each status.
  - `tests/e2e/test_describe.py`: both inputs, exit codes, and the three statements of §1.
  - A check that every `mrs_*_statement` of the grammar maps to a verb and kind, or is
    listed as `other` on purpose. A new REST statement then shows up as a failing test
    rather than as `other`.

## 5. Out of scope

- Whether the objects exist, and what the statement would change: that is the outline's and
  `edit`'s job.
- Statements inside routine bodies, compound statements, and `SOURCE` expansion.
- Routing to `edit` ops: that is msm's (decision of 2026-11-12: only msm decides where
  statements go).

## 6. Decisions taken here (to confirm with the user)

1. A new command `describe`, not `split --describe` nor an outline feature (§2).
2. List input (`--statements`), so msm need not join statements or write `DELIMITER`.
3. `kind: "schema"` for `DATABASE`/`SCHEMA`, not `database`: the outline calls them schemas,
   and what this repository writes says `SCHEMA`.
4. `index`, `temporary`, `rest_content_file`, `rest_auth_app`, `rest_user`, `rest_role`,
   `new_name`, `new_path`, `database_schema` and `current_schema`, beyond what msm asked for.
   All are cheap from the tree and spare msm a second look.
5. Statements that do not parse get no verb or kind, no guess from their words.

## 7. Steps

1. `Rest_context` out of `Catalog_fold::rest`, no change in behaviour (outline tests unchanged),
   and `Catalog::rest_context()`. Done: test `Catalog.rest_context_after_each_statement`.
2. `describe` engine, JSON, CLI with text input; `docs/describe-format.md`; tests.
3. List input `--statements`.
4. `subject_of` on top of `Statement_description` (optional, behaviour kept).
5. msm switches its routing and drops `_CREATE` / `_IF_NOT_EXISTS` (mariadb-shell-plugins).
   msm uses `--statements` and routes by `index`, so it needs steps 2 and 3 in one release
   pinned in MariaDB Shell: land them in the same release.

**msm's use (its reply, 2026-11-28):** it refuses every entry whose `status` is not `ok`
(with `message`), a qualified `objects[].schema` (MSM scripts name objects unqualified),
`kind: "schema"`, and for now any REST `written: false`: a statement relying on a `USE REST`
would land in Section 180 under another context. So whether `USE` / `USE REST` carry across
list items does not matter to msm; they carry, as in a text, because that is the simpler rule.
