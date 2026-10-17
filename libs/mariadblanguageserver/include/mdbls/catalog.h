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

#ifndef MDBLS_CATALOG_H_
#define MDBLS_CATALOG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/syntax_tree.h"

namespace mdbls {

// Byte offsets in the document an object was defined in.
struct Range {
  uint32_t begin = 0;
  uint32_t end = 0;
};

// Where an object was defined: the whole statement, and its name, in the
// text of source `source` (0 for a catalog folded from one document; the
// file's position in the list for a workspace catalog).
struct Origin {
  Range statement;
  Range name;
  uint32_t source = 0;
};

struct Column {
  std::string name;
  std::string type;                    // as written, blanks collapsed: "varchar(20)"
  bool nullable = true;
  std::optional<std::string> default_value;  // the expression as written
  bool auto_increment = false;
  std::optional<std::string> comment;
  std::optional<std::string> collation;
  std::optional<std::string> generated;      // the generation expression
  Origin origin;

  // "varchar(20) NOT NULL DEFAULT 'x' AUTO_INCREMENT"
  std::string attributes() const;
};

struct Index {
  std::string name;                    // "" when unnamed
  std::string kind;                    // primary, unique, index, fulltext, spatial, vector
  std::vector<std::string> columns;
  Origin origin;
};

struct Foreign_key {
  std::string name;                    // "" when unnamed
  std::vector<std::string> columns;
  std::string ref_schema;              // "" when not qualified
  std::string ref_table;
  std::vector<std::string> ref_columns;
  std::string on_delete;               // "" when not given
  std::string on_update;
  Origin origin;
};

struct Check {
  std::string name;
  std::string expression;
  Origin origin;
};

struct Table {
  std::string name;
  bool temporary = false;
  std::vector<Column> columns;
  std::vector<Index> indexes;
  std::vector<Foreign_key> foreign_keys;
  std::vector<Check> checks;
  // engine, charset, collation, comment, auto_increment, row_format, ...
  std::vector<std::pair<std::string, std::string>> options;
  Origin origin;

  Column *column(std::string_view name);
  const Column *column(std::string_view name) const;
};

struct View {
  std::string name;
  std::vector<std::string> columns;    // the explicit column list, if any
  std::string definition;              // the SELECT as written
  Origin origin;
};

struct Parameter {
  std::string mode;                    // IN, OUT, INOUT, or "" (IN)
  std::string name;
  std::string type;
};

struct Routine {
  std::string name;
  std::string kind;                    // procedure, function
  std::vector<Parameter> parameters;
  std::string returns;                 // functions only
  Origin origin;

  // "IN a INT, b TEXT"
  std::string parameter_list() const;
};

struct Trigger {
  std::string name;
  std::string timing;                  // BEFORE, AFTER
  std::string events;                  // INSERT, UPDATE OR DELETE, ...
  std::string table;
  Origin origin;
};

struct Named_object {
  std::string name;
  Origin origin;
};

struct Schema {
  std::string name;                    // "" for objects whose schema is not known
  bool declared = false;               // created by CREATE DATABASE in the file
  std::vector<std::pair<std::string, std::string>> options;  // charset, collation, comment
  std::vector<Table> tables;
  std::vector<View> views;
  std::vector<Routine> routines;
  std::vector<Trigger> triggers;
  std::vector<Named_object> events;
  std::vector<Named_object> sequences;
  Origin origin;

  Table *table(std::string_view name);
  const Table *table(std::string_view name) const;
  const View *view(std::string_view name) const;
  const Routine *routine(std::string_view name) const;
  bool empty() const;
};

struct Rest_object {
  std::string kind;                    // view, procedure, function
  std::string path;
  std::string object;                  // the database object, as written
  Origin origin;
};

struct Rest_schema {
  std::string path;
  std::string schema;                  // the database schema
  std::vector<Rest_object> objects;
  Origin origin;
};

struct Rest_service {
  std::string path;
  std::vector<Rest_schema> schemas;
  std::vector<Named_object> content_sets;
  Origin origin;
};

// The schema objects a script defines, folded statement by statement in file
// order: CREATE adds, ALTER changes, DROP removes, RENAME renames, USE sets
// the schema unqualified names belong to.  Names compare without regard to
// case, as the server does on case-insensitive file systems; they are kept
// as written.
class Catalog {
 public:
  // Applies one accepted statement.  `tree` is its parse tree; `text` the
  // document its offsets point into.  Returns false for a statement that
  // changes nothing in the catalog.
  bool apply(const Syntax_tree &tree, std::string_view text);

  const std::vector<Schema> &schemas() const { return m_schemas; }
  const std::vector<Rest_service> &rest_services() const { return m_services; }
  const std::vector<Named_object> &rest_auth_apps() const { return m_auth_apps; }
  const std::vector<Named_object> &rest_roles() const { return m_roles; }
  const std::vector<Named_object> &rest_users() const { return m_users; }
  const std::string &current_schema() const { return m_current; }
  const Schema *find(std::string_view schema) const;

  // The source index new origins get (see Origin); 0 by default.
  void set_source(uint32_t source) { m_source = source; }

  // A catalog made of `schemas` as they are, for one read from a server.
  static Catalog from_schemas(std::vector<Schema> schemas);

  // Things the fold could not apply, such as ALTER TABLE of a table the
  // script never created: (statement offset, message).
  const std::vector<std::pair<uint32_t, std::string>> &notes() const { return m_notes; }

 private:
  Schema &schema(const std::string &name);
  Schema *find_schema(std::string_view name);
  Rest_service &service(const std::string &path);
  Rest_service *find_service(std::string_view path);

  std::vector<Schema> m_schemas;
  std::string m_current;
  std::vector<Rest_service> m_services;
  std::string m_current_service;
  std::vector<Named_object> m_auth_apps;
  std::vector<Named_object> m_roles;
  std::vector<Named_object> m_users;
  std::vector<std::pair<uint32_t, std::string>> m_notes;
  uint32_t m_source = 0;

  friend class Catalog_fold;
};

// Whether a statement can change the catalog: its first word is CREATE,
// ALTER, DROP, RENAME or USE (also inside a versioned comment).  A cheap test
// on the text, so that a dump's INSERTs are never parsed for the outline.
bool may_change_catalog(std::string_view statement);

// The grammar rule and token names the fold looks for that the grammar does
// not have; empty when all are there (a test checks this).
std::vector<std::string> catalog_missing_grammar_symbols();

}  // namespace mdbls

#endif  // MDBLS_CATALOG_H_
