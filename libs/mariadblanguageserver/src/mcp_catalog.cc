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

#include "mdbls/mcp_catalog.h"

#include <chrono>
#include <utility>
#include <vector>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "catalog_builder.h"
#include "mdbls/lsp/server.h"
#include "text_util.h"

namespace mdbls {

namespace {

using Json = rapidjson::Value;

// The member, or nullptr when missing or null.
const Json *member(const Json &o, const char *key) {
  if (!o.IsObject()) return nullptr;
  const auto it = o.FindMember(key);
  return it == o.MemberEnd() || it->value.IsNull() ? nullptr : &it->value;
}

// A member as text: strings as they are, numbers and booleans spelled out,
// "" when missing.  The tools report flags as 0/1 and some values as
// either, depending on the server.
std::string text(const Json &o, const char *key) {
  const Json *v = member(o, key);
  if (v == nullptr) return {};
  if (v->IsString()) return std::string(v->GetString(), v->GetStringLength());
  if (v->IsBool()) return v->GetBool() ? "1" : "0";
  if (v->IsInt64()) return std::to_string(v->GetInt64());
  if (v->IsUint64()) return std::to_string(v->GetUint64());
  if (v->IsDouble()) return std::to_string(v->GetDouble());
  return {};
}

std::optional<std::string> optional_text(const Json &o, const char *key) {
  return member(o, key) == nullptr ? std::nullopt : std::optional<std::string>(text(o, key));
}

// A non-empty member as text.
std::optional<std::string> nonempty(const Json &o, const char *key) {
  auto t = optional_text(o, key);
  return t && !t->empty() ? t : std::nullopt;
}

bool flag(const Json &o, const char *key) {
  const Json *v = member(o, key);
  if (v == nullptr) return false;
  if (v->IsBool()) return v->GetBool();
  if (v->IsNumber()) return v->GetDouble() != 0;
  if (v->IsString()) {
    const std::string_view s(v->GetString(), v->GetStringLength());
    return s == "1" || iequals(s, "true");
  }
  return false;
}

// A JSON object of string arguments.
std::string arguments(std::initializer_list<std::pair<const char *, std::string_view>> args) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.StartObject();
  for (const auto &[key, value] : args) {
    w.Key(key);
    w.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
  }
  w.EndObject();
  return std::string(buf.GetString(), buf.GetSize());
}

const char *const k_server_sql =
    "SELECT @@sql_mode AS sql_mode, VERSION() AS version, DATABASE() AS schema_name";

// The first row of db.execute_sql's result: result_sets[0].rows[0], or
// rows[0] as older shells report it.
const Json *first_row(const Json &result) {
  const Json *sets = member(result, "result_sets");
  const Json *rows = sets != nullptr && sets->IsArray() && !sets->Empty()
                         ? member((*sets)[0], "rows")
                         : member(result, "rows");
  return rows != nullptr && rows->IsArray() && !rows->Empty() ? &(*rows)[0] : nullptr;
}

// The tool calls of one reading, and the first failure.
struct Reading {
  const Tool_caller &call;
  const Mcp_catalog_options &options;
  std::string error;
  bool permanent = false;

  // Calls `tool`; false, with `error` set, when it failed or the reading
  // was cancelled.
  bool run(std::string_view tool, const std::string &args, rapidjson::Document *out) {
    if (options.cancelled && options.cancelled()) {
      error = "cancelled";
      return false;
    }
    if (const auto f = call(tool, args, out)) {
      error = std::string(tool) + ": " + f->message;
      permanent = f->permanent;
      return false;
    }
    return true;
  }

  bool list(const std::string &schema, const char *type, rapidjson::Document *out) {
    if (!run("db.list_objects", arguments({{"schema_name", schema}, {"object_type", type}}), out)) {
      return false;
    }
    if (!out->IsArray()) {
      error = "db.list_objects: not a list";
      return false;
    }
    return true;
  }

