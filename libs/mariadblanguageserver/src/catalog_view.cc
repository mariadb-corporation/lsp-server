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

#include "mdbls/catalog_view.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "text_util.h"

namespace mdbls {

namespace {

// Case-insensitive order, without lower-casing copies.
struct Iless {
  bool operator()(const std::string &a, const std::string &b) const {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  }
};

}  // namespace

std::string Catalog_view::current_schema() const {
  if (!m_current.empty()) return m_current;
  for (const auto &l : m_layers) {
    if (!l.default_schema.empty()) return l.default_schema;
  }
  return {};
}

std::vector<const Schema *> Catalog_view::schemas_for(const Catalog_layer &layer,
                                                      std::string_view schema) const {
  std::vector<const Schema *> out;
  if (!layer.catalog) return out;
  const std::string current = current_schema();
  const std::string_view want = schema.empty() ? std::string_view(current) : schema;
  if (!want.empty()) {
    if (const Schema *s = layer.catalog->find(want)) out.push_back(s);
  }
  // Objects created before any USE belong to whatever schema is current.
  if (want.empty() || iequals(want, current)) {
    if (const Schema *s = layer.catalog->find("")) {
      if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
  }
  // A live layer with no schema known: its default schema.
  if (want.empty() && !layer.default_schema.empty()) {
    if (const Schema *s = layer.catalog->find(layer.default_schema)) {
      if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
  }
  return out;
}

Catalog_view::Table_hit Catalog_view::find_table(std::string_view schema,
                                                 std::string_view name) const {
  const Table_hit hit = find_table_in_layers(schema, name);
  const std::string schema_name = hit.schema != nullptr ? hit.schema->name : std::string();
  if (hit.table != nullptr && hit.table->stub) m_stubs.insert({schema_name, hit.table->name, "table"});
  if (hit.view != nullptr && hit.view->stub) m_stubs.insert({schema_name, hit.view->name, "view"});
  return hit;
}

Catalog_view::Table_hit Catalog_view::find_table_in_layers(std::string_view schema,
                                                           std::string_view name) const {
  for (const auto &l : m_layers) {
    for (const Schema *s : schemas_for(l, schema)) {
      if (const Table *t = s->table(name)) return {t, nullptr, s, &l};
      if (const View *v = s->view(name)) return {nullptr, v, s, &l};
    }
  }
  // Unqualified and no schema known: any schema, if only one has it.
  if (schema.empty() && current_schema().empty()) {
    Table_hit hit;
    int found = 0;
    for (const auto &l : m_layers) {
      if (!l.catalog) continue;
      for (const auto &s : l.catalog->schemas()) {
        if (const Table *t = s.table(name)) {
          if (found++ == 0) hit = {t, nullptr, &s, &l};
        } else if (const View *v = s.view(name)) {
          if (found++ == 0) hit = {nullptr, v, &s, &l};
        }
      }
      if (found > 0) break;
    }
    if (found == 1) return hit;
  }
  return {};
}

Catalog_view::Routine_hit Catalog_view::find_routine(std::string_view schema,
                                                     std::string_view name) const {
  for (const auto &l : m_layers) {
    for (const Schema *s : schemas_for(l, schema)) {
      if (const Routine *r = s->routine(name)) {
        if (r->stub) m_stubs.insert({s->name, r->name, r->kind});
        return {r, s, &l};
      }
    }
  }
  return {};
}

std::vector<std::string> Catalog_view::schema_names() const {
  std::vector<std::string> out;
  std::set<std::string, Iless> seen;
  for (const auto &l : m_layers) {
    if (!l.catalog) continue;
    for (const auto &s : l.catalog->schemas()) {
      if (s.name.empty()) continue;
      if (seen.insert(s.name).second) out.push_back(s.name);
    }
  }
  return out;
}

bool Catalog_view::has_schema(std::string_view name) const {
  for (const auto &l : m_layers) {
    if (l.catalog && l.catalog->find(name) != nullptr) return true;
  }
  return false;
}

void Catalog_view::for_each_table(std::string_view schema,
                                  const std::function<void(const Table_hit &)> &f) const {
  std::set<std::string, Iless> seen;
  for (const auto &l : m_layers) {
    for (const Schema *s : schemas_for(l, schema)) {
      for (const auto &t : s->tables) {
        if (seen.insert(t.name).second) f({&t, nullptr, s, &l});
      }
      for (const auto &v : s->views) {
        if (seen.insert(v.name).second) f({nullptr, &v, s, &l});
      }
    }
  }
}

void Catalog_view::for_each_routine(std::string_view schema,
                                    const std::function<void(const Routine_hit &)> &f) const {
  std::set<std::string, Iless> seen;
  for (const auto &l : m_layers) {
    for (const Schema *s : schemas_for(l, schema)) {
      for (const auto &r : s->routines) {
        if (seen.insert(r.name).second) f({&r, s, &l});
      }
    }
  }
}

}  // namespace mdbls
