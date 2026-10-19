# SOURCE includes

Written 2026-10-19 on branch `wip/core-impl`. Makes the language server and the CLI read
the `SOURCE '<path>'[<start>:<end>];` statements of MariaDB Schema Management (MSM)
scripts. The outline of such a script then holds its own objects and those of the files it
includes, and the `SOURCE` lines are no longer syntax errors. The test case is MariaDB Shell's
`mysqlshdk/libs/mrs/db_schema/mariadb_rest_service.msm.project/development/mariadb_rest_service_next.sql`.

## 1. What `SOURCE` means

MSM is the shell's `msm_plugin` (mariadb-shell-plugins). Its `prepare_release` expands
`SOURCE` before it cuts a release (`substitute_source_statements_with_content`,
`msm_plugin/lib/management.py`). It is a text preprocessor, not the client's `source`
command:

- A directive is a line `^[ \t]*SOURCE\s+'<path>'[<start>:<end>];<rest of line>`. `SOURCE` is
  uppercase, the quotes are `'` or `"` and must match on the directive's line, white space is
  ASCII (`\s` under `re.ASCII`), and the rest of the line (a comment)
  and its line break are replaced along with the directive. The scan does not know SQL: it
  also matches inside a routine body or a block comment, and it does not skip a directive
  in a `--` comment because the line must start with `SOURCE`.
- A relative path is relative to the folder of the script (`development/`).
- The slice is optional and selects part of the file:
  - Without a slice, or with `[:]`, the whole file is taken.
  - A bound is empty, a character offset, or a line number `lnN`, and the two bounds can be of either kind.
  - Character offsets follow Python slice rules: they count code points of the file read in text mode, so `\r\n` and a lone `\r` count as one `\n`, and a negative offset counts from the end.
  - Line numbers are 1-based, the end line is included, and `ln-1` is the last line. Only `\n` ends a line.
  - Bounds past either end of the file are clamped, and `ln0` is an error.
- The directive's indentation is added to every included line.
- Expansion goes one level deep: a `SOURCE` line in an included file stays as it is.
- The directive can sit inside a statement. The test file's `restore_roles` procedure has
  one in its `BEGIN ... END`, so the roles file becomes part of that procedure's body.

Before 2026-10-19 the slice was required: msm left a directive without one in the release
script (where it failed), `[:]` made `prepare_release` fail, and there were no line
numbers. That is fixed on mariadb-shell-plugins branch `wip/msm-source-lines`; the rules
above are the fixed ones. The agent skill `mariadb-schema-management-develop`
(ai-plugins) still says the slice is required.

### What it gives on the test file

| | today | expanded as msm does |
| --- | --- | --- |
| `mariadb-lsp check` | 11 syntax errors, one per `SOURCE` | none |
| `mariadb-lsp outline` | 1 view | 34 tables, 4 views, 11 routines, 115 triggers, 4 events; 312 DDL statements, 311 applied, no notes |

The included sections have no `USE` and no `CREATE SCHEMA`: their objects belong to
`mariadb_rest_service` only because the including script's `USE` comes before them. They
must be folded in the including script's context, not on their own.

## 2. Decisions (the user, 2026-10-19)

1. A directive without a slice includes the whole file. msm not expanding it was a bug,
   fixed in msm (§1). Slices take line numbers `lnN` as well as character offsets: 1-based,
   end line included.
2. The client's `source file` and `\. file` (no quotes, path relative to the client's working
   directory) are not supported.
3. The outline view keeps its schema → object tree; an object from an included file names
   that file in its detail.
4. A file that is both included and listed in `catalogFiles` is folded only through the
   include, so its objects are not there twice.

## 3. Design

### 3.1 Expand, then map back

The included text is folded the way msm deploys it. The script is expanded into one text,
that text is indexed and folded, and every offset found in it is mapped back to the file
and offset it came from. Folding each file on its own, which `fold_outline` already supports, was ruled out: it
would get four things wrong:

- the delimiter, `sql_mode` and current schema an included file starts with and leaves behind;
- a directive inside a statement (the procedure body).

Re-indexing the expanded text on every outline rebuild costs little: the test project is
270 KB, and the outline is already rebuilt from the whole document per generation.

The expanded text leaves out msm's indentation. It only changes whitespace between tokens
(and the content of a multi-line string, which the outline does not read), and without it
every segment maps 1:1.

### 3.2 `source_include` (new: `include/mdbls/source_include.h`, `src/source_include.cc`)

