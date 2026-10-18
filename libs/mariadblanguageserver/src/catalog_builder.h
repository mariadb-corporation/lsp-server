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

#ifndef MDBLS_CATALOG_BUILDER_H_
#define MDBLS_CATALOG_BUILDER_H_

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mdbls/catalog.h"

namespace mdbls {

// The schemas of a server's catalog being built, with name lookups; shared
// by the direct reader (live_catalog.cc) and the MCP one (mcp_catalog.cc).
// A Schema& stays valid until another schema is added.
struct Catalog_builder {
  std::vector<Schema> schemas;
  std::unordered_map<std::string, size_t> schema_index;
  std::map<std::pair<std::string, std::string>, std::pair<size_t, size_t>> table_index;
  std::map<std::pair<std::string, std::string>, std::pair<size_t, size_t>> view_index;

  Schema &schema(const std::string &name) {
    const auto it = schema_index.find(name);
    if (it != schema_index.end()) return schemas[it->second];
    schema_index.emplace(name, schemas.size());
    schemas.push_back(Schema{});
    schemas.back().name = name;
    schemas.back().declared = true;
    return schemas.back();
  }
  Table *table(const std::string &s, const std::string &t) {
    const auto it = table_index.find({s, t});
    return it == table_index.end() ? nullptr : &schemas[it->second.first].tables[it->second.second];
  }
  View *view(const std::string &s, const std::string &v) {
    const auto it = view_index.find({s, v});
    return it == view_index.end() ? nullptr : &schemas[it->second.first].views[it->second.second];
  }
};

}  // namespace mdbls

#endif  // MDBLS_CATALOG_BUILDER_H_
