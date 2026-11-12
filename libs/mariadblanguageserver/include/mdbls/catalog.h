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

#include <compare>
#include <cstdint>
#include <functional>
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

// Why a statement is listed under Outline::notes.  to_string() gives the
// `kind` of the outline JSON; docs/outline-format.md lists them all, and a
// test checks that it does.
enum class Note_kind {
  syntax_error,          // not applied: the statement has a syntax error
  unsupported_sql_mode,  // not applied: an sql_mode the parser does not support
  unknown_table,         // ALTER TABLE, CREATE INDEX (not applied) or CREATE TABLE ... LIKE
                         // (applied) of a table the file does not create
  include_unreadable,    // not applied: a SOURCE file that cannot be read
  include_nested,        // not applied: a SOURCE directive in an included text
  rest_none_in_use,      // not applied: a REST statement that names no service or schema
                         // when none is in use, where MariaDB Shell fails
  unknown_rest_service,  // not applied: ALTER REST SERVICE of a service the file does not create
  unknown_rest_schema,   // not applied: ALTER REST SCHEMA of a schema the file does not name
};
constexpr Note_kind k_note_kinds[] = {Note_kind::syntax_error, Note_kind::unsupported_sql_mode,
                                      Note_kind::unknown_table, Note_kind::include_unreadable,
                                      Note_kind::include_nested, Note_kind::rest_none_in_use,
                                      Note_kind::unknown_rest_service, Note_kind::unknown_rest_schema};
const char *to_string(Note_kind kind);

// A remark on one statement: what the fold could not read, or applied with
// a reservation.
struct Note {
  uint32_t at = 0;       // the statement's offset in the text of `source`
  Note_kind kind = Note_kind::syntax_error;
  bool applied = false;  // the statement changed the outline nonetheless
  std::string message;
  uint32_t source = 0;   // as Origin::source
};

struct Column {
  std::string name;
  // as written, blanks collapsed, without CHARACTER SET and COLLATE:
  // "varchar(20)", "INT UNSIGNED"
  std::string type;
  bool nullable = true;
  std::optional<std::string> default_value;  // the expression as written
  bool auto_increment = false;
  std::optional<std::string> comment;
  std::optional<std::string> collation;
  std::optional<std::string> generated;      // the generation expression
  Origin origin;
  // Where `default_value` is written when not in the definition: an ALTER
  // TABLE ... ALTER COLUMN c SET DEFAULT (`statement` is the expression).
  std::optional<Origin> default_origin;
  std::optional<std::string> charset;        // lower-cased
  std::optional<std::string> on_update;      // ON UPDATE CURRENT_TIMESTAMP(6), as written
  bool invisible = false;
  std::optional<std::string> generated_storage;  // VIRTUAL, PERSISTENT, STORED
  std::optional<std::string> generated_row;  // START, END (AS ROW START / END)
  std::optional<bool> system_versioning;     // WITH / WITHOUT SYSTEM VERSIONING
  // Engine-defined attributes (ENCRYPTED=YES): lower-cased name, value as written.
  std::vector<std::pair<std::string, std::string>> options;

  // "varchar(20) CHARACTER SET utf8mb4 NOT NULL DEFAULT 'x' AUTO_INCREMENT"
  std::string attributes() const;
  // The whole definition after the name: attributes() plus ON UPDATE,
  // AS (expr) VIRTUAL|PERSISTENT|STORED, AS ROW START|END and INVISIBLE.
  std::string definition() const;
};

struct Key_part {
  std::string column;
  std::optional<uint32_t> length;      // a prefix length: a(10)
  std::string order;                   // ASC, DESC, or "" when not written
  std::optional<std::string> expression;  // never set: MariaDB has no expression key parts
};

struct Index {
  std::string name;                    // "" when unnamed
  std::string kind;                    // primary, unique, index, fulltext, spatial, vector
  Origin origin;
  std::vector<Key_part> key_parts;
  std::string using_;                  // BTREE, HASH, RTREE, or ""
  std::optional<std::string> comment;
  bool ignored = false;
  std::string without_overlaps;        // the period of WITHOUT OVERLAPS, or ""
  // key_block_size, parser, engine-defined options: value as written.
  std::vector<std::pair<std::string, std::string>> options;

  // The key parts' columns.
  std::vector<std::string> columns() const;
  // An index on one whole column, as a column's PRIMARY KEY or UNIQUE makes.
  static Index on(std::string kind, const std::string &column, Origin origin);
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
  std::string index_name;              // the name after FOREIGN KEY, or ""
  std::string match;                   // FULL, PARTIAL, SIMPLE, or ""
};

struct Check {
  std::string name;
  std::string expression;
  Origin origin;
  std::string column;                  // the column of a column-level CHECK, else ""
};

// PERIOD FOR SYSTEM_TIME (s, e) has the name SYSTEM_TIME.
struct Period {
  std::string name;
  std::string start;
  std::string end;
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
  // Only the name is known, and foreign keys of other tables that point
  // here: a live catalog read through MCP reads the rest when a request
  // first needs it (see Live_catalog::details).
  bool stub = false;
  std::string partitioning;            // the PARTITION BY clause as written, or ""
  bool system_versioning = false;      // WITH SYSTEM VERSIONING
  std::vector<Period> periods;