- `find_source_directives(text)`: the msm pattern as a line scan, giving for each one the
  byte range of the line it replaces (line break included), the path, the two bounds as
  written, and the range of the path (for links and diagnostics).
- `slice_range(text, start, end)`: the byte range a slice selects in a UTF-8 text, by
  Python's rules over code points with `\r\n` counted as one character, and by `\n` lines
  for `lnN`; an error message for `ln0` and for malformed bounds.
- `Expansion expand_sources(text, base_dir, reader)`: the expanded text, a sorted segment
  list (`{expanded_begin, length, source, source_begin}`), and the sources: source 0 is
  the document, and source k is include k, with its path, URI, full text, `Line_map`,
  directive range and selected byte range, or an error. `reader(path)` returns
  a file's text: from the open document when the editor has it open, otherwise from disk
  through `read_sql_file`. Nested directives are left as text and listed for a warning.
- `Expansion::map(offset) -> {source, offset}`, and `map_range(begin, end)`. A range
  that starts and ends in different sources is the procedure case: it maps to the source of
  its begin and is clipped to the end of the directive there.

### 3.3 Outline

- `build_outline` takes an optional `Expansion`. It indexes the expanded text with the
  document's `Index_options`, folds it, and maps every `Origin` (statement and name) and
  every note back.
- `Origin::source` is 0 for the document and k for include k.
- `Outline` gains `sources`: the include list with paths, URIs, line maps and errors.
- The workspace catalog uses the same table. Its files are sources too, and a listed file
  that some listed file includes is folded only through the include (decision 4).

### 3.4 Diagnostics

- A statement that is only a directive (top level) is not parsed. It gets:
  - an error on the path when the file cannot be read;
  - an error on the slice when a bound is malformed or `ln0`;
  - a warning when the slice selects nothing.
- A nested directive in an included file gets a warning at the including directive: msm
  will not expand it.
- A statement that contains directive lines (the procedure) is parsed with those lines
  blanked to spaces, so its offsets stay those of the document. (The first draft checked it
  expanded. Blanking keeps the check a pure function of the document's text, as the
  incremental checks on the parser threads need, and follows the rule below.)
- The included files' own statements get no diagnostics in the including document. They
  get them when the file itself is opened.
- `Check_options::source_directives` (CLI `--no-source`) turns all of this off: directives
  are then checked as SQL.

### 3.5 Server

- Each open document keeps its expansion's file list. A map from file to the open documents
  that include it marks their outline caches stale on:
  - `didChange` of an included file that is open;
  - `didSave` and `didClose` of it, since the disk text applies again after a close;
  - `workspace/didChangeWatchedFiles`.
  An untitled document resolves only absolute paths; any other path gets the "cannot be
  read" error.
- `textDocument/documentSymbol`: the tree stays as it is (decision 3). An object from an
  include has the directive's range as `range` and `selectionRange`, since LSP allows only
  ranges inside the document, and its `detail` ends with a middle dot and `<file name>:<line>`.
- Definition and hover: an origin with `source` > 0 points into the included file, with its
  URI and positions from that file's line map. The workspace catalog already does this.
- Completion, hover and signature help see the included objects through the document's
  outline (`Catalog_sources::document`) and need no change.
- References and rename stay document-only. Renaming an object defined in an include is
  refused with a message naming the file.
- New `textDocument/documentLink`: the path of every directive links to its file.

### 3.6 CLI and outline JSON

- `check` and `outline` expand directives. `--no-source` turns that off, which brings
  back today's behaviour (syntax errors).
- Outline format 1 gains two fields:
  - a top-level `includes` array: `{file, range, slice, error?}`, where `range` is the directive's range in the document and `file` is the path as resolved;
  - a `source` field on objects that come from an include: the index into `includes`. The object's `range` and `selection_range` are then positions in that file.

  Both fields are additive, so the format stays 1. The vscode-extension's reader of
  `mariadb/outline` must be checked to make sure it does not assume every range lies in the document.
- The formatter leaves directives alone. `may_format` already refuses a statement starting
  with `SOURCE` and every routine, so no change is expected; a test confirms it.

## 4. Steps

1. `source_include` with unit tests: the pattern (quotes, missing slice, `[:]`, trailing
   comment, last line without a break, lowercase `source` not matched), slices
   (offsets, negative, `lnN`, `ln-N`, mixed, clamped, `ln0`, multibyte UTF-8, `\r\n`,
   lone `\r`, `\f` not a line break), and offset mapping across segments.