  bool details(const std::string &schema, const std::string &name, const char *type,
               rapidjson::Document *out) {
    return run("db.get_object_details",
               arguments({{"schema_name", schema}, {"object_name", name}, {"object_type", type}}),
               out);
  }
};

void read_columns(const Json &details, Table *table, View *view) {
  const Json *columns = member(details, "columns");
  if (columns == nullptr || !columns->IsArray()) return;
  for (const auto &c : columns->GetArray()) {
    if (view != nullptr) {
      view->columns.push_back(text(c, "name"));
      continue;
    }
    Column col;
    col.name = text(c, "name");
    col.type = collapse(text(c, "datatype"));
    col.nullable = !flag(c, "not_null");
    col.default_value = optional_text(c, "column_default");
    col.auto_increment = text(c, "id_generation") == "auto_inc";
    col.comment = nonempty(c, "comment");
    col.collation = nonempty(c, "collation");
    table->columns.push_back(std::move(col));
  }
}

// The primary key and the unique keys: one row per constrained column,
// in constraint order.  Foreign keys come from the references.
void read_constraints(const Json &details, Table &t) {
  const Json *rows = member(details, "constraints");
  if (rows == nullptr || !rows->IsArray()) return;
  for (const auto &r : rows->GetArray()) {
    const std::string type = upper(text(r, "constraint_type"));
    const std::string kind = type == "PRIMARY KEY" ? "primary" : type == "UNIQUE" ? "unique" : "";
    if (kind.empty()) continue;
    const std::string name = text(r, "constraint_name");
    if (t.indexes.empty() || t.indexes.back().name != name || t.indexes.back().kind != kind) {
      Index ix;
      ix.name = name;
      ix.kind = kind;
      t.indexes.push_back(std::move(ix));
    }
    if (const auto column = nonempty(r, "column_name")) t.indexes.back().columns.push_back(*column);
  }
}

// The foreign keys of the table itself (the n:1 references) into `t`, and
// those of other tables that point at it (1:1 and 1:n) into `incoming`.
void read_references(const Json &details, const std::string &schema, Table &t,
                     Catalog_details *incoming) {
  const Json *rows = member(details, "references");
  if (rows == nullptr || !rows->IsArray()) return;
  for (const auto &r : rows->GetArray()) {
    const Json *mapping = member(r, "reference_mapping");
    rapidjson::Document parsed;
    if (mapping != nullptr && mapping->IsString()) {
      // Not expanded by the tool: as the server assembled it.
      parsed.Parse(mapping->GetString(), mapping->GetStringLength());
      if (parsed.HasParseError()) continue;
      mapping = &parsed;
    }
    if (mapping == nullptr) continue;
    const std::string kind = text(*mapping, "kind");
    Foreign_key fk;
    const std::string constraint = text(*mapping, "constraint");
    fk.name = constraint.substr(constraint.find('.') == std::string::npos ? 0 : constraint.find('.') + 1);
    const Json *pairs = member(*mapping, "column_mapping");
    if (kind == "n:1") {
      fk.ref_schema = text(*mapping, "referenced_schema");
      fk.ref_table = text(*mapping, "referenced_table");
      if (pairs != nullptr && pairs->IsArray()) {
        for (const auto &pair : pairs->GetArray()) {
          fk.columns.push_back(text(pair, "base"));
          fk.ref_columns.push_back(text(pair, "ref"));
        }
      }
      t.foreign_keys.push_back(std::move(fk));
    } else if (incoming != nullptr) {
      // "referenced" is the other table here, and "base" this one's column.
      fk.ref_schema = schema;
      fk.ref_table = t.name;
      if (pairs != nullptr && pairs->IsArray()) {
        for (const auto &pair : pairs->GetArray()) {
          fk.columns.push_back(text(pair, "ref"));
          fk.ref_columns.push_back(text(pair, "base"));
        }
      }
      incoming->incoming.emplace_back(text(*mapping, "referenced_schema"),
                                      text(*mapping, "referenced_table"), std::move(fk));
    }
  }
}

void read_routine(const Json &details, Routine &rt) {
  if (const Json *params = member(details, "parameters"); params != nullptr && params->IsArray()) {
    for (const auto &p : params->GetArray()) {
      Parameter prm;
      prm.mode = upper(text(p, "mode"));
      prm.name = text(p, "name");
      prm.type = collapse(text(p, "datatype"));
      rt.parameters.push_back(std::move(prm));
    }
  }
  rt.returns = collapse(text(details, "returns"));
}

}  // namespace