  Column *column(std::string_view name);
  const Column *column(std::string_view name) const;
};

// An object a view's query reads: a table or view (FROM, JOIN, subqueries;
// not a common table expression in scope, not an alias), a stored function
// (a call that is qualified or not a built-in's name), or a sequence
// (NEXTVAL, NEXT VALUE FOR, LASTVAL, PREVIOUS VALUE FOR, SETVAL).
struct View_read {
  std::string kind;                    // table_or_view, function, sequence
  std::string schema;                  // the qualifier as written, "" when none
  std::string name;                    // unquoted, case as written
};

struct View {
  std::string name;
  std::vector<std::string> columns;    // the explicit column list, if any
  std::string definition;              // the SELECT as written
  Origin origin;
  bool stub = false;                   // as Table::stub: no columns yet
  std::string algorithm;               // UNDEFINED, MERGE, TEMPTABLE, or ""
  std::string security;                // DEFINER, INVOKER, or ""
  std::string definer;                 // user@host, CURRENT_USER, or ""
  std::string check_option;            // CASCADED, LOCAL, or ""
  // Unique by (kind, schema, name), in text order; nullopt when the query
  // was not read (a live catalog's view).
  std::optional<std::vector<View_read>> reads;
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
  bool stub = false;                   // as Table::stub: no parameters yet
  std::optional<std::string> body;     // as written, not collapsed
  std::optional<bool> deterministic;
  std::string data_access;             // CONTAINS SQL, NO SQL, READS SQL DATA, MODIFIES SQL DATA
  std::string security;                // DEFINER, INVOKER, or ""
  std::string definer;
  std::optional<std::string> comment;
  bool aggregate = false;              // CREATE AGGREGATE FUNCTION
  std::optional<std::string> soname;   // a UDF's library

  // "IN a INT, b TEXT"
  std::string parameter_list() const;
};

struct Trigger {
  std::string name;
  std::string timing;                  // BEFORE, AFTER
  std::string events;                  // INSERT, UPDATE OR DELETE, ...
  std::string table;
  Origin origin;
  std::string body;                    // as written, not collapsed
  std::string order_kind;              // FOLLOWS, PRECEDES, or ""
  std::string order_trigger;
  std::string definer;
  std::vector<std::string> update_columns;  // UPDATE OF a, b
};

struct Named_object {
  std::string name;
  Origin origin;
};

struct Event {
  std::string name;
  Origin origin;
  std::string schedule;                // AT ... / EVERY ... [STARTS ...] [ENDS ...]
  std::optional<bool> on_completion_preserve;
  std::string status;                  // ENABLE, DISABLE, DISABLE ON SLAVE, or ""
  std::optional<std::string> comment;
  std::string body;                    // as written, not collapsed
  std::string definer;
};

struct Sequence {
  std::string name;
  Origin origin;
  // As written; "" when not written.  NO MINVALUE / NOMAXVALUE leave theirs
  // empty, NOCACHE gives cache "0".
  std::string type;                    // AS BIGINT UNSIGNED
  std::string start;
  std::string increment;
  std::string minvalue;
  std::string maxvalue;
  std::string cache;
  std::optional<bool> cycle;
  std::vector<std::pair<std::string, std::string>> options;  // as Table::options
};

// A table, view or routine of a live catalog whose details a request wants.
struct Object_key {
  std::string schema;
  std::string name;
  std::string type;  // table, view, procedure, function

  auto operator<=>(const Object_key &) const = default;
};

struct Schema {
  std::string name;                    // "" for objects whose schema is not known
  bool declared = false;               // created by CREATE DATABASE in the file
  std::vector<std::pair<std::string, std::string>> options;  // charset, collation, comment
  std::vector<Table> tables;
  std::vector<View> views;
  std::vector<Routine> routines;
  std::vector<Trigger> triggers;
  std::vector<Event> events;
  std::vector<Sequence> sequences;
  Origin origin;

  Table *table(std::string_view name);
  const Table *table(std::string_view name) const;
  const View *view(std::string_view name) const;
  const Trigger *trigger(std::string_view name) const;
  const Event *event(std::string_view name) const;
  const Sequence *sequence(std::string_view name) const;
  // `kind`: "procedure" or "function"; "" for either (the first of the name).
  const Routine *routine(std::string_view name, std::string_view kind = {}) const;
  bool empty() const;
};

struct Rest_field;

// `key: [schema.]table { ... }` in a REST object's data mapping.
struct Rest_reference {
  std::optional<std::string> schema;   // when qualified
  std::string table;
  std::vector<Rest_field> fields;
};

// A `key: value @OPTION ... { ... }` pair of a REST object's data mapping.
struct Rest_field {
  std::string name;                    // the JSON key, unquoted
  std::optional<std::string> column;   // the column or parameter; nullopt for a reference
  std::optional<Rest_reference> reference;
  std::vector<std::string> options;    // @OPTIONs, upper-case, without @, but DATATYPE
  std::optional<std::string> datatype; // @DATATYPE("...")'s text
  std::optional<std::string> json_schema;  // JSON SCHEMA's value, as JSON text
};