2. Outline over an expansion; `Origin::source` and `Outline::sources`; the workspace
   catalog on the same source table; decision 4.
3. Diagnostics (§3.4) in `syntax_check` and the server's check jobs.
4. Server: the reader backed by open documents; the dependency map and invalidation;
   document symbols, definition, hover, rename refusal, `documentLink`.
5. CLI: expansion in `check` and `outline`, `--no-source`, the JSON fields.
6. Tests:
   - A small MSM project under `tests/fixtures/sql/msm/` with a top-level `SOURCE`, a line-number slice, a slice without bounds, a `SOURCE` in a procedure body, and a nested one.
   - e2e tests covering document symbols, definition into an include, diagnostics, editing an open included file, and `documentLink`.
   - The real test file must give the numbers in §1 and no diagnostics.
7. Docs: `docs/language-server.md` (includes, documentLink), `docs/outline-format.md`
   (`includes`, `source`), the status paragraph here, `.claude/PROJECT_CONTEXT.md`.

## 5. Later

- An included file opened on its own lacks its includer's context: no current schema, and the
  wrong delimiter or `sql_mode` for a body section. It could take that context from an open
  document that includes it.
- Completion of paths inside `SOURCE '...'`.
- References and rename across includes.
- Caching each include's index, if outline rebuilds on large includes show up in profiles.

## Status

**Checked before review (2026-10-19, at `c38ded5`):**

- **Debian 13 (GCC 14.2, aarch64) and Windows 11 (MSVC, ARM64):** the build passes (no new warnings), 810 unit tests pass (one skip: no MariaDB Shell there), and the e2e suite passes (66 tests, the 10 live-connection ones skipped).
- **Windows bug:** these runs found that `path_to_file_uri` wrote `file:///C%3A/...` where VS Code writes `file:///c%3A/...`; fixed.
- **256 MB dump, optimized build, against `c2954dc` (before P9):**
  - edit to diagnostics, median: 3.2–3.3 ms both;
  - full check after `didOpen`: 1.6 s both;
  - `mariadb-lsp check`: 0.53 s both;
  - `mariadb-lsp outline`: 3.5 → 8 ms, the expansion's directive scan.
- **Slowdown found and fixed:** a first version scanned the whole text after every check pass, nearly doubling edit-to-diagnostics latency. The checks now record whether a statement holds a directive.

**The four open items of §5 are done (2026-10-19):**

- **Hover** names the file and line of an object a file the document includes defines.
- **Path completion** in `SOURCE '...'`: folders and `.sql` files, with `/` as a trigger that answers an empty list outside a SOURCE path (`source_path_at`, `source_path_candidates`).
- **Context for an included file opened on its own:** `build_outline` records for each include the script's schema, `sql_mode` and delimiter where it begins (`Source_context`; `;` inside a routine body). The server applies them, below anything the client set, to an open file an open script includes (`apply_include_contexts`, the first script by URI), and drops them when no script includes it any more. `Outline_options::schema` gives a document's outline its initial schema, from that context or from the client's `currentSchema`. This needed `schema_in_use` to skip comments after `USE`: MSM writes `USE /*<msm:schema_prefix>*/name/*<msm:schema_postfix>*/`.
- **References and rename across includes:** the outline keeps the expanded text and its index (`Outline::expanded`, `Outline::map`). References search that text and map each hit back to its file, a file included twice listed once. Rename writes edits per file and no longer refuses included objects; references now use a fresh outline, as rename does.

They came with 2 more unit tests and 3 more e2e tests (`test_path_completion`, `test_included_file_takes_the_script_context`, and references, rename and hover in `test_navigation_into_an_include`). On the REST project, every section with objects lists them under `mariadb_rest_service` once the script is open.

Also done: references and rename requested from an included file run in the open script that includes it (`Including_script`, `Ref_scope` in `language_features.cc`):

- The request's position moves into the script's expanded text, once the include's text is checked to be the file's current one.
- The results come back per file, the requesting one under its client's URI. Otherwise the request falls back to the file alone.
- e2e `test_navigation_from_an_included_file`.

The vscode-extension needed no code: it does not read `mariadb/outline`, and links, the `/` trigger and the file watcher registration come through the language client library. Its notes, changelog and an LSP UI test cover SOURCE (branch `wip/lsp-indentation`).

