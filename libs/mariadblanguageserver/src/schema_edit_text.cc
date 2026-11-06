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

#include "schema_edit_text.h"

#include <algorithm>
#include <cctype>
#include <iterator>

#include "mdbls/keywords.h"
#include "text_util.h"

namespace mdbls::edit_text {

std::string Style::id(std::string_view name) const {
  return backticks ? backtick_quote(name) : quote_identifier(name);
}

std::string Style::kw(std::string_view keyword) const { return upper ? std::string(keyword) : lower(keyword); }

bool known_type(std::string_view type) {
  static const char *const k_types[] = {
      "TINYINT", "SMALLINT", "MEDIUMINT", "MIDDLEINT", "INT", "INTEGER", "BIGINT", "INT1", "INT2", "INT3",
      "INT4", "INT8", "DECIMAL", "DEC", "NUMERIC", "FIXED", "NUMBER", "FLOAT", "FLOAT4", "FLOAT8", "DOUBLE",
      "REAL", "BIT", "BOOL", "BOOLEAN", "SERIAL", "CHAR", "CHARACTER", "NCHAR", "NATIONAL", "VARCHAR",
      "VARCHAR2", "NVARCHAR", "VARCHARACTER", "VARBINARY", "BINARY", "RAW", "TINYTEXT", "TEXT", "MEDIUMTEXT",
      "LONGTEXT", "LONG", "CLOB", "TINYBLOB", "BLOB", "MEDIUMBLOB", "LONGBLOB", "ENUM", "SET", "JSON", "DATE",
      "TIME", "DATETIME", "TIMESTAMP", "YEAR", "GEOMETRY", "POINT", "LINESTRING", "POLYGON", "MULTIPOINT",
      "MULTILINESTRING", "MULTIPOLYGON", "GEOMETRYCOLLECTION", "GEOMCOLLECTION", "INET4", "INET6", "UUID",
      "VECTOR", "XMLTYPE", "SIGNED", "UNSIGNED"};
  const std::string_view t = trim(type);
  size_t n = 0;
  while (n < t.size() && (std::isalnum(static_cast<unsigned char>(t[n])) || t[n] == '_')) ++n;
  const std::string_view word = t.substr(0, n);
  return std::any_of(std::begin(k_types), std::end(k_types), [&](const char *k) { return iequals(word, k); });
}

std::string sql_string(std::string_view value) {
  std::string out = "'";
  for (const char c : value) {
    if (c == '\'') out += "''";
    else if (c == '\\') out += "\\\\";
    else if (c == '\0') out += "\\0";
    else out += c;
  }
  return out + "'";
}

namespace {

std::string ids(const std::vector<std::string> &names, const Style &st) {
  std::string out;
  for (const auto &n : names) {
    if (!out.empty()) out += ", ";
    out += st.id(n);
  }
  return out;
}

// NAME=value for engine-defined and other options given by name.
std::string option_sql(std::string_view name, std::string_view value, const Style &st) {
  return st.kw(upper(name)) + "=" + std::string(value);
}

// An expression in parentheses: kept as written when it has them.
std::string parenthesized(std::string_view e) {
  const std::string_view t = trim(e);
  if (!t.empty() && t.front() == '(' && t.back() == ')') return std::string(t);
  return "(" + std::string(t) + ")";
}

}  // namespace

std::string column_sql(const Column &c, const std::vector<Check> &checks, const Style &st) {
  std::string out = st.id(c.name) + " " + c.type;
  const bool serial = iequals(trim(c.type), "SERIAL");
  if (c.charset) out += " " + st.kw("CHARACTER SET") + " " + *c.charset;
  if (c.collation) out += " " + st.kw("COLLATE") + " " + *c.collation;
  if (c.generated) {
    out += " " + st.kw("AS") + " " + parenthesized(*c.generated);
    if (c.generated_storage) out += " " + st.kw(upper(*c.generated_storage));
  } else if (c.generated_row) {
    out += " " + st.kw("GENERATED ALWAYS AS ROW") + " " + st.kw(upper(*c.generated_row));
  } else {
    if (!c.nullable && !serial) out += " " + st.kw("NOT NULL");
    if (c.default_value) out += " " + st.kw("DEFAULT") + " " + *c.default_value;
    if (c.on_update) out += " " + st.kw("ON UPDATE") + " " + *c.on_update;
    if (c.auto_increment && !serial) out += " " + st.kw("AUTO_INCREMENT");
  }
  if (c.invisible) out += " " + st.kw("INVISIBLE");
  if (c.comment) out += " " + st.kw("COMMENT") + " " + sql_string(*c.comment);
  if (c.system_versioning) {
    out += " " + st.kw(*c.system_versioning ? "WITH SYSTEM VERSIONING" : "WITHOUT SYSTEM VERSIONING");
  }
  for (const auto &[k, v] : c.options) out += " " + option_sql(k, v, st);
  for (const auto &ck : checks) out += " " + st.kw("CHECK") + " " + parenthesized(ck.expression);
  return out;
}

std::string index_sql(const Index &ix, const Style &st) {
  std::string out;
  if (ix.kind == "primary") {
    out = st.kw("PRIMARY KEY");
  } else {
    if (ix.kind != "index") out = st.kw(upper(ix.kind)) + " ";
    out += st.kw("INDEX");
    if (!ix.name.empty()) out += " " + st.id(ix.name);
  }
  std::string parts;
  for (const auto &kp : ix.key_parts) {
    if (!parts.empty()) parts += ", ";
    parts += st.id(kp.column);
    if (kp.length) parts += "(" + std::to_string(*kp.length) + ")";
    if (!kp.order.empty()) parts += " " + st.kw(upper(kp.order));
  }
  if (!ix.without_overlaps.empty()) parts += ", " + st.id(ix.without_overlaps) + " " + st.kw("WITHOUT OVERLAPS");
  out += " (" + parts + ")";
  if (!ix.using_.empty()) out += " " + st.kw("USING") + " " + st.kw(upper(ix.using_));
  for (const auto &[k, v] : ix.options) {
    if (k == "parser") out += " " + st.kw("WITH PARSER") + " " + v;
    else out += " " + option_sql(k, v, st);
  }
  if (ix.comment) out += " " + st.kw("COMMENT") + " " + sql_string(*ix.comment);
  if (ix.ignored) out += " " + st.kw("IGNORED");
  return out;
}

std::string foreign_key_sql(const Foreign_key &fk, const Style &st) {
  std::string out;
  if (!fk.name.empty()) out = st.kw("CONSTRAINT") + " " + st.id(fk.name) + " ";
  out += st.kw("FOREIGN KEY");
  if (!fk.index_name.empty() && fk.index_name != fk.name) out += " " + st.id(fk.index_name);
  out += " (" + ids(fk.columns, st) + ") " + st.kw("REFERENCES") + " ";
  if (!fk.ref_schema.empty()) out += st.id(fk.ref_schema) + ".";
  out += st.id(fk.ref_table) + " (" + ids(fk.ref_columns, st) + ")";
  if (!fk.match.empty()) out += " " + st.kw("MATCH") + " " + st.kw(upper(fk.match));
  if (!fk.on_delete.empty()) out += " " + st.kw("ON DELETE") + " " + st.kw(upper(fk.on_delete));
  if (!fk.on_update.empty()) out += " " + st.kw("ON UPDATE") + " " + st.kw(upper(fk.on_update));
  return out;
}

std::string check_sql(const Check &ck, const Style &st) {
  std::string out;
  if (!ck.name.empty()) out = st.kw("CONSTRAINT") + " " + st.id(ck.name) + " ";
  return out + st.kw("CHECK") + " " + parenthesized(ck.expression);
}

std::string period_sql(const Period &p, const Style &st) {
  const std::string name = iequals(p.name, "SYSTEM_TIME") ? st.kw("SYSTEM_TIME") : st.id(p.name);
  return st.kw("PERIOD FOR") + " " + name + " (" + st.id(p.start) + ", " + st.id(p.end) + ")";
}

std::string table_option_sql(std::string_view name, std::string_view value, const Style &st) {
  if (name == "engine") return st.kw("ENGINE") + "=" + std::string(value);
  if (name == "charset") return st.kw("DEFAULT CHARSET") + "=" + std::string(value);
  if (name == "collation") return st.kw("COLLATE") + "=" + std::string(value);
  if (name == "comment") return st.kw("COMMENT") + "=" + sql_string(value);
  if (name == "data") return st.kw("DATA DIRECTORY") + "=" + std::string(value);
  if (name == "index") return st.kw("INDEX DIRECTORY") + "=" + std::string(value);
  return option_sql(name, value, st);
}

std::string create_table_sql(const Table &t, const Style &st) {
  std::vector<std::string> elements;
  for (const auto &c : t.columns) {
    std::vector<Check> own;
    for (const auto &ck : t.checks) {
      if (!ck.column.empty() && iequals(ck.column, c.name)) own.push_back(ck);
    }
    elements.push_back(column_sql(c, own, st));
  }
  for (const auto &p : t.periods) elements.push_back(period_sql(p, st));
  for (const auto &ix : t.indexes) elements.push_back(index_sql(ix, st));
  for (const auto &fk : t.foreign_keys) elements.push_back(foreign_key_sql(fk, st));
  for (const auto &ck : t.checks) {
    if (ck.column.empty()) elements.push_back(check_sql(ck, st));
  }
  std::string out = st.kw("CREATE") + " " + (t.temporary ? st.kw("TEMPORARY") + " " : "") + st.kw("TABLE") +
                    " " + st.id(t.name) + " (" + join(elements, ", ") + ")";
  for (const auto &[k, v] : t.options) out += " " + table_option_sql(k, v, st);
  if (t.system_versioning) out += " " + st.kw("WITH SYSTEM VERSIONING");
  if (!t.partitioning.empty()) out += " " + t.partitioning;
  return out;
}

std::string create_view_sql(const View &v, const Style &st) {
  std::string out = st.kw("CREATE");
  if (!v.algorithm.empty()) out += " " + st.kw("ALGORITHM") + "=" + st.kw(upper(v.algorithm));
  if (!v.definer.empty()) out += " " + st.kw("DEFINER") + "=" + v.definer;
  if (!v.security.empty()) out += " " + st.kw("SQL SECURITY") + " " + st.kw(upper(v.security));
  out += " " + st.kw("VIEW") + " " + st.id(v.name);
  if (!v.columns.empty()) out += " (" + ids(v.columns, st) + ")";
  out += " " + st.kw("AS") + " " + std::string(trim(v.definition));
  if (!v.check_option.empty()) {
    out += " " + st.kw("WITH") + " " + st.kw(upper(v.check_option)) + " " + st.kw("CHECK OPTION");
  }
  return out;
}

std::string create_routine_sql(const Routine &r, const Style &st) {
  std::string out = st.kw("CREATE");
  if (!r.definer.empty()) out += " " + st.kw("DEFINER") + "=" + r.definer;
  const bool function = r.kind == "function";
  if (function && r.aggregate) out += " " + st.kw("AGGREGATE");
  out += " " + st.kw(function ? "FUNCTION" : "PROCEDURE") + " " + st.id(r.name) + "(";
  for (size_t i = 0; i < r.parameters.size(); ++i) {
    const auto &p = r.parameters[i];
    if (i > 0) out += ", ";
    if (!p.mode.empty() && !function) out += st.kw(upper(p.mode)) + " ";
    out += st.id(p.name) + " " + p.type;
  }
  out += ")";
  if (function) out += " " + st.kw("RETURNS") + " " + r.returns;
  if (r.comment) out += " " + st.kw("COMMENT") + " " + sql_string(*r.comment);
  if (r.deterministic) out += " " + st.kw(*r.deterministic ? "DETERMINISTIC" : "NOT DETERMINISTIC");
  if (!r.data_access.empty()) out += " " + st.kw(upper(r.data_access));
  if (!r.security.empty()) out += " " + st.kw("SQL SECURITY") + " " + st.kw(upper(r.security));
  out += " " + std::string(trim(*r.body));
  return out;
}

}  // namespace mdbls::edit_text
