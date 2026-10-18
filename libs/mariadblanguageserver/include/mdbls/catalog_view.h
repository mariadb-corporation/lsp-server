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

#ifndef MDBLS_CATALOG_VIEW_H_
#define MDBLS_CATALOG_VIEW_H_

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog.h"

namespace mdbls {

// Where a catalog came from.  Earlier layers override later ones.
enum class Layer_kind : uint8_t {
  document,   // the DDL of the document itself
  workspace,  // the workspace's catalog files
  live,       // a snapshot read from a server
};

struct Catalog_layer {
  std::shared_ptr<const Catalog> catalog;
  Layer_kind kind = Layer_kind::document;
  // The schema unqualified names belong to in this layer when nothing
  // else says: a live connection's default schema.
  std::string default_schema;
};

// Several catalogs looked at as one, in order: the open document's own DDL,
// then the workspace's files, then a live connection.  A name found in an
// earlier layer hides the same name in a later one.
//
// Unqualified names belong to the current schema (the last USE before the
// statement, else what the client or the connection says).  Objects a
// document creates before any USE are kept under the schema "" by the fold;
// they count as the current schema's.
class Catalog_view {
 public:
  void add(Catalog_layer layer) { m_layers.push_back(std::move(layer)); }
  const std::vector<Catalog_layer> &layers() const { return m_layers; }
  bool empty() const { return m_layers.empty(); }

  void set_current_schema(std::string schema) { m_current = std::move(schema); }
  // The current schema, or the first layer's default schema when none.
  std::string current_schema() const;

  struct Table_hit {
    const Table *table = nullptr;  // one of table and view is set
    const View *view = nullptr;
    const Schema *schema = nullptr;
    const Catalog_layer *layer = nullptr;
  };
  // The table or view `name` of `schema` ("" for the current schema).
  Table_hit find_table(std::string_view schema, std::string_view name) const;

  struct Routine_hit {
    const Routine *routine = nullptr;
    const Schema *schema = nullptr;
    const Catalog_layer *layer = nullptr;
  };
  Routine_hit find_routine(std::string_view schema, std::string_view name) const;

  // Schema names, each once, in layer order.
  std::vector<std::string> schema_names() const;
  bool has_schema(std::string_view name) const;

  // Each table and view of `schema` ("" for the current one) once, the
  // earliest layer's.
  void for_each_table(std::string_view schema,
                      const std::function<void(const Table_hit &)> &f) const;
  // Each routine of `schema` once.
  void for_each_routine(std::string_view schema,
                        const std::function<void(const Routine_hit &)> &f) const;

  // The stubs (see Table::stub) find_table and find_routine have returned:
  // what a request would have used had their details been read.
  const std::set<Object_key> &stubs_met() const { return m_stubs; }

 private:
  Table_hit find_table_in_layers(std::string_view schema, std::string_view name) const;

  // The schemas of `layer` that `schema` ("" = current) means.
  std::vector<const Schema *> schemas_for(const Catalog_layer &layer,
                                          std::string_view schema) const;

  std::vector<Catalog_layer> m_layers;
  std::string m_current;
  mutable std::set<Object_key> m_stubs;
};

}  // namespace mdbls

#endif  // MDBLS_CATALOG_VIEW_H_