// A routine's PARAMETERS or one of its RESULTs.
struct Rest_field_set {
  std::optional<std::string> name;     // the class name written after the keyword
  std::vector<Rest_field> fields;
};

// The settings a REST statement writes (outline features
// rest_object_details, rest_settings); null where not written.  A service has
// enabled, published, protocol, comment, the auth_* paths, options and
// metadata; a schema enabled, requires_auth, items_per_page, comment,
// options and metadata; an object all but published, protocol and auth_*.
// An option written twice keeps the last.
struct Rest_settings {
  std::optional<std::string> enabled;               // ENABLED, DISABLED, PRIVATE
  std::optional<bool> published;                    // PUBLISHED, UNPUBLISHED
  std::optional<std::string> protocol;              // HTTP, HTTPS
  std::optional<bool> requires_auth;                // AUTHENTICATION [NOT] REQUIRED
  std::optional<uint64_t> items_per_page;
  std::optional<std::string> comment;
  std::optional<std::string> auth_path;             // AUTHENTICATION PATH ...; DEFAULT is null
  std::optional<std::string> auth_redirection;
  std::optional<std::string> auth_validation;
  std::optional<std::string> auth_page_content;
  std::optional<std::string> media_type;
  bool media_type_autodetect = false;
  std::optional<std::string> format;                // FEED, ITEM, MEDIA
  std::optional<std::string> auth_procedure;        // unquoted, schema.name
  std::optional<std::string> options;               // OPTIONS, as JSON text
  // `options` is a JSON merge patch to options the file does not know: an
  // ALTER ... MERGE OPTIONS of a service or schema the file only names.  A
  // CREATE stores MERGE OPTIONS as they are, as MariaDB Shell does.
  bool merge_options = false;
  std::optional<std::string> metadata;              // METADATA, as JSON text
};

struct Rest_object {
  std::string kind;                    // view, procedure, function
  std::string path;
  std::string object;                  // the database object, as written
  Origin origin;
  // The database object, unquoted; the schema is empty when unqualified.
  std::string object_schema;
  std::string object_name;
  // The statement's details (outline feature rest_object_details); an option
  // written twice keeps the last.
  std::optional<std::string> class_name;            // a view's CLASS
  std::vector<std::string> crud;                    // a view's @OPTIONs after CLASS
  std::optional<std::vector<Rest_field>> fields;    // a view's { ... }
  std::optional<Rest_field_set> parameters;         // a routine's PARAMETERS
  std::vector<Rest_field_set> results;              // a routine's RESULTs
  Rest_settings settings;
  bool force = false;                               // a routine's FORCE
};

struct Rest_schema {
  std::string path;
  std::string schema;                  // the database schema
  std::vector<Rest_object> objects;
  bool declared = false;               // created by CREATE REST SCHEMA in the file
  Origin origin;
  Rest_settings settings;
};

// A REST service's name, `[developer, ...@]/path`.  Services are told apart
// by request path and developers, as MariaDB Shell does (find_service).
struct Rest_service_name {
  std::string path;                    // as written, blanks collapsed
  std::string request_path;            // unquoted
  std::vector<std::string> developers; // unquoted, sorted as the shell does
  // developers,...@request_path, a developer other than [A-Za-z0-9_]+ in
  // single quotes: equal (without regard to case) for the same service; ""
  // for no name.
  std::string key;
};

struct Rest_service {
  Rest_service_name name;              // `path` as the statement that made it wrote it
  Rest_settings settings;
  std::vector<Rest_schema> schemas;
  std::vector<Named_object> content_sets;
  std::vector<Named_object> auth_apps;  // the linked auth apps (ADD AUTH APP)
  bool declared = false;               // created by CREATE REST SERVICE in the file
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
  // The schema in use before the first statement.
  void set_current_schema(std::string schema) { m_current = std::move(schema); }
  // Calls `f` on every origin in the catalog, for moving the offsets of an
  // expanded text back to their files.
  void for_each_origin(const std::function<void(Origin &)> &f);

  // A catalog made of `schemas` as they are, for one read from a server.
  static Catalog from_schemas(std::vector<Schema> schemas);

  // Things the fold could not apply, such as ALTER TABLE of a table the
  // script never created.
  const std::vector<Note> &notes() const { return m_notes; }

 private:
  Schema &schema(const std::string &name);
  Schema *find_schema(std::string_view name);
  Rest_service &service(const Rest_service_name &name);
  Rest_service *find_service(std::string_view key);

  std::vector<Schema> m_schemas;
  std::string m_current;
  std::vector<Rest_service> m_services;
  // What USE REST SERVICE / SCHEMA set, as MariaDB Shell keeps it: the
  // current service, and the request path of its current schema.
  Rest_service_name m_current_service;
  std::string m_current_rest_schema;
  std::vector<Named_object> m_auth_apps;
  std::vector<Named_object> m_roles;
  std::vector<Named_object> m_users;
  std::vector<Note> m_notes;
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
