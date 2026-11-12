/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MDBLS_SCHEMA_EDIT_H_
#define MDBLS_SCHEMA_EDIT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mdbls/completion.h"
#include "mdbls/formatter.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/source_include.h"
#include "mdbls/statement_index.h"

namespace mdbls {

// Structured schema edits applied to a script and the files its SOURCE
// directives include (`mariadb-lsp edit`, docs/edit-format.md): the edits
// come back as text edits per file, with the DDL that migrates a deployed
// schema and the outline of the edited texts.  Nothing is written.

enum class Edit_op {
  rename_table,
  rename_column,
  rename_view,
  rename_routine,
  add_table,
  drop_table,
  add_column,
  modify_column,
  drop_column,
  add_index,
  drop_index,
  add_foreign_key,
  drop_foreign_key,
  set_table_options,
  add_view,
  drop_view,
  add_routine,
  drop_routine,
  set_primary_key,
  add_trigger,
  drop_trigger,
  rename_trigger,
  add_event,
  drop_event,
  rename_event,
  add_sequence,
  drop_sequence,
  rename_sequence,
  rename_schema,
  replace_view,
  replace_routine,
  replace_trigger,
  replace_event,
};

// The request's `op` spelling; nullopt for an unknown one.
std::optional<Edit_op> edit_op_from_string(std::string_view op);
const char *to_string(Edit_op op);
// Every op this binary does, space-separated, in the order of Edit_op:
// the `edit ops:` line of `mariadb-lsp --help`.
std::string supported_edit_ops();
// The same, for programs: {"format": 1, "ops": [...]}, what
// `mariadb-lsp edit --ops` prints.
std::string edit_ops_json();

// One edit of a request.  Which members it uses depends on the op.
struct Schema_edit {
  Edit_op op = Edit_op::rename_table;
  std::string table;
  std::string column;
  std::string view;
  std::string routine;
  std::string routine_kind;  // procedure, function, or "" when not given
  std::string trigger;       // drop_trigger, rename_trigger
  std::string event;         // drop_event, rename_event
  std::string sequence;      // drop_sequence, rename_sequence
  std::string schema;        // rename_schema, MSM placeholders included
  bool comments = false;     // rename_schema: also `schema` in comments
  std::string new_name;
  // add_table: the table; its columns, keys, checks, options.
  std::optional<Table> new_table;
  // add_column: the column; modify_column: what `column` becomes.
  std::optional<Column> new_column;
  std::string after;   // add_column: the column it goes after, or ""
  bool first = false;  // add_column: first
  std::optional<View> new_view;        // add_view, replace_view
  std::optional<Routine> new_routine;  // add_routine, replace_routine
  std::optional<Trigger> new_trigger;  // add_trigger, replace_trigger
  std::optional<Event> new_event;      // add_event, replace_event
  std::optional<Sequence> new_sequence;  // add_sequence
  // set_table_options: outline option name and value; nullopt removes it.
  std::vector<std::pair<std::string, std::optional<std::string>>> options;
  std::optional<Index> new_index;              // add_index
  std::optional<Foreign_key> new_foreign_key;  // add_foreign_key
  // drop_index, drop_foreign_key: the key by `name`, by `kind` ("primary",
  // drop_index), or by `columns` (an unnamed key); set_primary_key: its columns.
  std::string name;
  std::string kind;
  std::vector<std::string> columns;
};

struct Edit_request {
  std::string schema;  // the outline schema the names belong to; "" for none
  std::vector<Schema_edit> edits;
  // The ALTER TABLE statements of a table become one where that runs as
  // they do one after another (merge_alter_statements).
  bool merge_alters = false;
};

// Why a request was refused.  `kind` is stable: bad-request, unsupported,
// unknown-object, name-taken, referenced, edit-outside-slice, self-check.
struct Edit_error {
  std::optional<size_t> edit;  // the edit's index, or none for the request
  std::string kind;
  std::string message;
};

// The request JSON (format 1) read, or why it cannot be.
std::variant<Edit_request, Edit_error> parse_edit_request(std::string_view json);

// A file the edits read: the script (files[0]) or an included file.
struct Edited_file {
  std::string path;      // resolved, as Source_include::path; "" for the script
  std::string original;  // as read
  std::string text;      // after the edits
  // `text` is `original` with these applied: sorted, not overlapping,
  // offsets in `original`.
  std::vector<Text_edit> edits;
};

// Something an edit could not do itself, for a person to check.  `kind` is
// stable: name-in-string, rest-reference, routine-recreated,
// routine-created, still-referenced, no-migration, trigger-created,
// trigger-recreated, event-created.
struct Edit_note {
  size_t edit = 0;
  std::string kind;
  size_t file = 0;                 // index in Edit_result::files
  std::optional<size_t> include;   // the first include of that file
  uint32_t at = 0;                 // offset in that file's original text
  std::string message;
};

// What a migration statement does (docs/edit-format.md, Migration):
// create and drop a table, view, routine, trigger, event or sequence; add
// and drop a table's member.
enum class Migration_action : uint8_t { create, drop, add, alter, rename };
enum class Migration_object : uint8_t {
  table, view, procedure, function, trigger, event, sequence,   // objects
  column, index, primary_key, foreign_key, table_options,       // a table's members
};
const char *to_string(Migration_action a);
const char *to_string(Migration_object o);

// What one ALTER TABLE clause names, for merge_alter_statements; never
// written out (public for its tests, as Edit_log).  Column and key names
// compare without regard to case.
struct Alter_key {
  std::string name;  // PRIMARY for the primary key
  bool foreign_key = false;
  bool add = false;  // added, else dropped
};
struct Alter_names {
  std::vector<std::string> defines;    // columns it adds, or renames to
  std::vector<std::string> modifies;   // columns it changes in place (MODIFY, ALTER COLUMN)
  std::vector<std::string> renames;    // columns it renames from (RENAME COLUMN, CHANGE COLUMN)
  std::vector<std::string> drops;      // columns it drops
  std::vector<std::string> refers;     // columns it names: key parts, AFTER
  std::vector<std::string> exclusive;  // columns no other clause of the statement may name
  std::vector<Alter_key> keys;
  std::vector<std::string> tables;  // other tables it depends on: a foreign key's
  bool expression = false;          // writes an expression that may name columns
};

// What a migration statement, or one clause of a merged ALTER TABLE, does.
struct Migration_clause {
  size_t edit = 0;
  Migration_action action = Migration_action::alter;
  Migration_object object = Migration_object::table;
  std::string name;      // the object's name before the edit (create, add: the new one's); PRIMARY for a primary key
  std::string table;     // a member's table; "" for an object
  std::string new_name;  // rename, and a column's alter that renames it; "" otherwise
  // An ALTER TABLE's text after `ALTER TABLE <table> `; "" for other
  // statements.
  std::string spec;
};

// One statement of the migration, with what it does to which object.
struct Migration_statement : Migration_clause {
  std::string sql;  // no schema qualifier, no delimiter
  // An ALTER TABLE: what each of its clauses names, for merging.
  std::vector<Alter_names> names;
  // After merge_alter_statements, an ALTER TABLE's clauses (one for a
  // statement it left alone); empty otherwise.
  std::vector<Migration_clause> clauses;

