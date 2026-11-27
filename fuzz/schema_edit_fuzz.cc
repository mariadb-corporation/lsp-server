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

// replace_table as msm uses it: a script with a table `t`, and another
// CREATE TABLE `t` made from the same columns, keys and checks, outlined
// and sent.  The edit is refused for a reason of the request, never by its
// self-check; done, the files' edits give their texts, the table has the
// columns in the order sent, and sending the same table again changes
// nothing.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "fuzz_util.h"
#include "mdbls/completion.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/schema_edit.h"
#include "mdbls/statement_index.h"

using namespace mdbls;

namespace {

Parser_pool &pool() {
  static Parser_pool p(1);
  return p;
}

const char *const k_names[] = {"a", "b", "c", "d", "e", "p_id"};
const char *const k_types[] = {"INT", "BIGINT", "VARCHAR(10)", "VARCHAR(20)", "TEXT", "DECIMAL(10,2)", "DATE"};

struct Column_def {
  std::string name;
  int type = 0;
  std::string sql;
};

// One CREATE TABLE t of columns, keys and checks the fuzzer chooses; and
// sometimes a column added by an ALTER TABLE after it.
std::string table_sql(FuzzedDataProvider &data) {
  std::vector<Column_def> columns;
  for (const char *name : k_names) {
    if (!data.ConsumeBool()) continue;
    Column_def c;
    c.name = name;
    c.type = c.name == "p_id" ? 0 : data.ConsumeIntegralInRange<int>(0, 6);
    c.sql = c.name + " " + k_types[c.type];
    const auto bits = data.ConsumeIntegral<uint8_t>();
    if (bits & 1) c.sql += " NOT NULL";
    if (bits & 2) {
      static const char *const defaults[] = {"0", "0", "'x'", "'x'", "", "1.5", "'2020-01-01'"};
      if (*defaults[c.type] != '\0') c.sql += std::string(" DEFAULT ") + defaults[c.type];
    }
    if (bits & 4) c.sql += " COMMENT 'the " + c.name + "'";
    if ((bits & 8) && c.type <= 1) c.sql += " CHECK (" + c.name + " >= 0)";
    if ((bits & 16) && c.type <= 1 && c.name != "p_id") c.sql += " UNIQUE";
    columns.push_back(std::move(c));
  }
  if (columns.empty()) columns.push_back({"a", 0, "a INT"});
  // Another order.
  for (size_t k = columns.size(); k > 1; --k) {
    std::swap(columns[k - 1], columns[data.ConsumeIntegralInRange<size_t>(0, k - 1)]);
  }
  std::vector<std::string> items;
  for (const auto &c : columns) items.push_back(c.sql);
  const auto pick = [&] { return columns[data.ConsumeIntegralInRange<size_t>(0, columns.size() - 1)]; };
  const auto keyable = [](const Column_def &c) { return c.type != 4; };
  if (data.ConsumeBool()) {
    const auto c = pick();
    if (keyable(c)) items.push_back("PRIMARY KEY (" + c.name + ")");
  }
  for (const char *ix : {"ix1", "ix2"}) {
    if (!data.ConsumeBool()) continue;
    const auto c = pick();
    const auto d = pick();
    std::string parts = c.type == 4 ? c.name + "(10)" : c.name;
    if (d.name != c.name && data.ConsumeBool()) parts += ", " + (d.type == 4 ? d.name + "(5)" : d.name);
    items.push_back(std::string(data.ConsumeBool() ? "UNIQUE " : "") + "KEY " + (data.ConsumeBool() ? ix : "") + " (" +
                    parts + ")");
  }
  if (std::any_of(columns.begin(), columns.end(), [](const Column_def &c) { return c.name == "p_id"; }) &&
      data.ConsumeBool()) {
    items.push_back(std::string(data.ConsumeBool() ? "CONSTRAINT fk_t_p " : "") + "FOREIGN KEY (p_id) REFERENCES p (id)" +
                    (data.ConsumeBool() ? " ON DELETE CASCADE" : ""));
  }
  if (data.ConsumeBool()) {
    const auto c = pick();
    if (c.type <= 1) {
      items.push_back(std::string(data.ConsumeBool() ? "CONSTRAINT ck1 " : "") + "CHECK (" + c.name +
                      (data.ConsumeBool() ? " < 100)" : " <> 7)"));
    }
  }
  const bool lines = data.ConsumeBool();
  std::string sql = "CREATE TABLE t (";
  for (size_t k = 0; k < items.size(); ++k) {
    sql += lines ? "\n  " : (k == 0 ? "" : " ");
    sql += items[k] + (k + 1 < items.size() ? "," : "");
  }
  sql += lines ? "\n)" : ")";
  const auto options = data.ConsumeIntegral<uint8_t>();
  if (options & 1) sql += " ENGINE=InnoDB";
  if (options & 2) sql += " COMMENT='t'";
  if (options & 4) sql += " ROW_FORMAT=DYNAMIC";
  sql += ";\n";
  if (options & 8) sql += "ALTER TABLE t ADD COLUMN g INT;\n";
  return sql;
}

std::string script_with(const std::string &t) { return "CREATE TABLE p (id INT PRIMARY KEY);\n" + t; }

const Table *table_t(const Outline &o) {
  const Schema *s = o.catalog.find("");
  return s == nullptr ? nullptr : s->table("t");
}

std::variant<Edit_result, Edit_error> replace(const std::string &script, const Table &sent,
                                              std::vector<std::pair<std::string, std::string>> renames) {
  Edit_request request;
  Schema_edit e;
  e.op = Edit_op::replace_table;
  e.new_table = sent;
  e.renames = std::move(renames);
  request.edits.push_back(std::move(e));
  return apply_schema_edits(script, request, pool());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
  FuzzedDataProvider data(bytes, size);
  const std::string script = script_with(table_sql(data));
  const std::string agent = script_with(table_sql(data));
  const auto index = Statement_index::build(agent, {});
  const Outline o = build_outline(agent, index, pool());
  const Table *sent = table_t(o);
  if (sent == nullptr) return 0;
  // A column the agent's table drops renamed to one it adds, sometimes.
  std::vector<std::pair<std::string, std::string>> renames;
  if (data.ConsumeBool()) {
    const auto old_index = Statement_index::build(script, {});
    const Outline old = build_outline(script, old_index, pool());
    if (const Table *t = table_t(old)) {
      std::string from, to;
      for (const auto &c : t->columns) {
        if (sent->column(c.name) == nullptr) from = c.name;
      }
      for (const auto &c : sent->columns) {
        if (t->column(c.name) == nullptr) to = c.name;
      }
      if (!from.empty() && !to.empty()) renames.emplace_back(from, to);
    }
  }

  const auto r = replace(script, *sent, renames);
  if (const auto *e = std::get_if<Edit_error>(&r)) {
    if (e->kind == "self-check") {
      std::fprintf(stderr, "%s\n--- script\n%s--- sent\n%s", e->message.c_str(), script.c_str(), agent.c_str());
    }
    MDBLS_FUZZ_CHECK(e->kind != "self-check");
    return 0;
  }
  const auto &result = std::get<Edit_result>(r);
  for (const auto &f : result.files) MDBLS_FUZZ_CHECK(f.text == apply_edits(f.original, f.edits));
  const Table *now = table_t(result.outline);
  MDBLS_FUZZ_CHECK(now != nullptr && now->columns.size() == sent->columns.size());
  for (size_t k = 0; k < sent->columns.size(); ++k) MDBLS_FUZZ_CHECK(now->columns[k].name == sent->columns[k].name);

  // Again: nothing to do.
  const std::string &edited = result.files[0].text;
  const auto again = replace(edited, *sent, {});
  if (const auto *e = std::get_if<Edit_error>(&again)) {
    std::fprintf(stderr, "%s: %s\n---\n%s\n", e->kind.c_str(), e->message.c_str(), edited.c_str());
  }
  MDBLS_FUZZ_CHECK(std::holds_alternative<Edit_result>(again));
  const auto &second = std::get<Edit_result>(again);
  MDBLS_FUZZ_CHECK(second.migration.empty());
  MDBLS_FUZZ_CHECK(second.notes.size() == 1 && second.notes[0].kind == "unchanged");
  MDBLS_FUZZ_CHECK(second.files[0].text == edited);
  return 0;
}