std::optional<Live_snapshot> fetch_mcp_catalog(const Tool_caller &call, std::string *error,
                                               bool *permanent,
                                               const Mcp_catalog_options &options) {
  Reading reading{call, options, {}, false};
  auto fail = [&]() -> std::optional<Live_snapshot> {
    if (error != nullptr) *error = reading.error;
    if (permanent != nullptr) *permanent = reading.permanent;
    return std::nullopt;
  };
  rapidjson::Document d;
  Live_snapshot snap;
  if (!reading.run("db.execute_sql", arguments({{"sql", k_server_sql}}), &d)) return fail();
  if (const Json *row = first_row(d)) {
    snap.server.sql_mode = text(*row, "sql_mode");
    snap.server.version = text(*row, "version");
    snap.server.current_schema = text(*row, "schema_name");
  }
  snap.server.version_id = lsp::parse_server_version(snap.server.version);

  if (!reading.run("db.list_schemas", "{}", &d)) return fail();
  if (!d.IsArray()) {
    reading.error = "db.list_schemas: not a list";
    return fail();
  }
  Catalog_builder b;
  struct Listed {
    std::string name;
    bool user;
  };
  std::vector<Listed> schemas;
  for (const auto &s : d.GetArray()) {
    const std::string name = text(s, "schema_name");
    if (name.empty() || name == "performance_schema") continue;
    Schema &sc = b.schema(name);
    if (const auto comment = nonempty(s, "schema_comment")) sc.options.emplace_back("comment", *comment);
    schemas.push_back({name, text(s, "schema_type") == "User Schema"});
  }

  for (const auto &listed : schemas) {
    Schema &sc = b.schema(listed.name);
    const size_t si = b.schema_index[listed.name];
    if (!reading.list(listed.name, "table", &d)) return fail();
    for (const auto &o : d.GetArray()) {
      Table t;
      t.name = text(o, "name");
      if (const auto comment = nonempty(o, "comment")) t.options.emplace_back("comment", *comment);
      b.table_index[{listed.name, t.name}] = {si, sc.tables.size()};
      sc.tables.push_back(std::move(t));
    }
    if (!reading.list(listed.name, "view", &d)) return fail();
    for (const auto &o : d.GetArray()) {
      View v;
      v.name = text(o, "name");
      b.view_index[{listed.name, v.name}] = {si, sc.views.size()};
      sc.views.push_back(std::move(v));
    }
    if (!listed.user) continue;

    // Names only: the details are read when a request needs them.
    for (Table &t : sc.tables) t.stub = true;
    for (View &v : sc.views) v.stub = true;
    for (const char *kind : {"procedure", "function"}) {
      if (!reading.list(listed.name, kind, &d)) return fail();
      for (const auto &o : d.GetArray()) {
        Routine rt;
        rt.name = text(o, "name");
        rt.kind = kind;
        rt.stub = true;
        sc.routines.push_back(std::move(rt));
      }
    }
    if (!reading.list(listed.name, "trigger", &d)) return fail();
    for (const auto &o : d.GetArray()) {
      Trigger tr;
      tr.name = text(o, "name");
      sc.triggers.push_back(std::move(tr));
    }
    if (!reading.list(listed.name, "event", &d)) return fail();
    for (const auto &o : d.GetArray()) sc.events.push_back({text(o, "name"), {}});
    if (!reading.list(listed.name, "sequence", &d)) return fail();
    for (const auto &o : d.GetArray()) sc.sequences.push_back({text(o, "name"), {}});
  }

  for (const auto &sc : b.schemas) {
    snap.tables += sc.tables.size();
    for (const auto &t : sc.tables) snap.columns += t.columns.size();
  }
  snap.catalog = std::make_shared<const Catalog>(Catalog::from_schemas(std::move(b.schemas)));
  snap.fetched = std::chrono::system_clock::now();
  return snap;
}

std::optional<Catalog_details> fetch_mcp_details(const Tool_caller &call,
                                                const std::vector<Object_key> &objects,
                                                std::string *error, bool *permanent,
                                                const Mcp_catalog_options &options) {
  Reading reading{call, options, {}, false};
  Catalog_details out;
  rapidjson::Document d;
  for (const auto &o : objects) {
    if (!reading.details(o.schema, o.name, o.type.c_str(), &d)) {
      if (error != nullptr) *error = reading.error;
      if (permanent != nullptr) *permanent = reading.permanent;
      return std::nullopt;
    }
    if (o.type == "table") {
      Table t;
      t.name = o.name;
      if (const Json *basic = member(d, "basic")) {
        if (const auto comment = nonempty(*basic, "comment")) t.options.emplace_back("comment", *comment);
      }
      read_columns(d, &t, nullptr);
      read_constraints(d, t);
      read_references(d, o.schema, t, &out);
      out.tables.emplace_back(o.schema, std::move(t));
    } else if (o.type == "view") {
      View v;
      v.name = o.name;
      read_columns(d, nullptr, &v);
      out.views.emplace_back(o.schema, std::move(v));
    } else {
      Routine rt;
      rt.name = o.name;
      rt.kind = o.type;
      read_routine(d, rt);
      out.routines.emplace_back(o.schema, std::move(rt));
    }
  }
  return out;
}

}  // namespace mdbls