  bool is_alter_table() const { return !spec.empty(); }
};

// Makes the ALTER TABLE statements of one table one statement wherever that
// runs as they do one after another (checked on MariaDB 12.3): a statement
// joins the table's last one when no statement but ALTER TABLEs of other
// tables, that it does not depend on nor they on it, lies between them, and
// none of its clauses conflicts with one already there.  Other statements
// stay where they are.  Every ALTER TABLE gets its `clauses`.
void merge_alter_statements(std::vector<Migration_statement> &migration);

struct Edit_result {
  std::vector<Edited_file> files;  // the script, then each included file once
  std::vector<Migration_statement> migration;
  std::vector<Edit_note> notes;
  Outline outline;                 // of the edited texts
};

struct Edit_options {
  Index_options index;
  Format_options format;
  std::string source_dir;  // relative SOURCE paths are taken from here
  Source_reader read;      // reads included files; read_sql_file() when empty
  std::string schema;      // in use before the first statement
};

// Applies `request` to `script` and the files it includes, in order, each
// edit to the result of the ones before; all or nothing.  Uses `pool` for
// parsing, so it must not run on it.
std::variant<Edit_result, Edit_error> apply_schema_edits(std::string_view script,
                                                         const Edit_request &request,
                                                         Parser_pool &pool,
                                                         const Edit_options &options = {});

// The output JSON (format 1): `script_file` names the script ("-" for
// stdin), included files go by their path relative to `source_dir`.
std::string edit_result_json(const Edit_result &result, std::string_view script_file,
                             const std::string &source_dir);
std::string edit_error_json(const Edit_error &error);

// The edit log behind Edited_file::edits, public for its tests: edits given
// in the current text are kept as edits of the original one.
class Edit_log {
 public:
  explicit Edit_log(std::string original) : m_original(original), m_text(std::move(original)) {}

  const std::string &original() const { return m_original; }
  const std::string &text() const { return m_text; }
  const std::vector<Text_edit> &edits() const { return m_edits; }

  // Applies `edits`, given in the current text, not overlapping, in any
  // order.
  void apply(std::vector<Text_edit> edits);
  // Where offset `current` of the current text was in the original one: an
  // offset in text an edit wrote goes to that edit's start.
  uint32_t to_original(uint32_t current) const;
  // Where offset `original` of the original text is now; nullopt when an
  // edit replaced the text around it (strictly inside the edited range).
  // An offset at an edit's start stays before what the edit wrote unless
  // `after_insert`.
  std::optional<uint32_t> to_current(uint32_t original, bool after_insert) const;

 private:
  void apply_one(const Text_edit &e);

  std::string m_original;
  std::string m_text;
  std::vector<Text_edit> m_edits;
};

}  // namespace mdbls

#endif  // MDBLS_SCHEMA_EDIT_H_
