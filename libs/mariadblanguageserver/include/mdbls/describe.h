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

#ifndef MDBLS_DESCRIBE_H_
#define MDBLS_DESCRIBE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/line_map.h"
#include "mdbls/parser_pool.h"
#include "mdbls/sql_mode.h"
#include "mdbls/statement_index.h"

// What each top-level statement of a SQL text does, read from its parse
// tree (`mariadb-lsp describe`, docs/describe-format.md).
namespace mdbls {

enum class Describe_status : uint8_t {
  ok,                  // the statement parses
  syntax_error,        // it does not parse under its sql_mode
  unsupported,         // the lexer cannot read it (invalid UTF-8, an unsupported sql_mode)
  source_directive,    // an MSM SOURCE directive, or a statement holding one
};

// A name as written, unquoted; `schema` empty when it is not qualified.
struct Object_name {
  std::optional<std::string> schema;
  std::string name;
};

struct Described_object {
  Object_name name;
  std::optional<Object_name> table;     // a trigger's or an index's table
  std::optional<Object_name> new_name;  // RENAME TABLE, ALTER TABLE/EVENT ... RENAME TO
};

struct Described_rest_service {
  std::string path;                     // the request path as written, quotes kept
  std::vector<std::string> developers;  // unquoted, sorted as MariaDB Shell does
  bool written = false;                 // named by the statement, not taken from what is in use
};

struct Described_rest_schema {
  std::string path;
  bool written = false;
};

struct Described_rest {
  std::optional<Described_rest_service> service;
  std::optional<Described_rest_schema> schema;
  std::optional<std::string> path;             // the statement's own item: view, procedure, ...
  std::optional<std::string> new_path;         // NEW REQUEST PATH
  std::optional<std::string> database_schema;  // FROM db, FOR SCHEMA db
};

struct Statement_description {
  size_t statement = 0;  // into the index's statements
  Describe_status status = Describe_status::ok;
  std::string message;   // why it is not ok
  uint32_t error_begin = 0, error_end = 0;  // syntax_error: the offending token
  // Empty unless ok: "create", "alter", "drop", "rename", "use", "other";
  // "table", "view", ..., "rest_service", ..., "other".
  std::string verb;
  std::string kind;
  bool or_replace = false;
  bool if_exists = false;
  bool if_not_exists = false;
  bool temporary = false;
  std::vector<Described_object> objects;
  std::optional<std::string> current_schema;  // the USE in force before it
  std::optional<Described_rest> rest;         // REST SQL only
};

struct Describe_options {
  unsigned long version_id = k_default_version_id;
  int priority = 0;
};

// One description per SQL statement of `index` (comments left out), in
// file order.  The statements are folded through a Catalog as `outline`
// folds them, so `current_schema` and the REST service and schema in use
// are those the outline resolves.
std::vector<Statement_description> describe_statements(std::string_view text, const Statement_index &index,
                                                       Parser_pool &pool, const Describe_options &options = {});

// {"format": 1, "statements": [...]} (docs/describe-format.md).
std::string describe_json(const std::vector<Statement_description> &descriptions, std::string_view text,
                          const Statement_index &index, const Line_map &lines);

}  // namespace mdbls

#endif  // MDBLS_DESCRIBE_H_