**Done (2026-10-19): steps 1 to 7.** Step 6's fixture and tests came with steps 2 and 4. Step 7 documents SOURCE in `docs/language-server.md` (a "SOURCE includes" section, the capability rows, the `source` diagnostic code), `docs/outline-format.md` (`includes`, `source`) and the README. Open items are in §5, plus checking the vscode-extension against the `source` field once it reads `mariadb/outline`, and `db.execute_sql_script` in the MCP plugin, which should expand SOURCE too (a prompt for that work was handed over; nothing is implemented yet).

Step 1 is done (2026-10-19). `source_include.{h,cc}` provides
`find_source_directives`, `slice_source`, `expand_sources` and `Source_expansion::map`,
with 8 unit tests (`source_include_t.cc`). On the REST project's development script,
`expand_sources` gives the same 292,661 bytes as msm's own expansion (without msm's
indentation).

Step 5 is done together with step 4 (2026-10-19), because the server's expanded outlines need it in `mariadb/outline`. Outline JSON format 1 gains:

- `includes`: `file`, `range` (the directive), `slice` (as written, or null) and `error`;
- `source` on an object from an include, its index in `includes`, with `range` and `selection_range` in that file.

`mariadb-lsp outline` expands directives, and `--no-source` turns that off. On the REST project script, the CLI gives the numbers in §1 with 11 includes. One more e2e test (`test_outline_json`). Still to check: whether the vscode-extension's reader of `mariadb/outline` handles `source`.

Step 4 is done (2026-10-19). The server:

- **Outline:** `outline_of` expands the document's directives, relative to its folder. An included file is read from the editor's text when it is open, else from disk (`Include_texts`, kept by the loop thread to the open documents that some document includes).
- **Bookkeeping:** after each outline the loop thread learns which files the document includes and what is wrong with its directives (`on_outline_includes`). It publishes these problems as diagnostics with code `source`.
- **Background rebuilds:** after each check pass, a background job folds the outline of a document that has directives. An edit to an open included file, closing it, or a `workspace/didChangeWatchedFiles` for an included file marks the includers' outlines stale (`Outline_cache::epoch`) and folds them anew. The server registers a `**/*.sql` watcher when the client allows dynamic registration.
- **LSP features:**
  - Document symbols put an included object on its directive, with `· file:line` in the detail.
  - Definition goes into the included file, and workspace symbols list included objects there.
  - Rename refuses an included object, naming its file and line.
  - New: `textDocument/documentLink` on every directive's path.

e2e tests (`tests/e2e/test_source.py`) cover these. On the REST project script the server lists 168 objects under `mariadb_rest_service` (35 ms for the first `documentSymbol`), publishes no diagnostics, and gives 11 links.

Step 3 is done (2026-10-19), for the library and the CLI:

- `Check_status::source_directive`: a statement that is a directive is not parsed. A statement with directive lines in it is parsed with them blanked. `find_source_directives(text, from, to)` limits the search to one statement, so checking a dump stays linear.
- `source_problems(expansion)` lists the errors (unreadable file on the path, malformed slice on the slice) and the warnings (nothing selected, a nested directive).
- `mariadb-lsp check` reports these problems: errors in `errors`, warnings in `warnings` and as `warning:` lines. `--no-source` checks directives as SQL.
- The server already gets the new check status; step 4 adds the problems to its diagnostics.
- 3 more unit tests. The REST project's development script checks without a diagnostic (exit 0).

Step 2 is done (2026-10-19). Three pieces:

- `build_outline(Source_expansion, ...)` and the expansion overload of `fold_outline` fold the expanded text with a placeholder source, then map the origins carrying it back through `Catalog::for_each_origin`.
- Notes in an included text move to their directive and name the file and line (`140_tables.sql:13: ...`). An unreadable include and a nested directive get a note at their directive.
- The workspace catalog expands its files, adds included files to `files`, and folds a listed file that another listed file includes only through that include.

Fixture: `tests/fixtures/sql/msm/` (a script, four sections, a body-level and a nested directive); 3 more unit tests. On the REST project the library gives the numbers in §1, each object in its section file, in about 29 ms (debug build).

The scan keeps the path on the directive's line (which keeps it linear) and takes `\s` as
ASCII white space. msm's regular expression was changed to match it, in the same PR.

The msm fix and the line numbers are mariadb-shell-plugins PR #60 (`wip/msm-source-lines`),
with a unit test; `test_prepare_release` and `test_msm_sections` still pass, and the REST
project's development script expands to the same text as before. The skill and DevHub text
for them is ai-plugins PR #61, which should merge after #60.
