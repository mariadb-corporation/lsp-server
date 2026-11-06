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

#include "mdbls/schema_edit.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>

#include "mdbls/catalog_view.h"
#include "mdbls/keywords.h"
#include "mdbls/lsp/json_position.h"
#include "mdbls/navigation.h"
#include "mdbls/sql_names.h"
#include "mdbls/syntax_tree.h"
#include "mdbls/workspace_catalog.h"
#include "mdbparse.h"
#include "schema_edit_text.h"
#include "text_util.h"

namespace mdbls {

namespace {

struct Op_name {
  Edit_op op;
  const char *name;
};

constexpr Op_name k_ops[] = {
    {Edit_op::rename_table, "rename_table"},
    {Edit_op::rename_column, "rename_column"},
    {Edit_op::rename_view, "rename_view"},
    {Edit_op::rename_routine, "rename_routine"},
    {Edit_op::add_table, "add_table"},
    {Edit_op::drop_table, "drop_table"},
    {Edit_op::add_column, "add_column"},
    {Edit_op::modify_column, "modify_column"},
    {Edit_op::drop_column, "drop_column"},
    {Edit_op::add_index, "add_index"},
    {Edit_op::drop_index, "drop_index"},
    {Edit_op::add_foreign_key, "add_foreign_key"},
    {Edit_op::drop_foreign_key, "drop_foreign_key"},
    {Edit_op::set_table_options, "set_table_options"},
    {Edit_op::add_view, "add_view"},
    {Edit_op::drop_view, "drop_view"},
    {Edit_op::add_routine, "add_routine"},
    {Edit_op::drop_routine, "drop_routine"},
    {Edit_op::set_primary_key, "set_primary_key"},
    {Edit_op::add_trigger, "add_trigger"},
    {Edit_op::drop_trigger, "drop_trigger"},
    {Edit_op::rename_trigger, "rename_trigger"},
    {Edit_op::add_event, "add_event"},
    {Edit_op::drop_event, "drop_event"},
    {Edit_op::rename_event, "rename_event"},
    {Edit_op::add_sequence, "add_sequence"},
    {Edit_op::drop_sequence, "drop_sequence"},
    {Edit_op::rename_sequence, "rename_sequence"},
};

// The longest name the server takes for a table, column or routine.
constexpr size_t k_max_name_characters = 64;

Edit_error error(std::optional<size_t> edit, std::string kind, std::string message) {
  return {edit, std::move(kind), std::move(message)};
}

// `name` written as a token spelling the old name was: in backticks or
// double quotes when it was, else quoted only when it must be.
std::string spell_like(std::string_view token, std::string_view name) {
  if (!token.empty() && token[0] == '`') return backtick_quote(name);
  if (!token.empty() && token[0] == '"') {
    std::string out = "\"";
    for (const char c : name) {
      if (c == '"') out += '"';
      out += c;
    }
    return out + '"';
  }
  return quote_identifier(name);
}

bool is_word_char(char c) {
  const auto u = static_cast<unsigned char>(c);
  return std::isalnum(u) || c == '_' || c == '$' || u >= 0x80;
}

// Whether `text` holds `word` as a whole word, in any case.
bool has_word(std::string_view text, std::string_view word) {
  if (word.empty() || text.size() < word.size()) return false;
  for (size_t p = 0; p + word.size() <= text.size(); ++p) {
    if (!iequals(text.substr(p, word.size()), word)) continue;
    const bool before = p == 0 || !is_word_char(text[p - 1]);
    const bool after = p + word.size() == text.size() || !is_word_char(text[p + word.size()]);
    if (before && after) return true;
  }
  return false;
}

// Whether `columns` holds `name`, in any case.
bool names(const std::vector<std::string> &columns, std::string_view name) {
  return std::any_of(columns.begin(), columns.end(), [&](const std::string &c) { return iequals(c, name); });
}

// The unquoted last part of a qualified name as written: `s`.`t` -> t.
std::string last_name_part(std::string_view qualified) {
  char quote = 0;
  size_t start = 0;
  for (size_t p = 0; p < qualified.size(); ++p) {
    const char c = qualified[p];
    if (quote != 0) {
      if (c == quote) quote = 0;
    } else if (c == '`' || c == '"') {
      quote = c;
    } else if (c == '.') {
      start = p + 1;
    }
  }
  return unquote_name(trim(qualified.substr(start)));
}

// A note's message without the "FILE:LINE: " a note on an included
// statement starts with, so a note that only moved compares equal.
std::string note_key(const Note &n) {
  std::string_view m = n.message;
  if (n.source > 0) {
    const size_t colon = m.find(':');
    if (colon != std::string_view::npos) {
      size_t p = colon + 1;
      while (p < m.size() && std::isdigit(static_cast<unsigned char>(m[p]))) ++p;
      if (p > colon + 1 && m.substr(p, 2) == ": ") m.remove_prefix(p + 2);
    }
  }
  return std::string(to_string(n.kind)) + "\n" + std::string(m);
}

}  // namespace

std::optional<Edit_op> edit_op_from_string(std::string_view op) {
  for (const auto &o : k_ops) {
    if (op == o.name) return o.op;
  }
  return std::nullopt;
}

const char *to_string(Edit_op op) {
  for (const auto &o : k_ops) {
    if (o.op == op) return o.name;
  }
  return "";
}

std::string supported_edit_ops() {
  std::string out;
  for (const auto &o : k_ops) {
    if (!out.empty()) out += ' ';
    out += o.name;
  }
  return out;
}

const char *to_string(Migration_action a) {
  switch (a) {
    case Migration_action::create: return "create";
    case Migration_action::drop: return "drop";
    case Migration_action::add: return "add";
    case Migration_action::alter: return "alter";
    case Migration_action::rename: return "rename";
  }
  return "";
}

const char *to_string(Migration_object o) {
  switch (o) {
    case Migration_object::table: return "table";
    case Migration_object::view: return "view";
    case Migration_object::procedure: return "procedure";
    case Migration_object::function: return "function";
    case Migration_object::trigger: return "trigger";
    case Migration_object::event: return "event";
    case Migration_object::sequence: return "sequence";
    case Migration_object::column: return "column";
    case Migration_object::index: return "index";
    case Migration_object::primary_key: return "primary_key";
    case Migration_object::foreign_key: return "foreign_key";
    case Migration_object::table_options: return "table_options";
  }
  return "";
}

std::string edit_ops_json() {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.StartObject();
  w.Key("format");
  w.Int(1);
  w.Key("ops");
  w.StartArray();
  for (const auto &o : k_ops) w.String(o.name);
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

// ---------------------------------------------------------------------------
// Edit_log

void Edit_log::apply(std::vector<Text_edit> edits) {
  // Back to front; of two inserts at one place the later one first, so
  // they end up in the order given.
  std::stable_sort(edits.begin(), edits.end(),
                   [](const Text_edit &a, const Text_edit &b) { return a.begin < b.begin; });
  for (auto it = edits.rbegin(); it != edits.rend(); ++it) apply_one(*it);
}

void Edit_log::apply_one(const Text_edit &e) {
  // The entries whose written text the edit touches merge with it into one
  // entry: the union of their original ranges, the text as it is then.
  auto delta_of = [](const Text_edit &x) { return static_cast<int64_t>(x.text.size()) - (x.end - x.begin); };
  auto current_begin = [](const Text_edit &x, int64_t delta) { return static_cast<uint32_t>(x.begin + delta); };
  int64_t delta = 0;  // current - original, before entry i
  size_t i = 0;
  for (; i < m_edits.size() && current_begin(m_edits[i], delta) + m_edits[i].text.size() < e.begin; ++i) {
    delta += delta_of(m_edits[i]);
  }
  const size_t first = i;
  const int64_t delta_before = delta;
  uint32_t cs = e.begin;
  uint32_t ce = e.end;
  for (; i < m_edits.size() && current_begin(m_edits[i], delta) <= e.end; ++i) {
    const uint32_t cb = current_begin(m_edits[i], delta);
    cs = std::min(cs, cb);
    ce = std::max(ce, static_cast<uint32_t>(cb + m_edits[i].text.size()));
    delta += delta_of(m_edits[i]);
  }
  Text_edit merged{static_cast<uint32_t>(cs - delta_before), static_cast<uint32_t>(ce - delta),
                   m_text.substr(cs, e.begin - cs) + e.text + m_text.substr(e.end, ce - e.end)};
  m_text.replace(e.begin, e.end - e.begin, e.text);
  const auto at = m_edits.erase(m_edits.begin() + static_cast<std::ptrdiff_t>(first),
                                m_edits.begin() + static_cast<std::ptrdiff_t>(i));
  if (std::string_view(m_original).substr(merged.begin, merged.end - merged.begin) != merged.text) {
    m_edits.insert(at, std::move(merged));
  }
}

uint32_t Edit_log::to_original(uint32_t current) const {
  int64_t delta = 0;
  for (const auto &x : m_edits) {
    const auto cb = static_cast<uint32_t>(x.begin + delta);
    if (current < cb) break;
    if (current < cb + x.text.size()) return x.begin;
    delta += static_cast<int64_t>(x.text.size()) - (x.end - x.begin);
  }
  return static_cast<uint32_t>(current - delta);
}

std::optional<uint32_t> Edit_log::to_current(uint32_t original, bool after_insert) const {
  int64_t delta = 0;
  for (const auto &x : m_edits) {
    if (original < x.begin) break;
    if (original == x.begin) {
      if (x.begin == x.end && after_insert) {
        return static_cast<uint32_t>(original + delta + static_cast<int64_t>(x.text.size()));
      }
      return static_cast<uint32_t>(original + delta);
    }
    if (original < x.end) return std::nullopt;
    delta += static_cast<int64_t>(x.text.size()) - (x.end - x.begin);
  }
  return static_cast<uint32_t>(original + delta);
}

// ---------------------------------------------------------------------------
// Request

namespace {

using Json = rapidjson::Value;

// An option's name as a request gives it: letters, digits, underscores.
bool option_name(std::string_view key) {
  return !key.empty() && std::all_of(key.begin(), key.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  });
}

// Reads the members of request objects, stopping at the first problem.
class Json_reader {
 public:
  explicit Json_reader(size_t edit) : m_edit(edit) {}

  const std::optional<Edit_error> &bad() const { return m_bad; }
  bool ok() const { return !m_bad; }
  void fail(std::string message) {
    if (!m_bad) m_bad = error(m_edit, "bad-request", std::move(message));
  }

  // `v` is an object of no other members than `allowed` (and the
  // positions an outline object carries, which are ignored).
  bool object(const Json &v, const std::string &what, std::initializer_list<const char *> allowed) {
    if (!ok()) return false;
    if (!v.IsObject()) {
      fail(what + " is not an object");
      return false;
    }
    for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
      const std::string_view m(it->name.GetString(), it->name.GetStringLength());
      if (m == "range" || m == "selection_range" || m == "source") continue;
      if (std::none_of(allowed.begin(), allowed.end(), [&](const char *a) { return m == a; })) {
        fail(what + " has an unknown member \"" + std::string(m) + "\"");
        return false;
      }
    }
    return true;
  }

  static bool given(const Json &o, const char *m) { return o.HasMember(m) && !o[m].IsNull(); }

  // A string member; `required`: present and not empty.
  void str(const Json &o, const char *m, std::string *out, bool required, const std::string &what) {
    if (!ok()) return;
    if (!given(o, m)) {
      if (required) fail(what + " has no \"" + m + "\"");
      return;
    }
    if (!o[m].IsString()) {
      fail("\"" + std::string(m) + "\" of " + what + " is not a string");
      return;
    }
    *out = std::string(o[m].GetString(), o[m].GetStringLength());
    if (required && out->empty()) fail("\"" + std::string(m) + "\" of " + what + " is empty");
  }
  void opt_str(const Json &o, const char *m, std::optional<std::string> *out, const std::string &what) {
    if (!given(o, m)) return;
    std::string v;
    str(o, m, &v, false, what);
    if (ok()) *out = std::move(v);
  }
  // A string that must be one of `values` (compared in any case), stored
  // spelt as in `values`; "" when not given.
  void choice(const Json &o, const char *m, std::string *out, std::initializer_list<const char *> values,
              const std::string &what) {
    str(o, m, out, false, what);
    if (!ok() || out->empty()) return;
    for (const char *v : values) {
      if (iequals(*out, v)) {
        *out = v;
        return;
      }
    }
    fail("\"" + std::string(m) + "\" of " + what + " is not one of the values it takes: " + *out);
  }
  void boolean(const Json &o, const char *m, bool *out, const std::string &what) {
    if (!ok() || !given(o, m)) return;
    if (!o[m].IsBool()) {
      fail("\"" + std::string(m) + "\" of " + what + " is not true or false");
      return;
    }
    *out = o[m].GetBool();
  }
  void opt_bool(const Json &o, const char *m, std::optional<bool> *out, const std::string &what) {
    if (!given(o, m)) return;
    bool v = false;
    boolean(o, m, &v, what);
    if (ok()) *out = v;
  }
  void strings(const Json &o, const char *m, std::vector<std::string> *out, bool required,
               const std::string &what) {
    if (!ok()) return;
    if (!given(o, m)) {
      if (required) fail(what + " has no \"" + m + "\"");
      return;
    }
    if (!o[m].IsArray()) {
      fail("\"" + std::string(m) + "\" of " + what + " is not an array");
      return;
    }
    for (const auto &x : o[m].GetArray()) {
      if (!x.IsString() || x.GetStringLength() == 0) {
        fail("\"" + std::string(m) + "\" of " + what + " holds something that is not a name");
        return;
      }
      out->emplace_back(x.GetString(), x.GetStringLength());
    }
    if (required && out->empty()) fail("\"" + std::string(m) + "\" of " + what + " is empty");
  }
  // An object of string values (null leaves the option out).
  void options(const Json &o, const char *m, std::vector<std::pair<std::string, std::string>> *out,
               const std::string &what) {
    if (!ok() || !given(o, m)) return;
    if (!o[m].IsObject()) {
      fail("\"" + std::string(m) + "\" of " + what + " is not an object");
      return;
    }
    for (auto it = o[m].MemberBegin(); it != o[m].MemberEnd(); ++it) {
      if (it->value.IsNull()) continue;
      const std::string key(it->name.GetString(), it->name.GetStringLength());
      if (!option_name(key)) {
        fail("\"" + key + "\" of " + what + " is not an option");
        return;
      }
      if (!it->value.IsString()) {
        fail("the option \"" + key + "\" of " + what + " is not a string");
        return;
      }
      out->emplace_back(lower(key), std::string(it->value.GetString(), it->value.GetStringLength()));
    }
  }
  // An array member, each element read by `each`.
  template <typename F>
  void array(const Json &o, const char *m, const std::string &what, F each) {
    if (!ok() || !given(o, m)) return;
    if (!o[m].IsArray()) {
      fail("\"" + std::string(m) + "\" of " + what + " is not an array");
      return;
    }
    size_t k = 0;
    for (const auto &x : o[m].GetArray()) {
      if (!ok()) return;
      each(x, k++);
    }
  }

 private:
  size_t m_edit;
  std::optional<Edit_error> m_bad;
};

std::optional<Column> read_column(Json_reader &r, const Json &v, const std::string &what) {
  if (!r.object(v, what, {"name", "type", "nullable", "default", "auto_increment", "comment", "charset",
                          "collation", "generated", "generated_storage", "generated_row", "on_update",
                          "invisible", "system_versioning", "options"})) {
    return std::nullopt;
  }
  Column c;
  r.str(v, "name", &c.name, true, what);
  const std::string w = c.name.empty() ? what : "column `" + c.name + "`";
  r.str(v, "type", &c.type, true, w);
  r.boolean(v, "nullable", &c.nullable, w);
  r.opt_str(v, "default", &c.default_value, w);
  r.boolean(v, "auto_increment", &c.auto_increment, w);
  r.opt_str(v, "comment", &c.comment, w);
  r.opt_str(v, "charset", &c.charset, w);
  r.opt_str(v, "collation", &c.collation, w);
  r.opt_str(v, "generated", &c.generated, w);
  std::string storage, row;
  r.choice(v, "generated_storage", &storage, {"VIRTUAL", "PERSISTENT", "STORED"}, w);
  r.choice(v, "generated_row", &row, {"START", "END"}, w);
  if (!storage.empty()) c.generated_storage = storage;
  if (!row.empty()) c.generated_row = row;
  r.opt_str(v, "on_update", &c.on_update, w);
  r.boolean(v, "invisible", &c.invisible, w);
  r.opt_bool(v, "system_versioning", &c.system_versioning, w);
  r.options(v, "options", &c.options, w);
  if (!r.ok()) return std::nullopt;
  return c;
}

std::optional<Index> read_index(Json_reader &r, const Json &v, const std::string &what) {
  if (!r.object(v, what, {"name", "kind", "columns", "key_parts", "using", "comment", "ignored",
                          "without_overlaps", "options"})) {
    return std::nullopt;
  }
  Index ix;
  r.str(v, "name", &ix.name, false, what);
  const std::string w = ix.name.empty() ? what : "index `" + ix.name + "`";
  r.choice(v, "kind", &ix.kind, {"primary", "unique", "index", "fulltext", "spatial", "vector"}, w);
  if (r.ok() && ix.kind.empty()) r.fail(w + " has no \"kind\"");
  std::vector<std::string> columns;
  r.strings(v, "columns", &columns, false, w);
  for (auto &c : columns) ix.key_parts.emplace_back().column = std::move(c);
  if (Json_reader::given(v, "key_parts")) {
    if (!ix.key_parts.empty()) r.fail(w + " has both \"columns\" and \"key_parts\"");
    r.array(v, "key_parts", w, [&](const Json &x, size_t k) {
      const std::string kw = "key part " + std::to_string(k + 1) + " of " + w;
      if (!r.object(x, kw, {"column", "length", "order", "expression"})) return;
      Key_part kp;
      r.str(x, "column", &kp.column, true, kw);
      if (Json_reader::given(x, "length")) {
        if (!x["length"].IsUint() || x["length"].GetUint() == 0) r.fail("\"length\" of " + kw + " is not a positive number");
        else kp.length = x["length"].GetUint();
      }
      r.choice(x, "order", &kp.order, {"ASC", "DESC"}, kw);
      if (Json_reader::given(x, "expression")) r.fail(kw + ": MariaDB has no expression key parts");
      ix.key_parts.push_back(std::move(kp));
    });
  }
  if (r.ok() && ix.key_parts.empty()) r.fail(w + " has no \"columns\"");
  r.choice(v, "using", &ix.using_, {"BTREE", "HASH", "RTREE"}, w);
  r.opt_str(v, "comment", &ix.comment, w);
  r.boolean(v, "ignored", &ix.ignored, w);
  r.str(v, "without_overlaps", &ix.without_overlaps, false, w);
  r.options(v, "options", &ix.options, w);
  if (!r.ok()) return std::nullopt;
  return ix;
}

std::optional<Foreign_key> read_foreign_key(Json_reader &r, const Json &v, const std::string &what) {
  if (!r.object(v, what, {"name", "index_name", "columns", "references", "match", "on_delete", "on_update"})) {
    return std::nullopt;
  }
  Foreign_key fk;
  r.str(v, "name", &fk.name, false, what);
  const std::string w = fk.name.empty() ? what : "foreign key `" + fk.name + "`";
  r.str(v, "index_name", &fk.index_name, false, w);
  r.strings(v, "columns", &fk.columns, true, w);
  if (r.ok() && !Json_reader::given(v, "references")) r.fail(w + " has no \"references\"");
  if (r.ok()) {
    const Json &ref = v["references"];
    const std::string rw = "\"references\" of " + w;
    if (r.object(ref, rw, {"schema", "table", "columns"})) {
      r.str(ref, "schema", &fk.ref_schema, false, rw);
      r.str(ref, "table", &fk.ref_table, true, rw);
      r.strings(ref, "columns", &fk.ref_columns, true, rw);
    }
  }
  if (r.ok() && fk.columns.size() != fk.ref_columns.size()) {
    r.fail(w + " has " + std::to_string(fk.columns.size()) + " columns but references " +
           std::to_string(fk.ref_columns.size()));
  }
  r.choice(v, "match", &fk.match, {"FULL", "PARTIAL", "SIMPLE"}, w);
  const auto rules = {"CASCADE", "RESTRICT", "SET NULL", "NO ACTION", "SET DEFAULT"};
  r.choice(v, "on_delete", &fk.on_delete, rules, w);
  r.choice(v, "on_update", &fk.on_update, rules, w);
  if (!r.ok()) return std::nullopt;
  return fk;
}

std::optional<Table> read_table(Json_reader &r, const Json &v) {
  const std::string what = "\"table\"";
  if (!r.object(v, what, {"name", "temporary", "columns", "indexes", "foreign_keys", "checks", "options",
                          "partitioning", "system_versioning", "periods"})) {
    return std::nullopt;
  }
  Table t;
  r.str(v, "name", &t.name, true, what);
  const std::string w = "table `" + t.name + "`";
  r.boolean(v, "temporary", &t.temporary, w);
  if (r.ok() && !Json_reader::given(v, "columns")) r.fail(w + " has no \"columns\"");
  r.array(v, "columns", w, [&](const Json &x, size_t k) {
    if (auto c = read_column(r, x, "column " + std::to_string(k + 1) + " of " + w)) t.columns.push_back(std::move(*c));
  });
  if (r.ok() && t.columns.empty()) r.fail(w + " has no columns");
  r.array(v, "indexes", w, [&](const Json &x, size_t k) {
    if (auto ix = read_index(r, x, "index " + std::to_string(k + 1) + " of " + w)) t.indexes.push_back(std::move(*ix));
  });
  r.array(v, "foreign_keys", w, [&](const Json &x, size_t k) {
    if (auto fk = read_foreign_key(r, x, "foreign key " + std::to_string(k + 1) + " of " + w)) {
      t.foreign_keys.push_back(std::move(*fk));
    }
  });
  r.array(v, "checks", w, [&](const Json &x, size_t k) {
    const std::string cw = "check " + std::to_string(k + 1) + " of " + w;
    if (!r.object(x, cw, {"name", "expression", "column"})) return;
    Check ck;
    r.str(x, "name", &ck.name, false, cw);
    r.str(x, "expression", &ck.expression, true, cw);
    r.str(x, "column", &ck.column, false, cw);
    t.checks.push_back(std::move(ck));
  });
  r.options(v, "options", &t.options, w);
  r.str(v, "partitioning", &t.partitioning, false, w);
  r.boolean(v, "system_versioning", &t.system_versioning, w);
  r.array(v, "periods", w, [&](const Json &x, size_t k) {
    const std::string pw = "period " + std::to_string(k + 1) + " of " + w;
    if (!r.object(x, pw, {"name", "start", "end"})) return;
    Period p;
    r.str(x, "name", &p.name, true, pw);
    r.str(x, "start", &p.start, true, pw);
    r.str(x, "end", &p.end, true, pw);
    t.periods.push_back(std::move(p));
  });
  if (!r.ok()) return std::nullopt;
  return t;
}

std::optional<View> read_view(Json_reader &r, const Json &v) {
  const std::string what = "\"view\"";
  if (!r.object(v, what, {"name", "columns", "definition", "algorithm", "security", "definer", "check_option"})) {
    return std::nullopt;
  }
  View vw;
  r.str(v, "name", &vw.name, true, what);
  const std::string w = "view `" + vw.name + "`";
  r.strings(v, "columns", &vw.columns, false, w);
  r.str(v, "definition", &vw.definition, true, w);
  if (r.ok() && trim(vw.definition).empty()) r.fail(w + " has an empty \"definition\"");
  r.choice(v, "algorithm", &vw.algorithm, {"UNDEFINED", "MERGE", "TEMPTABLE"}, w);
  r.choice(v, "security", &vw.security, {"DEFINER", "INVOKER"}, w);
  r.str(v, "definer", &vw.definer, false, w);
  r.choice(v, "check_option", &vw.check_option, {"LOCAL", "CASCADED"}, w);
  if (!r.ok()) return std::nullopt;
  return vw;
}

std::optional<Routine> read_routine(Json_reader &r, const Json &v) {
  const std::string what = "\"routine\"";
  if (!r.object(v, what, {"name", "kind", "parameters", "returns", "body", "deterministic", "data_access",
                          "security", "definer", "comment", "aggregate", "soname"})) {
    return std::nullopt;
  }
  Routine rt;
  r.str(v, "name", &rt.name, true, what);
  const std::string w = "routine `" + rt.name + "`";
  r.choice(v, "kind", &rt.kind, {"procedure", "function"}, w);
  if (r.ok() && rt.kind.empty()) r.fail(w + " has no \"kind\"");
  r.array(v, "parameters", w, [&](const Json &x, size_t k) {
    const std::string pw = "parameter " + std::to_string(k + 1) + " of " + w;
    if (!r.object(x, pw, {"mode", "name", "type"})) return;
    Parameter p;
    r.choice(x, "mode", &p.mode, {"IN", "OUT", "INOUT"}, pw);
    if (r.ok() && rt.kind == "function" && !p.mode.empty() && p.mode != "IN") {
      r.fail(pw + ": a function's parameters are IN");
    }
    r.str(x, "name", &p.name, true, pw);
    r.str(x, "type", &p.type, true, pw);
    rt.parameters.push_back(std::move(p));
  });
  r.str(v, "returns", &rt.returns, rt.kind == "function", w);
  if (r.ok() && rt.kind == "procedure" && !rt.returns.empty()) r.fail(w + ": a procedure has no \"returns\"");
  std::string body;
  r.str(v, "body", &body, true, w);
  if (r.ok() && trim(body).empty()) r.fail(w + " has an empty \"body\"");
  rt.body = body;
  r.opt_bool(v, "deterministic", &rt.deterministic, w);
  r.choice(v, "data_access", &rt.data_access, {"CONTAINS SQL", "NO SQL", "READS SQL DATA", "MODIFIES SQL DATA"}, w);
  r.choice(v, "security", &rt.security, {"DEFINER", "INVOKER"}, w);
  r.str(v, "definer", &rt.definer, false, w);
  r.opt_str(v, "comment", &rt.comment, w);
  r.boolean(v, "aggregate", &rt.aggregate, w);
  if (r.ok() && rt.aggregate && rt.kind != "function") r.fail(w + ": only a function is AGGREGATE");
  if (r.ok() && Json_reader::given(v, "soname")) r.fail(w + ": a function from a library (SONAME) cannot be added");
  if (!r.ok()) return std::nullopt;
  return rt;
}

// The events of a trigger, `INSERT`, `UPDATE OR DELETE`: upper-cased, one
// OR between two, each at most once; "" when they are not.
std::string trigger_events(std::string_view events) {
  std::vector<std::string> words;
  const std::string all = collapse(upper(events));
  for (size_t p = 0; p < all.size();) {
    const size_t q = std::min(all.find(' ', p), all.size());
    words.push_back(all.substr(p, q - p));
    p = q + 1;
  }
  if (words.size() % 2 == 0) return "";
  std::set<std::string> seen;
  std::string out;
  for (size_t k = 0; k < words.size(); ++k) {
    if (k % 2 == 1) {
      if (words[k] != "OR") return "";
      out += " OR ";
      continue;
    }
    if (words[k] != "INSERT" && words[k] != "UPDATE" && words[k] != "DELETE") return "";
    if (!seen.insert(words[k]).second) return "";
    out += words[k];
  }
  return out;
}

// The events of a trigger's `events` (INSERT, UPDATE OR DELETE).
std::set<std::string> event_set(std::string_view events) {
  const std::string all = trigger_events(events);
  std::set<std::string> out;
  for (const char *w : {"INSERT", "UPDATE", "DELETE"}) {
    if (has_word(all, w)) out.insert(w);
  }
  return out;
}

std::optional<Trigger> read_trigger(Json_reader &r, const Json &v) {
  const std::string what = "\"trigger\"";
  if (!r.object(v, what, {"name", "timing", "events", "table", "body", "order", "definer", "update_columns"})) {
    return std::nullopt;
  }
  Trigger t;
  r.str(v, "name", &t.name, true, what);
  const std::string w = "trigger `" + t.name + "`";
  r.choice(v, "timing", &t.timing, {"BEFORE", "AFTER"}, w);
  if (r.ok() && t.timing.empty()) r.fail(w + " has no \"timing\"");
  std::string events;
  r.str(v, "events", &events, true, w);
  if (r.ok()) {
    t.events = trigger_events(events);
    if (t.events.empty()) {
      r.fail("\"events\" of " + w + " is not INSERT, UPDATE or DELETE, or several of them joined by OR: " + events);
    }
  }
  r.str(v, "table", &t.table, true, w);
  r.str(v, "body", &t.body, true, w);
  if (r.ok() && trim(t.body).empty()) r.fail(w + " has an empty \"body\"");
  if (r.ok() && Json_reader::given(v, "order")) {
    const Json &o = v["order"];
    const std::string ow = "\"order\" of " + w;
    if (r.object(o, ow, {"kind", "trigger"})) {
      r.choice(o, "kind", &t.order_kind, {"FOLLOWS", "PRECEDES"}, ow);
      if (r.ok() && t.order_kind.empty()) r.fail(ow + " has no \"kind\"");
      r.str(o, "trigger", &t.order_trigger, true, ow);
    }
  }
  r.str(v, "definer", &t.definer, false, w);
  r.strings(v, "update_columns", &t.update_columns, false, w);
  if (r.ok() && !t.update_columns.empty() && t.events.find("UPDATE") == std::string::npos) {
    r.fail(w + ": \"update_columns\" needs the UPDATE event");
  }
  if (!r.ok()) return std::nullopt;
  return t;
}

std::optional<Event> read_event(Json_reader &r, const Json &v) {
  const std::string what = "\"event\"";
  if (!r.object(v, what, {"name", "schedule", "on_completion_preserve", "status", "comment", "body", "definer"})) {
    return std::nullopt;
  }
  Event e;
  r.str(v, "name", &e.name, true, what);
  const std::string w = "event `" + e.name + "`";
  r.str(v, "schedule", &e.schedule, true, w);
  if (r.ok() && trim(e.schedule).empty()) r.fail(w + " has an empty \"schedule\"");
  r.opt_bool(v, "on_completion_preserve", &e.on_completion_preserve, w);
  r.choice(v, "status", &e.status, {"ENABLE", "DISABLE", "DISABLE ON SLAVE"}, w);
  r.opt_str(v, "comment", &e.comment, w);
  r.str(v, "body", &e.body, true, w);
  if (r.ok() && trim(e.body).empty()) r.fail(w + " has an empty \"body\"");
  r.str(v, "definer", &e.definer, false, w);
  if (!r.ok()) return std::nullopt;
  return e;
}

// An integer as a sequence option writes it: digits, a sign before them.
bool sequence_number(std::string_view v) {
  if (!v.empty() && (v[0] == '-' || v[0] == '+')) v.remove_prefix(1);
  return !v.empty() && std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::optional<Sequence> read_sequence(Json_reader &r, const Json &v) {
  const std::string what = "\"sequence\"";
  if (!r.object(v, what, {"name", "type", "start", "increment", "minvalue", "maxvalue", "cache", "cycle", "options"})) {
    return std::nullopt;
  }
  Sequence q;
  r.str(v, "name", &q.name, true, what);
  const std::string w = "sequence `" + q.name + "`";
  r.str(v, "type", &q.type, false, w);
  for (auto [member, out] : {std::pair{"start", &q.start}, std::pair{"increment", &q.increment},
                             std::pair{"minvalue", &q.minvalue}, std::pair{"maxvalue", &q.maxvalue},
                             std::pair{"cache", &q.cache}}) {
    r.str(v, member, out, false, w);
    if (r.ok() && !out->empty() && !sequence_number(*out)) {
      r.fail("\"" + std::string(member) + "\" of " + w + " is not an integer: " + *out);
    }
  }
  r.opt_bool(v, "cycle", &q.cycle, w);
  r.options(v, "options", &q.options, w);
  if (!r.ok()) return std::nullopt;
  return q;
}

// A name the server takes for a table, column, view or routine.
std::optional<std::string> bad_name(const std::string &name) {
  if (char_count(name) > k_max_name_characters) return "the name `" + name + "` is longer than 64 characters";
  if (name.back() == ' ' || name.find('\0') != std::string::npos) {
    return "the name `" + name + "` ends in a space or holds a NUL";
  }
  return std::nullopt;
}

}  // namespace

std::variant<Edit_request, Edit_error> parse_edit_request(std::string_view json) {
  rapidjson::Document d;
  d.Parse(json.data(), json.size());
  if (d.HasParseError() || !d.IsObject()) {
    return error(std::nullopt, "bad-request", "the request is not a JSON object");
  }
  if (!d.HasMember("format") || !d["format"].IsInt() || d["format"].GetInt() != 1) {
    return error(std::nullopt, "bad-request", "the request must have \"format\": 1");
  }
  Edit_request out;
  if (d.HasMember("schema") && !d["schema"].IsNull()) {
    if (!d["schema"].IsString()) return error(std::nullopt, "bad-request", "\"schema\" is not a string");
    out.schema = d["schema"].GetString();
  }
  if (!d.HasMember("edits") || !d["edits"].IsArray()) {
    return error(std::nullopt, "bad-request", "the request has no \"edits\" array");
  }
  size_t i = 0;
  for (const auto &e : d["edits"].GetArray()) {
    if (!e.IsObject() || !e.HasMember("op") || !e["op"].IsString()) {
      return error(i, "bad-request", "the edit has no \"op\"");
    }
    const auto op = edit_op_from_string(e["op"].GetString());
    // An op this binary does not know may be one a newer binary does.
    if (!op) {
      return error(i, "unsupported",
                   std::string("this binary does not know the op \"") + e["op"].GetString() + "\" (a newer one may)");
    }
    Schema_edit se;
    se.op = *op;
    Json_reader r(i);
    const std::string what = std::string("the ") + e["op"].GetString() + " edit";
    auto name = [&](const char *member, std::string *to, bool required = true) {
      r.str(e, member, to, required, what);
    };
    // The members of an op of PR 2 and later are checked: an unknown one is
    // a mistake the request should hear about.
    auto members = [&](std::initializer_list<const char *> allowed) { r.object(e, what, allowed); };
    auto object = [&](const char *member) -> const Json * {
      if (!r.ok()) return nullptr;
      if (!Json_reader::given(e, member)) {
        r.fail(what + " has no \"" + std::string(member) + "\"");
        return nullptr;
      }
      return &e[member];
    };
    std::vector<std::string> names_to_check;  // new names the server must take
    switch (*op) {
      case Edit_op::rename_table:
        name("table", &se.table);
        name("new_name", &se.new_name);
        break;
      case Edit_op::rename_column:
        name("table", &se.table);
        name("column", &se.column);
        name("new_name", &se.new_name);
        break;
      case Edit_op::rename_view:
        name("view", &se.view);
        name("new_name", &se.new_name);
        break;
      case Edit_op::rename_routine:
        name("routine", &se.routine);
        r.choice(e, "kind", &se.routine_kind, {"procedure", "function"}, what);
        name("new_name", &se.new_name);
        break;
      case Edit_op::add_table:
        members({"op", "table"});
        if (const Json *t = object("table")) se.new_table = read_table(r, *t);
        if (se.new_table) {
          names_to_check.push_back(se.new_table->name);
          for (const auto &c : se.new_table->columns) names_to_check.push_back(c.name);
        }
        break;
      case Edit_op::drop_table:
        members({"op", "table"});
        name("table", &se.table);
        break;
      case Edit_op::add_column:
        members({"op", "table", "column", "after", "first"});
        name("table", &se.table);
        if (const Json *c = object("column")) se.new_column = read_column(r, *c, "\"column\"");
        name("after", &se.after, false);
        r.boolean(e, "first", &se.first, what);
        if (r.ok() && se.first && !se.after.empty()) r.fail(what + " has both \"after\" and \"first\"");
        if (se.new_column) names_to_check.push_back(se.new_column->name);
        break;
      case Edit_op::modify_column:
        members({"op", "table", "column", "new_column"});
        name("table", &se.table);
        name("column", &se.column);
        if (const Json *c = object("new_column")) se.new_column = read_column(r, *c, "\"new_column\"");
        if (se.new_column) names_to_check.push_back(se.new_column->name);
        break;
      case Edit_op::drop_column:
        members({"op", "table", "column"});
        name("table", &se.table);
        name("column", &se.column);
        break;
      case Edit_op::set_table_options:
        members({"op", "table", "options"});
        name("table", &se.table);
        if (const Json *o = object("options")) {
          if (!o->IsObject() || o->MemberCount() == 0) {
            r.fail("\"options\" of " + what + " is not an object of options");
          } else {
            for (auto it = o->MemberBegin(); it != o->MemberEnd(); ++it) {
              const std::string key = lower(std::string_view(it->name.GetString(), it->name.GetStringLength()));
              if (!option_name(key)) {
                r.fail("\"" + std::string(it->name.GetString()) + "\" is not a table option");
              } else if (it->value.IsNull()) {
                se.options.emplace_back(key, std::nullopt);
              } else if (it->value.IsString() && it->value.GetStringLength() > 0) {
                se.options.emplace_back(key, std::string(it->value.GetString(), it->value.GetStringLength()));
              } else {
                r.fail("the option \"" + key + "\" is not a string or null");
              }
            }
          }
        }
        break;
      case Edit_op::add_view:
        members({"op", "view"});
        if (const Json *v = object("view")) se.new_view = read_view(r, *v);
        if (se.new_view) names_to_check.push_back(se.new_view->name);
        break;
      case Edit_op::drop_view:
        members({"op", "view"});
        name("view", &se.view);
        break;
      case Edit_op::add_routine:
        members({"op", "routine"});
        if (const Json *v = object("routine")) se.new_routine = read_routine(r, *v);
        if (se.new_routine) names_to_check.push_back(se.new_routine->name);
        break;
      case Edit_op::drop_routine:
        members({"op", "routine", "kind"});
        name("routine", &se.routine);
        r.choice(e, "kind", &se.routine_kind, {"procedure", "function"}, what);
        break;
      case Edit_op::add_index:
        members({"op", "table", "index"});
        name("table", &se.table);
        if (const Json *x = object("index")) se.new_index = read_index(r, *x, "\"index\"");
        break;
      case Edit_op::add_foreign_key:
        members({"op", "table", "foreign_key"});
        name("table", &se.table);
        if (const Json *x = object("foreign_key")) se.new_foreign_key = read_foreign_key(r, *x, "\"foreign_key\"");
        break;
      case Edit_op::drop_index:
      case Edit_op::drop_foreign_key: {
        // The key by name, by kind (the primary key), or by columns.
        const bool index = *op == Edit_op::drop_index;
        if (index) members({"op", "table", "name", "kind", "columns"});
        else members({"op", "table", "name", "columns"});
        name("table", &se.table);
        name("name", &se.name, false);
        if (index) r.choice(e, "kind", &se.kind, {"primary"}, what);
        r.strings(e, "columns", &se.columns, false, what);
        const int given = !se.name.empty() + !se.kind.empty() + !se.columns.empty();
        if (r.ok() && given != 1) {
          r.fail(what + std::string(index ? " names the index by one of \"name\", \"kind\" and \"columns\""
                                          : " names the foreign key by one of \"name\" and \"columns\""));
        }
        break;
      }
      case Edit_op::set_primary_key:
        members({"op", "table", "columns"});
        name("table", &se.table);
        r.strings(e, "columns", &se.columns, true, what);
        break;
      case Edit_op::add_trigger:
        members({"op", "trigger"});
        if (const Json *t = object("trigger")) se.new_trigger = read_trigger(r, *t);
        if (se.new_trigger) names_to_check.push_back(se.new_trigger->name);
        break;
      case Edit_op::drop_trigger:
        members({"op", "trigger", "table"});
        name("trigger", &se.trigger);
        name("table", &se.table, false);
        break;
      case Edit_op::rename_trigger:
        members({"op", "trigger", "new_name"});
        name("trigger", &se.trigger);
        name("new_name", &se.new_name);
        break;
      case Edit_op::add_event:
        members({"op", "event"});
        if (const Json *v = object("event")) se.new_event = read_event(r, *v);
        if (se.new_event) names_to_check.push_back(se.new_event->name);
        break;
      case Edit_op::drop_event:
        members({"op", "event"});
        name("event", &se.event);
        break;
      case Edit_op::rename_event:
        members({"op", "event", "new_name"});
        name("event", &se.event);
        name("new_name", &se.new_name);
        break;
      case Edit_op::add_sequence:
        members({"op", "sequence"});
        if (const Json *q = object("sequence")) se.new_sequence = read_sequence(r, *q);
        if (se.new_sequence) names_to_check.push_back(se.new_sequence->name);
        break;
      case Edit_op::drop_sequence:
        members({"op", "sequence"});
        name("sequence", &se.sequence);
        break;
      case Edit_op::rename_sequence:
        members({"op", "sequence", "new_name"});
        name("sequence", &se.sequence);
        name("new_name", &se.new_name);
        break;
      default:
        break;
    }
    if (r.bad()) return *r.bad();
    if (!se.new_name.empty()) names_to_check.push_back(se.new_name);
    for (const auto &n : names_to_check) {
      if (auto m = bad_name(n)) return error(i, "bad-request", *m);
    }
    out.edits.push_back(std::move(se));
    ++i;
  }
  return out;
}

// ---------------------------------------------------------------------------
// The session: the texts as edited so far and their outline.

namespace {

struct Session_file {
  std::string path;  // "" for the script
  Edit_log log;
};

// A change one edit makes, per file, in the current texts.
using Changes = std::map<size_t, std::vector<Text_edit>>;

class Session {
 public:
  Session(std::string_view script, Parser_pool &pool, const Edit_options &options)
      : m_pool(pool), m_options(options) {
    m_files.push_back({"", Edit_log(std::string(script))});
  }

  void build() {
    // A copy: reading an include may move the files.
    const std::string script = m_files[0].log.text();
    Source_reader read = [this](const std::string &path, std::string *text) {
      if (const auto it = m_by_path.find(path); it != m_by_path.end()) {
        *text = m_files[it->second].log.text();
        return true;
      }
      if (m_unreadable.count(path) != 0) return false;
      const bool ok = m_options.read ? m_options.read(path, text) : read_sql_file(path, text);
      if (!ok) {
        m_unreadable.insert(path);
        return false;
      }
      m_by_path[path] = m_files.size();
      m_files.push_back({path, Edit_log(*text)});
      return true;
    };
    Outline_options oo;
    oo.version_id = m_options.index.version_id;
    oo.schema = m_options.schema;
    auto expansion = expand_sources(script, m_options.source_dir, read);
    if (expansion.empty()) {
      m_plain_index = std::make_shared<const Statement_index>(
          Statement_index::build_parallel(script, m_options.index, 0));
      m_outline = std::make_shared<Outline>(build_outline(script, *m_plain_index, m_pool, oo));
    } else {
      m_plain_index.reset();
      m_outline = std::make_shared<Outline>(build_outline(std::move(expansion), m_pool, m_options.index, oo));
    }
  }

  const Outline &outline() const { return *m_outline; }
  std::shared_ptr<Outline> outline_ptr() const { return m_outline; }
  std::vector<Session_file> &files() { return m_files; }
  const Edit_options &options() const { return m_options; }
  Parser_pool &pool() { return m_pool; }
  Parser_cache &cache() { return m_cache; }

  // The text references are searched in: the expanded script, or the
  // script itself when it has no directives.
  std::string_view searched() const {
    return m_outline->expanded ? std::string_view(m_outline->expanded->text)
                               : std::string_view(m_files[0].log.text());
  }
  const Statement_index &searched_index() const {
    return m_outline->expanded ? *m_outline->expanded->index : *m_plain_index;
  }

  // The file of an outline source (0: the script, k: includes[k - 1]).
  std::optional<size_t> file_of(uint32_t source) const {
    if (source == 0) return 0;
    if (source > m_outline->includes.size()) return std::nullopt;
    const auto it = m_by_path.find(m_outline->includes[source - 1].path);
    if (it == m_by_path.end()) return std::nullopt;
    return it->second;
  }
  // The first include of file `file`.
  std::optional<size_t> include_of(size_t file) const {
    if (file == 0) return std::nullopt;
    for (size_t k = 0; k < m_outline->includes.size(); ++k) {
      if (m_outline->includes[k].path == m_files[file].path) return k;
    }
    return std::nullopt;
  }

  // Where byte `offset` of source `source` is in the searched text.
  std::optional<uint32_t> to_searched(uint32_t source, uint32_t offset) const {
    if (!m_outline->expanded) return source == 0 ? std::optional<uint32_t>(offset) : std::nullopt;
    const auto &e = *m_outline->expanded;
    return unmap_source(e.segments, static_cast<uint32_t>(e.text.size()), source, offset);
  }

  Catalog_view view() const {
    Catalog_view v;
    v.add({std::shared_ptr<const Catalog>(m_outline, &m_outline->catalog), Layer_kind::document, {}});
    return v;
  }

  // Applies one edit's changes.
  void apply(const Changes &changes) {
    for (const auto &[file, edits] : changes) m_files[file].log.apply(edits);
  }

  // The selections of the directives as first read: (file, range) per
  // directive; no file for one that included nothing.
  void record_selections() {
    for (const auto &inc : m_outline->includes) {
      std::optional<size_t> file;
      if (inc.error.empty()) {
        if (const auto it = m_by_path.find(inc.path); it != m_by_path.end()) file = it->second;
      }
      m_selections.push_back({file, inc.selected});
    }
  }

  // Every edit of an included file must lie inside the text its directives
  // select, and each directive must still select the same text: a bound
  // that no longer does is rewritten in the script.
  std::optional<Edit_error> fix_slices(size_t edit) {
    // Each edit of a file inside at least one selection, crossing none.
    for (size_t f = 1; f < m_files.size(); ++f) {
      for (const auto &e : m_files[f].log.edits()) {
        bool inside = false;
        for (const auto &[file, sel] : m_selections) {
          if (file != f) continue;
          const bool crosses = (e.begin < sel.begin && e.end > sel.begin) ||
                               (e.begin < sel.end && e.end > sel.end);
          if (crosses) {
            return error(edit, "edit-outside-slice",
                         "an edit of " + m_files[f].path + " crosses the bound of a SOURCE slice");
          }
          if (sel.begin <= e.begin && e.end <= sel.end) inside = true;
        }
        if (!inside) {
          return error(edit, "edit-outside-slice",
                       "an edit of " + m_files[f].path + " lies outside what its SOURCE directives include");
        }
      }
    }
    const std::string &script = m_files[0].log.text();
    const auto directives = find_source_directives(script);
    if (directives.size() != m_selections.size()) {
      return error(edit, "self-check", "the edits changed the script's SOURCE directives");
    }
    std::vector<Text_edit> bounds;
    for (size_t k = 0; k < directives.size(); ++k) {
      const auto &[file, sel] = m_selections[k];
      if (!file || m_files[*file].log.edits().empty()) continue;
      const auto &d = directives[k];
      const auto &log = m_files[*file].log;
      const auto nb = log.to_current(sel.begin, false);
      const auto ne = log.to_current(sel.end, true);
      const auto sb = nb ? slice_bound_for(log.text(), d.start, false, *nb) : std::nullopt;
      const auto se = ne ? slice_bound_for(log.text(), d.end, true, *ne) : std::nullopt;
      const auto check = sb && se ? slice_source(log.text(), *sb, *se) : Source_slice{};
      if (!sb || !se || !check.error.empty() || check.range.begin != *nb || check.range.end != *ne) {
        return error(edit, "edit-outside-slice",
                     "the slice of SOURCE '" + d.file + "' cannot be written to select the edited text");
      }
      if (*sb == d.start && *se == d.end) continue;
      // The bounds as written in `[ start : end ]`.
      uint32_t p = d.slice.begin + 1;
      auto skip = [&] {
        while (p < d.slice.end && (script[p] == ' ' || script[p] == '\t')) ++p;
      };
      skip();
      const uint32_t start_at = p;
      p += static_cast<uint32_t>(d.start.size());
      while (p < d.slice.end && script[p] != ':') ++p;
      ++p;
      skip();
      const uint32_t end_at = p;
      if (std::string_view(script).substr(start_at, d.start.size()) != d.start ||
          std::string_view(script).substr(end_at, d.end.size()) != d.end) {
        return error(edit, "self-check", "the slice of SOURCE '" + d.file + "' cannot be read");
      }
      if (*sb != d.start) bounds.push_back({start_at, start_at + static_cast<uint32_t>(d.start.size()), *sb});
      if (*se != d.end) bounds.push_back({end_at, end_at + static_cast<uint32_t>(d.end.size()), *se});
    }
    if (!bounds.empty()) m_files[0].log.apply(std::move(bounds));
    return std::nullopt;
  }

 private:
  Parser_pool &m_pool;
  const Edit_options &m_options;
  Parser_cache m_cache;
  std::vector<Session_file> m_files;
  std::map<std::string, size_t> m_by_path;
  std::set<std::string> m_unreadable;
  std::shared_ptr<Outline> m_outline;
  std::shared_ptr<const Statement_index> m_plain_index;
  std::vector<std::pair<std::optional<size_t>, Range>> m_selections;
};

// Looking objects up in the request's schema, then in the part of the
// script that names no schema (msm's snapshot takes both).
class Lookup {
 public:
  Lookup(const Outline &outline, const std::string &schema) {
    for (const auto &s : outline.catalog.schemas()) {
      if (iequals(s.name, schema)) m_schemas.insert(m_schemas.begin(), &s);
      else if (s.name.empty()) m_schemas.push_back(&s);
    }
  }
  // A copy (one or two pointers): `lookup().schemas()` in a range-for
  // would otherwise refer into the destroyed Lookup.
  std::vector<const Schema *> schemas() const { return m_schemas; }

  std::pair<const Schema *, const Table *> table(std::string_view name) const {
    for (const Schema *s : m_schemas) {
      if (const Table *t = s->table(name)) return {s, t};
    }
    return {nullptr, nullptr};
  }
  std::pair<const Schema *, const View *> view(std::string_view name) const {
    for (const Schema *s : m_schemas) {
      if (const View *v = s->view(name)) return {s, v};
    }
    return {nullptr, nullptr};
  }
  std::pair<const Schema *, const Trigger *> trigger(std::string_view name) const {
    for (const Schema *s : m_schemas) {
      if (const Trigger *t = s->trigger(name)) return {s, t};
    }
    return {nullptr, nullptr};
  }
  std::pair<const Schema *, const Event *> event(std::string_view name) const {
    for (const Schema *s : m_schemas) {
      if (const Event *e = s->event(name)) return {s, e};
    }
    return {nullptr, nullptr};
  }
  std::pair<const Schema *, const Sequence *> sequence(std::string_view name) const {
    for (const Schema *s : m_schemas) {
      if (const Sequence *q = s->sequence(name)) return {s, q};
    }
    return {nullptr, nullptr};
  }
  // A table, view or sequence (they share a namespace) of that name.
  bool relation(std::string_view name) const {
    return table(name).second != nullptr || view(name).second != nullptr || sequence(name).second != nullptr;
  }
  // The routines of that name, of `kind` when it is not "".
  std::vector<std::pair<const Schema *, const Routine *>> routines(std::string_view name,
                                                                   std::string_view kind) const {
    std::vector<std::pair<const Schema *, const Routine *>> out;
    for (const Schema *s : m_schemas) {
      for (const auto &r : s->routines) {
        if (iequals(r.name, name) && (kind.empty() || iequals(r.kind, kind))) out.push_back({s, &r});
      }
    }
    return out;
  }

 private:
  std::vector<const Schema *> m_schemas;
};

// The grammar symbols the edits look for in parse trees: those of the
// catalog fold, whose test checks that the grammar has every one.
struct Grammar {
#define SYM(x) int x = grammar::symbol(#x);
#include "catalog_symbols.inc"
#undef SYM
};

const Grammar &gram() {
  static const Grammar g;
  return g;
}

constexpr uint32_t k_npos = Syntax_tree::npos;

uint32_t child_of(const Syntax_tree &t, uint32_t n, int sym) { return n == k_npos ? k_npos : t.child(n, sym); }
uint32_t find_in(const Syntax_tree &t, uint32_t n, int sym) { return n == k_npos ? k_npos : t.find(n, sym); }
std::vector<uint32_t> find_all_in(const Syntax_tree &t, uint32_t n, int sym) {
  return n == k_npos ? std::vector<uint32_t>{} : t.find_all(n, sym);
}
bool has_tokens(const Syntax_tree &t, uint32_t n) { return n != k_npos && t.node(n).token_count > 0; }

// A word of a statement as the subject scanner sees it.
struct Word {
  std::string_view text;
  bool quoted = false;
  bool punct = false;
};

// The first `max` words of `sql`: names, keywords, quoted names, and single
// punctuation characters.  Comments and strings are skipped; a versioned
// comment's text is read as if it were not commented (mysqldump writes
// `/*!50003 CREATE*/ ... TRIGGER`).
std::vector<Word> words_of(std::string_view sql, size_t max) {
  std::vector<Word> out;
  size_t p = 0;
  bool versioned = false;  // inside /*! ... */
  auto blank_or_end = [&](size_t q) { return q >= sql.size() || std::isspace(static_cast<unsigned char>(sql[q])); };
  while (p < sql.size() && out.size() < max) {
    const char c = sql[p];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++p;
    } else if (c == '#' || (c == '-' && sql.substr(p, 2) == "--" && blank_or_end(p + 2))) {
      while (p < sql.size() && sql[p] != '\n') ++p;
    } else if (sql.substr(p, 3) == "/*!") {
      p += 3;
      while (p < sql.size() && std::isdigit(static_cast<unsigned char>(sql[p]))) ++p;
      versioned = true;
    } else if (versioned && sql.substr(p, 2) == "*/") {
      p += 2;
      versioned = false;
    } else if (sql.substr(p, 2) == "/*") {
      const size_t e = sql.find("*/", p + 2);
      p = e == std::string_view::npos ? sql.size() : e + 2;
    } else if (c == '`' || c == '"' || c == '\'') {
      size_t q = p + 1;
      while (q < sql.size()) {
        if (sql[q] == c && q + 1 < sql.size() && sql[q + 1] == c) q += 2;
        else if (sql[q] == c) break;
        else if (sql[q] == '\\' && c == '\'') q += 2;
        else ++q;
      }
      q = std::min(q + 1, sql.size());
      if (c != '\'') out.push_back({sql.substr(p, q - p), true, false});
      p = q;
    } else if (is_word_char(c)) {
      size_t q = p;
      while (q < sql.size() && is_word_char(sql[q])) ++q;
      out.push_back({sql.substr(p, q - p), false, false});
      p = q;
    } else {
      out.push_back({sql.substr(p, 1), false, true});
      ++p;
    }
  }
  return out;
}

// What a CREATE, ALTER or DROP statement is about: the kind of object (the
// keyword: TABLE, VIEW, PROCEDURE, ...), its names (a DROP may list
// several), and for an index the table it is on.
struct Subject {
  std::string verb;
  std::string kind;
  std::vector<std::pair<std::string, std::string>> names;  // (schema, name)
  std::vector<std::string_view> name_words;                // each name's last part, as written in the SQL
  std::string on_schema;
  std::string on_table;
};

std::optional<Subject> subject_of(std::string_view sql) {
  const auto w = words_of(sql, 48);
  if (w.empty() || w[0].quoted || w[0].punct) return std::nullopt;
  Subject s;
  s.verb = upper(w[0].text);
  if (s.verb != "CREATE" && s.verb != "ALTER" && s.verb != "DROP") return std::nullopt;
  static const char *const kinds[] = {"TABLE",   "TABLES", "VIEW",     "PROCEDURE", "FUNCTION", "TRIGGER",
                                      "INDEX",   "EVENT",  "SEQUENCE", "DATABASE",  "SCHEMA",   "USER",
                                      "ROLE",    "SERVER", "PACKAGE",  "REST"};
  size_t i = 1;
  for (; i < w.size() && i < 24; ++i) {
    if (w[i].quoted || w[i].punct) continue;
    const std::string u = upper(w[i].text);
    if (std::any_of(std::begin(kinds), std::end(kinds), [&](const char *k) { return u == k; })) {
      s.kind = u == "TABLES" ? "TABLE" : u;
      break;
    }
  }
  if (s.kind.empty()) return std::nullopt;
  size_t j = i + 1;
  auto is = [&](size_t k, const char *word) { return k < w.size() && !w[k].quoted && iequals(w[k].text, word); };
  if (is(j, "IF")) {
    ++j;
    if (is(j, "NOT")) ++j;
    if (is(j, "EXISTS")) ++j;
  }
  // name [. name]
  auto name = [&](size_t &k) -> std::optional<std::pair<std::string, std::string>> {
    if (k >= w.size() || w[k].punct) return std::nullopt;
    std::string first = unquote_name(w[k].text);
    ++k;
    if (k + 1 < w.size() && w[k].punct && w[k].text == "." && !w[k + 1].punct) {
      std::string second = unquote_name(w[k + 1].text);
      s.name_words.push_back(w[k + 1].text);
      k += 2;
      return std::make_pair(std::move(first), std::move(second));
    }
    s.name_words.push_back(w[k - 1].text);
    return std::make_pair(std::string(), std::move(first));
  };
  while (true) {
    auto n = name(j);
    if (!n) break;
    s.names.push_back(std::move(*n));
    if (s.verb != "DROP" || j >= w.size() || w[j].text != ",") break;
    ++j;
  }
  if (s.names.empty()) return std::nullopt;
  if (s.kind == "INDEX" && is(j, "ON")) {
    ++j;
    if (auto n = name(j)) {
      s.on_schema = n->first;
      s.on_table = n->second;
    }
  }
  return s;
}

bool is_blank(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t'; });
}

// A line that holds only a comment.
bool is_comment_line(std::string_view line) {
  const auto t = trim(line);
  return t.substr(0, 2) == "--" || t.substr(0, 1) == "#" || t.substr(0, 2) == "/*" || t.substr(0, 1) == "*";
}

// The end of the line holding `p`: where its line break starts.
uint32_t line_end(std::string_view text, uint32_t p) {
  while (p < text.size() && text[p] != '\n') ++p;
  if (p < text.size() && p > 0 && text[p - 1] == '\r') --p;
  return p;
}

// Past the line break at `p` (which line_end() gave), if there is one.
uint32_t past_line_break(std::string_view text, uint32_t p) {
  if (p < text.size() && text[p] == '\r') ++p;
  if (p < text.size() && text[p] == '\n') ++p;
  return p;
}

// Whether `r` stands alone on its line, after blanks.
bool own_line(std::string_view text, Range r) {
  const uint32_t lb = line_begin(text, r.begin);
  return is_blank(text.substr(lb, r.begin - lb));
}

class Editor {
 public:
  Editor(Session &s, const Edit_request &request, Edit_result &result)
      : m_s(s), m_request(request), m_result(result) {}

  std::optional<Edit_error> run(size_t i) {
    // The op and its self-check see the names the new keys are given.
    const Schema_edit e = with_key_names(m_request.edits[i]);
    Changes changes;
    std::optional<Edit_error> err;
    switch (e.op) {
      case Edit_op::rename_table: err = rename_table(i, e, changes); break;
      case Edit_op::rename_column: err = rename_column(i, e, changes); break;
      case Edit_op::rename_view: err = rename_view(i, e, changes); break;
      case Edit_op::rename_routine: err = rename_routine(i, e, changes); break;
      case Edit_op::add_table: err = add_table(i, e, changes); break;
      case Edit_op::drop_table: err = drop_table(i, e, changes); break;
      case Edit_op::add_column: err = add_column(i, e, changes); break;
      case Edit_op::modify_column: err = modify_column(i, e, changes); break;
      case Edit_op::drop_column: err = drop_column(i, e, changes); break;
      case Edit_op::set_table_options: err = set_table_options(i, e, changes); break;
      case Edit_op::add_view: err = add_view(i, e, changes); break;
      case Edit_op::drop_view: err = drop_view(i, e, changes); break;
      case Edit_op::add_routine: err = add_routine(i, e, changes); break;
      case Edit_op::drop_routine: err = drop_routine(i, e, changes); break;
      case Edit_op::add_index: err = add_index(i, e, changes); break;
      case Edit_op::drop_index: err = drop_index(i, e, changes); break;
      case Edit_op::set_primary_key: err = set_primary_key(i, e, changes); break;
      case Edit_op::add_foreign_key: err = add_foreign_key(i, e, changes); break;
      case Edit_op::drop_foreign_key: err = drop_foreign_key(i, e, changes); break;
      case Edit_op::add_trigger: err = add_trigger(i, e, changes); break;
      case Edit_op::drop_trigger: err = drop_trigger(i, e, changes); break;
      case Edit_op::rename_trigger: err = rename_trigger(i, e, changes); break;
      case Edit_op::add_event: err = add_event(i, e, changes); break;
      case Edit_op::drop_event: err = drop_event(i, e, changes); break;
      case Edit_op::rename_event: err = rename_event(i, e, changes); break;
      case Edit_op::add_sequence: err = add_sequence(i, e, changes); break;
      case Edit_op::drop_sequence: err = drop_sequence(i, e, changes); break;
      case Edit_op::rename_sequence: err = rename_sequence(i, e, changes); break;
      default:
        return error(i, "unsupported", std::string("this binary does not do ") + to_string(e.op) + " yet");
    }
    if (err) return err;
    if (auto bad = normalize(i, changes)) return bad;
    m_s.apply(changes);
    if (auto slice = m_s.fix_slices(i)) return slice;
    m_s.build();
    return verify(i, e);
  }

 private:
  const Outline &outline() const { return m_s.outline(); }
  Lookup lookup() const { return Lookup(outline(), m_request.schema); }

  // The object a definition's name stands for, resolved as rename does.
  std::optional<Object_id> resolve(const Origin &o, Target_kind expected) {
    const uint32_t name_end = o.name.end > o.name.begin ? o.name.end - 1 : o.name.begin;
    const auto at = m_s.to_searched(o.source, name_end);
    if (!at) return std::nullopt;
    const std::string_view text = m_s.searched();
    const auto &index = m_s.searched_index();
    const auto i = statement_at(index, *at);
    if (!i) return std::nullopt;
    const Statement &st = index.statements()[*i];
    if (st.kind != Statement_kind::sql) return std::nullopt;
    mdbp_parser *p = m_s.cache().get(index.modes()[st.mode].mode, m_s.options().index.version_id, true);
    if (p == nullptr) return std::nullopt;
    const Statement_names names(p, index.text_of(text, st), st.begin);
    if (!names.parsed()) return std::nullopt;
    Catalog_view view = m_s.view();
    std::string schema = schema_in_use(text, index, *i);
    if (schema.empty()) schema = m_s.options().schema;
    view.set_current_schema(schema);
    const auto r = Name_resolver(view, names).resolve_at(*at);
    if (r.id.kind != expected) return std::nullopt;
    return r.id;
  }

  // Every name referring to `id`, by file, each rewritten to `new_name`;
  // `definition` must be among them.
  std::optional<Edit_error> rewrite_references(size_t edit, const Object_id &id,
                                               const Origin &definition, const std::string &new_name,
                                               Changes &changes) {
    Reference_options ro;
    ro.version_id = m_s.options().index.version_id;
    ro.initial_schema = m_s.options().schema;
    const Catalog_view view = m_s.view();
    const auto refs = find_references(m_s.searched(), m_s.searched_index(), id, 0, view, m_s.pool(), ro);
    std::set<std::tuple<size_t, uint32_t, uint32_t>> seen;
    bool definition_found = false;
    const auto def_file = m_s.file_of(definition.source);
    for (const auto &r : refs) {
      const auto m = outline().map(r.range);
      const auto file = m_s.file_of(m.source);
      if (!file) continue;
      if (!seen.insert({*file, m.range.begin, m.range.end}).second) continue;
      const std::string &text = m_s.files()[*file].log.text();
      if (m.range.end > text.size()) continue;
      const std::string_view token = std::string_view(text).substr(m.range.begin, m.range.end - m.range.begin);
      if (!iequals(unquote_name(token), id.name)) {
        return error(edit, "self-check",
                     "a reference to `" + id.name + "` does not spell it: " + std::string(token));
      }
      if (def_file && *file == *def_file && m.range.begin <= definition.name.end &&
          m.range.end >= definition.name.begin) {
        definition_found = true;
      }
      changes[*file].push_back({m.range.begin, m.range.end, spell_like(token, new_name)});
    }
    if (!definition_found) {
      return error(edit, "self-check", "the definition of `" + id.name + "` was not found among its references");
    }
    return std::nullopt;
  }

  // A note at byte `at` of the original text of file `file`.
  void note_at(size_t edit, const char *kind, size_t file, uint32_t at, std::string message) {
    Edit_note n;
    n.edit = edit;
    n.kind = kind;
    n.file = file;
    n.include = m_s.include_of(file);
    n.at = at;
    n.message = std::move(message);
    m_result.notes.push_back(std::move(n));
  }

  // The same, at byte `offset` of the current text of outline source `source`.
  void note(size_t edit, const char *kind, uint32_t source, uint32_t offset, std::string message) {
    const auto file = m_s.file_of(source);
    if (!file) return;
    note_at(edit, kind, *file, m_s.files()[*file].log.to_original(offset), std::move(message));
  }

  // A string literal holding every one of `words` as a word, outside a
  // COMMENT: dynamic SQL the rename does not rewrite.  A literal of a file
  // included twice is noted once.
  void string_notes(size_t edit, const std::vector<std::string> &words, const std::string &what) {
    const std::string_view text = m_s.searched();
    const auto &index = m_s.searched_index();
    std::set<std::pair<size_t, uint32_t>> seen;  // (file, offset in its original text)
    for (const auto &st : index.statements()) {
      if (st.kind != Statement_kind::sql) continue;
      const std::string_view sql = index.text_of(text, st);
      if (!std::all_of(words.begin(), words.end(), [&](const std::string &w) { return icontains(sql, w); })) {
        continue;
      }
      mdbp_parser *p = m_s.cache().get(index.modes()[st.mode].mode, m_s.options().index.version_id, false);
      mdbp_result r;
      if (p == nullptr || mdbp_lex_sql(p, sql.data(), sql.size(), &r) != MDBP_OK || r.tokens == nullptr) continue;
      for (size_t k = 0; k < r.ntokens; ++k) {
        const auto &t = r.tokens[k];
        if (t.byte_start < 0 || t.byte_end <= t.byte_start || t.name == nullptr) continue;
        if (std::strcmp(t.name, "TEXT_STRING") != 0 && std::strcmp(t.name, "NCHAR_STRING") != 0) continue;
        auto is = [&](size_t j, const char *name) {
          return r.tokens[j].name != nullptr && std::strcmp(r.tokens[j].name, name) == 0;
        };
        if (k > 0 && is(k - 1, "COMMENT_SYM")) continue;
        if (k > 1 && is(k - 2, "COMMENT_SYM")) continue;  // COMMENT = '...'
        const auto value = sql.substr(static_cast<size_t>(t.byte_start),
                                      static_cast<size_t>(t.byte_end - t.byte_start));
        if (!std::all_of(words.begin(), words.end(), [&](const std::string &w) { return has_word(value, w); })) {
          continue;
        }
        const auto m = outline().map({st.begin + static_cast<uint32_t>(t.byte_start),
                                      st.begin + static_cast<uint32_t>(t.byte_end)});
        const auto file = m_s.file_of(m.source);
        if (!file) continue;
        const auto at = m_s.files()[*file].log.to_original(m.range.begin);
        if (!seen.insert({*file, at}).second) continue;
        note_at(edit, "name-in-string", *file, at,
                what + " is named in a string literal, which was not changed (dynamic SQL?)");
      }
    }
  }

  // REST objects of `kind` (view for a table or view, procedure, function)
  // naming `name`: their SQL is not rewritten.
  void rest_notes(size_t edit, const std::string &name, std::string_view kind, const std::string &what) {
    for (const auto &svc : outline().catalog.rest_services()) {
      for (const auto &sch : svc.schemas) {
        for (const auto &o : sch.objects) {
          if (!iequals(o.kind, kind)) continue;
          if (!iequals(last_name_part(o.object), name)) continue;
          note(edit, "rest-reference", o.origin.source, o.origin.statement.begin,
               "the REST object " + o.path + " names " + what + "; its REST SQL was not changed");
        }
      }
    }
  }

  // The name is free among tables, views and sequences (they share a
  // namespace).
  std::optional<Edit_error> table_name_free(size_t edit, const std::string &old_name,
                                            const std::string &new_name) {
    if (iequals(old_name, new_name)) return std::nullopt;
    if (lookup().relation(new_name)) return error(edit, "name-taken", "a table, view or sequence `" + new_name + "` exists");
    return std::nullopt;
  }

  // The migration's statements, each with what it does.
  using Act = Migration_action;
  using Obj = Migration_object;
  static Obj routine_object(const std::string &kind) { return kind == "function" ? Obj::function : Obj::procedure; }
  // `sql` doing `a` to object `o` named `name` (a table, view or routine).
  void migrate(size_t edit, std::string sql, Act a, Obj o, std::string name, std::string new_name = {}) {
    m_result.migration.push_back({edit, std::move(sql), a, o, std::move(name), {}, std::move(new_name)});
  }
  // DROP TABLE|VIEW|PROCEDURE|FUNCTION `name`.
  void migrate_drop(size_t edit, Obj o, const std::string &name) {
    migrate(edit, "DROP " + upper(to_string(o)) + " " + backtick_quote(name), Act::drop, o, name);
  }
  // ALTER TABLE `table` <clause>, doing `a` to `table`'s member `o` named
  // `name` (PRIMARY for the primary key).
  void migrate_member(size_t edit, const std::string &table, const std::string &clause, Act a, Obj o,
                      std::string name, std::string new_name = {}) {
    if (o == Obj::primary_key) name = "PRIMARY";
    m_result.migration.push_back({edit, "ALTER TABLE " + backtick_quote(table) + " " + clause, a, o, std::move(name),
                                  table, std::move(new_name)});
  }
  void migrate_drop_foreign_key(size_t edit, const std::string &table, const std::string &name) {
    migrate_member(edit, table, "DROP FOREIGN KEY " + backtick_quote(name), Act::drop, Obj::foreign_key, name);
  }
  void migrate_add_foreign_key(size_t edit, const std::string &table, const Foreign_key &fk) {
    migrate_member(edit, table, "ADD " + edit_text::foreign_key_sql(fk, {}), Act::add, Obj::foreign_key, fk.name);
  }

  std::optional<Edit_error> rename_table(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    return rename_relation(i, e, "table", table->name, table->origin, Target_kind::table, Obj::table, changes);
  }

  std::optional<Edit_error> rename_view(size_t i, const Schema_edit &e, Changes &changes) {
    const View *view = lookup().view(e.view).second;
    if (view == nullptr) return error(i, "unknown-object", "there is no view `" + e.view + "`");
    return rename_relation(i, e, "view", view->name, view->origin, Target_kind::view, Obj::view, changes);
  }

  // A table, a view or a sequence (`object`): they share a namespace and
  // RENAME TABLE.  A sequence's name resolves as a table's.
  std::optional<Edit_error> rename_relation(size_t i, const Schema_edit &e, const std::string &noun,
                                            const std::string &name, const Origin &origin,
                                            Target_kind kind, Obj object, Changes &changes) {
    if (auto err = table_name_free(i, name, e.new_name)) return err;
    const auto id = resolve(origin, kind);
    if (!id) return error(i, "self-check", "the definition of " + noun + " `" + name + "` cannot be resolved");
    m_old_name = name;
    const std::string what = noun + " `" + name + "`";
    string_notes(i, {name}, what);
    rest_notes(i, name, "view", what);
    if (auto err = rewrite_references(i, *id, origin, e.new_name, changes)) return err;
    // The same name again changes nothing: RENAME TABLE t TO t fails on the
    // server (the table exists).
    if (name != e.new_name) {
      migrate(i, "RENAME TABLE " + backtick_quote(name) + " TO " + backtick_quote(e.new_name), Act::rename, object,
              name, e.new_name);
    }
    return std::nullopt;
  }

  std::optional<Edit_error> rename_column(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Column *column = table->column(e.column);
    if (column == nullptr) {
      return error(i, "unknown-object", "table `" + table->name + "` has no column `" + e.column + "`");
    }
    if (!iequals(column->name, e.new_name) && table->column(e.new_name) != nullptr) {
      return error(i, "name-taken", "table `" + table->name + "` has a column `" + e.new_name + "`");
    }
    const auto id = resolve(column->origin, Target_kind::column);
    if (!id) return error(i, "self-check", "the definition of column `" + column->name + "` cannot be resolved");
    const std::string old_name = column->name;
    string_notes(i, {old_name, table->name}, "column `" + table->name + "`.`" + old_name + "`");
    if (auto err = rewrite_references(i, *id, column->origin, e.new_name, changes)) return err;
    if (old_name != e.new_name) {
      migrate_member(i, table->name, "RENAME COLUMN " + backtick_quote(old_name) + " TO " + backtick_quote(e.new_name),
                     Act::rename, Obj::column, old_name, e.new_name);
    }
    m_old_name = old_name;
    return std::nullopt;
  }

  std::optional<Edit_error> rename_routine(size_t i, const Schema_edit &e, Changes &changes) {
    const auto l = lookup();
    const auto found = l.routines(e.routine, e.routine_kind);
    if (found.empty()) return error(i, "unknown-object", "there is no routine `" + e.routine + "`");
    if (found.size() > 1) {
      return error(i, "bad-request", "`" + e.routine + "` names a procedure and a function: give \"kind\"");
    }
    const Routine *routine = found[0].second;
    // A procedure and a function may share a name: only one of its own kind
    // takes it.
    if (!iequals(routine->name, e.new_name) && !l.routines(e.new_name, routine->kind).empty()) {
      return error(i, "name-taken", "a " + routine->kind + " `" + e.new_name + "` exists");
    }
    const auto id = resolve(routine->origin, Target_kind::routine);
    if (!id) return error(i, "self-check", "the definition of `" + routine->name + "` cannot be resolved");
    const std::string old_name = routine->name;
    string_notes(i, {old_name}, routine->kind + " `" + old_name + "`");
    rest_notes(i, old_name, routine->kind, routine->kind + " `" + old_name + "`");
    if (auto err = rewrite_references(i, *id, routine->origin, e.new_name, changes)) return err;
    m_old_name = old_name;
    m_routine_kind = routine->kind;
    return std::nullopt;
  }

  // ---------------------------------------------------------------------
  // Building blocks of the ops that add and remove text.  Positions are in
  // the searched text (the expanded script); change() maps them back.

  std::string_view text() const { return m_s.searched(); }
  const Statement_index &index() const { return m_s.searched_index(); }
  const Statement &statement(size_t k) const { return index().statements()[k]; }

  // The searched statement an origin's statement starts in.
  std::optional<size_t> statement_of(const Origin &o) const {
    const auto at = m_s.to_searched(o.source, o.statement.begin);
    if (!at) return std::nullopt;
    const size_t k = index().find(*at);
    if (k >= index().statements().size()) return std::nullopt;
    return k;
  }

  // The searched range of an origin's range in its file.
  std::optional<Range> searched_range(uint32_t source, Range r) const {
    const auto b = m_s.to_searched(source, r.begin);
    if (!b) return std::nullopt;
    return Range{*b, *b + (r.end - r.begin)};
  }

  // Replaces the searched range [b, e) with `with`, in the file it comes
  // from; false when the range does not lie in one file's text.
  bool change(Changes &changes, uint32_t b, uint32_t e, std::string with) {
    if (!mappable(b, e)) return false;
    const auto m = outline().map({b, e});
    changes[*m_s.file_of(m.source)].push_back({m.range.begin, m.range.end, std::move(with)});
    return true;
  }

  // The file and current offset a searched position is at.
  std::optional<std::pair<size_t, uint32_t>> file_at(uint32_t p) const {
    const auto m = outline().map({p, p});
    const auto file = m_s.file_of(m.source);
    if (!file) return std::nullopt;
    return std::make_pair(*file, m.range.begin);
  }

  // The parse tree of searched statement `k`, with its verb node (the
  // create, alter or drop node).
  struct Parsed {
    Syntax_tree tree;
    uint32_t verb = k_npos;
  };
  std::optional<Parsed> parse(size_t k) {
    const Statement &st = statement(k);
    if (st.kind != Statement_kind::sql) return std::nullopt;
    mdbp_parser *p = m_s.cache().get(index().modes()[st.mode].mode, m_s.options().index.version_id, true);
    if (p == nullptr) return std::nullopt;
    const std::string_view sql = index().text_of(text(), st);
    mdbp_result r;
    if (mdbp_parse_sql(p, sql.data(), sql.size(), &r) != MDBP_OK || !r.accepted) return std::nullopt;
    Parsed out{Syntax_tree::build(p, r, st.begin), k_npos};
    if (out.tree.empty() || out.tree.roots().size() != 1) return std::nullopt;
    const uint32_t verb = find_in(out.tree, out.tree.roots()[0], gram().verb_clause);
    if (verb == k_npos || out.tree.children(verb).empty()) return std::nullopt;
    out.verb = out.tree.children(verb)[0];
    return out;
  }

  std::string name_of(const Syntax_tree &t, uint32_t n) const {
    return n == k_npos ? "" : unquote_name(t.text(text(), n));
  }

  // Why `sql` does not parse, or nullopt when it does.
  std::optional<std::string> parse_problem(std::string_view sql) {
    mdbp_parser *p = m_s.cache().get(m_s.options().index.sql_mode, m_s.options().index.version_id, false);
    if (p == nullptr) return std::nullopt;
    mdbp_result r;
    const auto status = mdbp_parse_sql(p, sql.data(), sql.size(), &r);
    if (status == MDBP_OK && r.accepted) return std::nullopt;
    std::string near;
    if (r.tokens != nullptr && r.error_token_index < r.ntokens && r.tokens[r.error_token_index].byte_start >= 0) {
      near = std::string(sql.substr(static_cast<size_t>(r.tokens[r.error_token_index].byte_start), 40));
      near = std::string(trim(near.substr(0, near.find('\n'))));
    }
    return near.empty() ? std::string("a syntax error at its end") : "a syntax error near '" + near + "'";
  }

  // How the file of searched statement `k` writes SQL: the case of the
  // statement's first keyword, the quoting of the name at `name_at`, the
  // file's line breaks.  The script's line breaks without a statement.
  edit_text::Style style_at(std::optional<size_t> k, std::optional<uint32_t> name_at) const {
    edit_text::Style st;
    if (!k) {
      st.eol = std::string(eol_of(m_s.files()[0].log.text()));
      st.backslash_escapes = !icontains(m_s.options().index.sql_mode, "NO_BACKSLASH_ESCAPES");
      return st;
    }
    st.backslash_escapes = (index().modes()[statement(*k).mode].bits & k_no_backslash_escapes) == 0;
    const std::string_view sql = index().text_of(text(), statement(*k));
    size_t p = 0;
    while (p < sql.size() && std::isalpha(static_cast<unsigned char>(sql[p]))) ++p;
    if (std::any_of(sql.begin(), sql.begin() + static_cast<std::ptrdiff_t>(p),
                    [](char c) { return std::islower(static_cast<unsigned char>(c)); })) {
      st.upper = false;
    }
    if (name_at && *name_at < text().size()) st.backticks = text()[*name_at] == '`';
    if (const auto f = file_at(statement(*k).begin)) st.eol = std::string(eol_of(m_s.files()[f->first].log.text()));
    return st;
  }
  edit_text::Style style_of(const Origin &o) const {
    const auto name = m_s.to_searched(o.source, o.name.begin);
    return style_at(statement_of(o), name);
  }

  // `sql` (one statement, no delimiter) laid out by the formatter in the
  // file's style; as it is when the formatter leaves it.  `compound`: a
  // routine, whose body holds semicolons.
  std::string laid_out(const std::string &sql, const edit_text::Style &st, bool compound) {
    Index_options io = m_s.options().index;
    io.delimiter = compound ? "$$" : ";";
    // The formatter breaks lines as the text does: give it the file's.
    const std::string whole = sql + io.delimiter + st.eol;
    const auto idx = Statement_index::build(whole, io);
    std::string out = sql;
    if (idx.statements().size() == 1) {
      Format_options fo = m_s.options().format;
      fo.upper_case_keywords = st.upper;
      const auto edits =
          format_sql(whole, idx, 0, static_cast<uint32_t>(whole.size()), m_s.cache(), fo);
      if (!edits.empty()) {
        std::string formatted = apply_edits(whole, edits);
        std::string_view f = trim(formatted);
        if (f.size() >= io.delimiter.size() && f.substr(f.size() - io.delimiter.size()) == io.delimiter) {
          f.remove_suffix(io.delimiter.size());
        }
        out = std::string(trim(f));
      }
    }
    return out;
  }

  // Where a statement goes after searched statement `k`: at the end of the
  // line its delimiter ends, after as many blank lines as stand above `k`
  // (a banner's comment lines not counted), ending with the delimiter in
  // force there.
  struct Spot {
    uint32_t at = 0;
    std::string lead;       // line breaks before the new statement
    std::string trail;      // and after it: at the start of the text
    std::string delimiter;  // the delimiter in force at `at`
  };
  // `gap_from`: the statement whose blank lines above count, when the
  // object at `k` starts before it (a DROP ... IF EXISTS before its CREATE).
  Spot after_statement(size_t k, const std::string &eol, std::optional<size_t> gap_from = std::nullopt) const {
    const std::string_view t = text();
    const Statement &st = statement(k);
    Spot s;
    uint32_t q = st.end;
    while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) ++q;
    if (q < t.size() && (t.substr(q, 2) == "--" || t[q] == '#')) {
      s.at = line_end(t, q);
    } else if (q >= t.size() || t[q] == '\n' || t[q] == '\r') {
      s.at = q;
    } else {
      s.at = st.end;
    }
    // Counted in the statement's own file: what is above an included
    // file's first statement belongs to another.
    int blanks = 0;
    if (const auto f = file_at(statement(gap_from.value_or(k)).begin)) {
      const std::string_view ft = m_s.files()[f->first].log.text();
      uint32_t lb = line_begin(ft, f->second);
      while (lb > 0) {
        const uint32_t pb = line_begin(ft, lb - 1);
        const std::string_view line = ft.substr(pb, lb - 1 - pb);
        // A comment block is the statement's banner, unless it is an MSM
        // section's: the blank lines above that are the section's.
        if (trim(line).empty()) ++blanks;
        else if (!is_comment_line(line) || line.substr(0, k_msm_marker.size()) == k_msm_marker) break;
        lb = pb;
      }
    }
    blanks = std::min(blanks, 3);
    for (int i = 0; i <= blanks; ++i) s.lead += eol;
    s.delimiter = index().delimiters()[st.active_delimiter];
    if (s.delimiter.empty()) s.delimiter = ";";
    return s;
  }

  // Inserts statement `sql` after searched statement `k`, or at the end of
  // the script when there is none; `wrap`: in a DELIMITER block when the
  // delimiter there is ";".  The file and current offset it went to.
  std::optional<std::pair<size_t, uint32_t>> insert_statement(Changes &changes, std::optional<size_t> k,
                                                               const std::string &sql, const edit_text::Style &st,
                                                               bool wrap) {
    return insert_at(changes, k ? std::optional<Spot>(after_statement(*k, st.eol)) : std::nullopt, sql, st, wrap);
  }

  // The same at `spot` (the end of the script when there is none), with
  // statement `before` (a DROP ... IF EXISTS) right before `sql`.
  std::optional<std::pair<size_t, uint32_t>> insert_at(Changes &changes, const std::optional<Spot> &spot,
                                                       const std::string &sql, const edit_text::Style &st, bool wrap,
                                                       const std::string &before = "") {
    auto statements = [&](const std::string &d) { return (before.empty() ? "" : before + d + st.eol) + sql + d; };
    auto block = [&](const std::string &restore) {
      std::string d = "$$";
      for (const auto &x : index().delimiters()) {
        if (!x.empty() && x != ";") {
          d = x;
          break;
        }
      }
      return st.kw("DELIMITER") + " " + d + st.eol + statements(d) + st.eol + st.kw("DELIMITER") + " " + restore;
    };
    if (spot) {
      const Spot &s = *spot;
      const bool wrapped = wrap && s.delimiter == ";";
      const std::string body = wrapped ? block(s.delimiter) : statements(s.delimiter);
      const auto at = file_at(s.at);
      if (!at || !change(changes, s.at, s.at, s.lead + body + s.trail)) return std::nullopt;
      return at;
    }
    const std::string &script = m_s.files()[0].log.text();
    std::string lead;
    if (!script.empty()) lead = script.back() == '\n' ? st.eol : st.eol + st.eol;
    std::string d = index().final_delimiter();
    if (d.empty()) d = ";";
    const bool wrapped = wrap && d == ";";
    const auto at = static_cast<uint32_t>(script.size());
    changes[0].push_back({at, at, lead + (wrapped ? block(d) : statements(d)) + st.eol});
    return std::make_pair(size_t{0}, at);
  }

  // Whether the searched range [b, e) lies in one file's text, as change()
  // needs it.
  bool mappable(uint32_t b, uint32_t e) const {
    const auto m = outline().map({b, e});
    const auto file = m_s.file_of(m.source);
    if (!file || m.range.end - m.range.begin != e - b) return false;
    const std::string &ft = m_s.files()[*file].log.text();
    return m.range.end <= ft.size() && std::string_view(ft).substr(m.range.begin, e - b) == text().substr(b, e - b);
  }

  // Removes searched statement `k`: whole lines when it has its lines to
  // itself, with the comment lines right above it when one of them names
  // `banner`, and the run of blank lines above it, which is what adding a
  // statement puts before it; the run below instead when the text (or the
  // included file's slice) ends above it.  The searched range removed.
  std::optional<Range> remove_statement(Changes &changes, size_t k, const std::string &banner) {
    const std::string_view t = text();
    const Statement &st = statement(k);
    uint32_t q = st.end;
    while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) ++q;
    if (q < t.size() && (t.substr(q, 2) == "--" || t[q] == '#')) q = line_end(t, q);
    const bool to_line_end = q >= t.size() || t[q] == '\n' || t[q] == '\r';
    const uint32_t lb = line_begin(t, st.begin);
    if (to_line_end && is_blank(t.substr(lb, st.begin - lb)) && mappable(lb, past_line_break(t, q))) {
      const uint32_t start = lb;
      const uint32_t end = past_line_break(t, q);
      // A banner: the comment lines right above, when one names the object.
      uint32_t c = start;
      bool named = false;
      while (c > 0) {
        const uint32_t pb = line_begin(t, c - 1);
        const std::string_view line = t.substr(pb, c - 1 - pb);
        if (trim(line).empty() || !is_comment_line(line) || !mappable(pb, end)) break;
        if (has_word(line, banner)) named = true;
        c = pb;
      }
      const uint32_t top = named ? c : start;
      // The blank lines above; `open_above`: they end at the start of the
      // text or of the file's slice, not at a line with something on it.
      uint32_t above = top;
      bool open_above = true;
      while (above > 0) {
        const uint32_t pb = line_begin(t, above - 1);
        if (!trim(t.substr(pb, above - 1 - pb)).empty()) {
          open_above = false;
          break;
        }
        if (!mappable(pb, end)) break;
        above = pb;
      }
      uint32_t below = end;
      if (above == top && open_above) {
        while (below < t.size()) {
          const uint32_t le = line_end(t, below);
          const uint32_t next = past_line_break(t, le);
          if (!trim(t.substr(below, le - below)).empty() || next == below || !mappable(top, next)) break;
          below = next;
        }
      }
      if (change(changes, above, below, "")) return Range{above, below};
    }
    if (change(changes, st.begin, st.end, "")) return Range{st.begin, st.end};
    return std::nullopt;
  }

  // Inserts `item` into a comma-separated list after item `after` (none:
  // first), laid out as the list is: on a line of its own when its
  // neighbour is.
  bool insert_item(Changes &changes, const std::vector<Range> &items, std::optional<size_t> after,
                   const std::string &item, const std::string &eol) {
    const std::string_view t = text();
    const size_t next = after ? *after + 1 : 0;
    if (next < items.size()) {
      const Range n = items[next];
      if (own_line(t, n)) {
        const uint32_t lb = line_begin(t, n.begin);
        return change(changes, lb, lb, std::string(indentation(t, n.begin)) + item + "," + eol);
      }
      return change(changes, n.begin, n.begin, item + ", ");
    }
    if (!after) return false;
    const Range p = items[*after];
    if (!own_line(t, p)) return change(changes, p.end, p.end, ", " + item);
    const std::string indent(indentation(t, p.begin));
    const uint32_t le = line_end(t, p.end);
    const std::string_view rest = trim(t.substr(p.end, le - p.end));
    if (rest.empty()) return change(changes, p.end, p.end, "," + eol + indent + item);
    if (rest.substr(0, 2) == "--" || rest.substr(0, 1) == "#") {
      return change(changes, p.end, p.end, ",") && change(changes, le, le, eol + indent + item);
    }
    return change(changes, p.end, p.end, "," + eol + indent + item);
  }

  // Removes the items `gone` of a comma-separated list, with their commas;
  // not all of them.  The searched ranges removed go to `removed`.
  bool remove_items(Changes &changes, const std::vector<Range> &items, const std::set<size_t> &gone,
                    std::vector<Range> *removed) {
    const std::string_view t = text();
    for (size_t i = 0; i < items.size();) {
      if (gone.count(i) == 0) {
        ++i;
        continue;
      }
      size_t j = i;
      while (j + 1 < items.size() && gone.count(j + 1) != 0) ++j;
      uint32_t b, e;
      if (j + 1 < items.size()) {
        b = items[i].begin;
        e = items[j + 1].begin;
        if (own_line(t, items[i]) && own_line(t, items[j + 1])) {
          b = line_begin(t, b);
          e = line_begin(t, e);
        }
      } else {
        if (i == 0) return false;
        b = items[i - 1].end;
        e = items[j].end;
      }
      if (!change(changes, b, e, "")) return false;
      removed->push_back({b, e});
      i = j + 1;
    }
    return true;
  }

  // The searched statements whose subject is, alone, the object `name` of
  // keyword `kind` (TABLE, VIEW, PROCEDURE, ...) in schema `schema`, or
  // with `on_table`, an index on table `name`.
  std::vector<size_t> statements_about(const std::string &kind, const std::string &name, const std::string &schema,
                                       bool on_table = false) const {
    std::vector<size_t> out;
    const auto &sts = index().statements();
    for (size_t k = 0; k < sts.size(); ++k) {
      if (sts[k].kind != Statement_kind::sql) continue;
      const std::string_view sql = index().text_of(text(), sts[k]);
      if (!icontains(sql, name)) continue;
      const auto subject = subject_of(sql);
      if (!subject || subject->kind != kind) continue;
      std::string sch;
      if (on_table) {
        if (subject->on_table.empty() || !iequals(subject->on_table, name)) continue;
        sch = subject->on_schema;
      } else {
        if (subject->names.size() != 1 || !iequals(subject->names[0].second, name)) continue;
        sch = subject->names[0].first;
      }
      if (sch.empty()) {
        sch = schema_in_use(text(), index(), k);
        if (sch.empty()) sch = m_s.options().schema;
      }
      if (!iequals(sch, schema)) continue;
      out.push_back(k);
    }
    return out;
  }

  // The names referring to `id` in the searched text.
  std::vector<Reference> references_in_text(const Object_id &id) const {
    Reference_options ro;
    ro.version_id = m_s.options().index.version_id;
    ro.initial_schema = m_s.options().schema;
    return find_references(text(), index(), id, 0, m_s.view(), m_s.pool(), ro);
  }

  // A note on every statement that still refers to `id` outside the
  // searched ranges `removed`.
  void still_referenced(size_t edit, const std::optional<Object_id> &id, const std::vector<Range> &removed,
                        const std::string &what) {
    if (!id) return;
    const auto refs = references_in_text(*id);
    std::set<size_t> seen;
    for (const auto &r : refs) {
      if (std::any_of(removed.begin(), removed.end(),
                      [&](const Range &x) { return x.begin <= r.range.begin && r.range.begin < x.end; })) {
        continue;
      }
      const size_t k = index().find(r.range.begin);
      if (k >= index().statements().size() || !seen.insert(k).second) continue;
      const auto m = outline().map({statement(k).begin, statement(k).begin});
      note(edit, "still-referenced", m.source, m.range.begin,
           "a statement still refers to " + what + ", which the edit drops");
    }
  }

  // The schema new objects go to: the request's, else the part of the
  // script that names none.
  const Schema *home() const {
    const auto l = lookup();
    return l.schemas().empty() ? nullptr : l.schemas()[0];
  }

  // Where a new object goes after: the statement of an existing one, and
  // its origin, whose name's quoting the new text follows.
  // The kinds of object a new one is placed among.
  enum class Kind : uint8_t { table, view, routine, trigger, event, sequence };
  struct Anchor {
    std::optional<size_t> k;
    const Origin *origin = nullptr;
    std::string name;
    std::optional<Kind> kind;
  };
  // `f` called with schema `h`'s objects of kind `kind`.
  template <typename F>
  static auto with_objects(const Schema &h, Kind kind, F f) {
    switch (kind) {
      case Kind::view: return f(h.views);
      case Kind::routine: return f(h.routines);
      case Kind::trigger: return f(h.triggers);
      case Kind::event: return f(h.events);
      case Kind::sequence: return f(h.sequences);
      case Kind::table: break;
    }
    return f(h.tables);
  }
  template <typename T>
  static constexpr Kind kind_of() {
    if constexpr (std::is_same_v<T, Table>) return Kind::table;
    else if constexpr (std::is_same_v<T, View>) return Kind::view;
    else if constexpr (std::is_same_v<T, Routine>) return Kind::routine;
    else if constexpr (std::is_same_v<T, Trigger>) return Kind::trigger;
    else if constexpr (std::is_same_v<T, Event>) return Kind::event;
    else return Kind::sequence;
  }
  // The object of `objects` (a schema's tables, views, routines, triggers or
  // events) placed last in the searched text, within searched range
  // `within` when given.
  template <typename T>
  Anchor last_of(const std::vector<T> &objects, std::optional<Range> within = std::nullopt) const {
    Anchor best;
    for (const auto &o : objects) {
      const auto k = statement_of(o.origin);
      if (!k || (within && (statement(*k).begin < within->begin || statement(*k).begin >= within->end))) continue;
      if (!best.k || *k > *best.k) best = {k, &o.origin, o.name, kind_of<T>()};
    }
    return best;
  }

  // The start of an MSM section's marker line: `-- MSM Section 150: ...`.
  static constexpr std::string_view k_msm_marker = "-- MSM Section ";
  // An MSM script's section `id`: from its marker (a line `-- MSM Section
  // 150: ...`) to the next one, in the searched text; nullopt when the
  // script has no such marker (it is not an MSM script).
  std::optional<Range> msm_section(int id) const {
    const std::string_view t = text();
    const std::string_view marker = k_msm_marker;
    std::optional<Range> out;
    for (size_t p = t.find(marker); p != std::string_view::npos; p = t.find(marker, p + 1)) {
      if (p != line_begin(t, static_cast<uint32_t>(p))) continue;
      size_t q = p + marker.size();
      int n = 0;
      while (q < t.size() && std::isdigit(static_cast<unsigned char>(t[q]))) n = n * 10 + (t[q++] - '0');
      // The section ends where the next one's banner starts: at the comment
      // line right above its marker (the banner's top border), if any.
      if (out && out->end == t.size()) {
        const auto e = static_cast<uint32_t>(p);  // after out->begin: a later marker
        const uint32_t above = line_begin(t, e - 1);
        out->end = above > out->begin && is_comment_line(t.substr(above, e - 1 - above)) ? above : e;
      }
      if (n == id && !out) out = Range{static_cast<uint32_t>(p), static_cast<uint32_t>(t.size())};
    }
    return out;
  }

  // Where a statement goes at the end of a section: after its last
  // statement, else after the comment block of its marker.
  Spot end_of_section(Range r, const std::string &eol) const {
    std::optional<size_t> last;
    std::optional<size_t> before;  // the last statement above the section
    for (size_t k = 0; k < index().statements().size(); ++k) {
      const Statement &st = statement(k);
      if (st.kind != Statement_kind::sql) continue;
      if (st.begin < r.begin) before = k;
      else if (st.begin < r.end) last = k;
    }
    if (last) return after_create(*last, eol);
    const std::string_view t = text();
    uint32_t p = r.begin;
    while (p < r.end) {
      const uint32_t le = line_end(t, p);
      const uint32_t next = past_line_break(t, le);
      if (next == p || !is_comment_line(t.substr(p, le - p))) break;
      p = next;
    }
    Spot s;
    s.at = p > r.begin ? line_end(t, p - 1) : r.begin;
    if (s.at == 0) {
      // Nothing above: the statement starts the text, a blank line after it.
      s.trail = eol + eol;
    } else {
      s.lead = eol + eol;
    }
    s.delimiter = before ? index().delimiters()[statement(*before).active_delimiter] : ";";
    if (s.delimiter.empty()) s.delimiter = ";";
    return s;
  }

  // How the script writes the object at statement `k`: CREATE OR REPLACE,
  // or with a DROP ... IF EXISTS of it right before (`drop`, its statement).
  struct Form {
    bool or_replace = false;
    std::optional<size_t> drop;
  };
  Form form_at(size_t k, const std::string &name) const {
    Form f;
    const auto w = words_of(index().text_of(text(), statement(k)), 3);
    f.or_replace = w.size() == 3 && iequals(w[1].text, "OR") && iequals(w[2].text, "REPLACE");
    for (size_t j = k; j-- > 0;) {
      if (statement(j).kind != Statement_kind::sql) continue;
      const auto subject = subject_of(index().text_of(text(), statement(j)));
      if (subject && subject->verb == "DROP" && subject->names.size() == 1 && iequals(subject->names[0].second, name)) {
        f.drop = j;
      }
      break;
    }
    return f;
  }
  // Where a statement goes after the object statement `k` creates: the
  // blank lines above it count from its DROP ... IF EXISTS, if any.
  Spot after_create(size_t k, const std::string &eol) const {
    std::optional<size_t> gap_from;
    const auto subject = subject_of(index().text_of(text(), statement(k)));
    if (subject && subject->verb == "CREATE" && subject->names.size() == 1) {
      gap_from = form_at(k, subject->names[0].second).drop;
    }
    return after_statement(k, eol, gap_from);
  }
  // Where a new object goes: after `anchor`, else at `spot`, else at the
  // end of the script.  `msm`: the script has MSM sections.
  struct Place {
    Anchor anchor;
    std::optional<Spot> spot;
    bool msm = false;
  };
  // After the home schema's last object of the first of `kinds` that has
  // one; in an MSM script only within section `section`, else at that
  // section's end.  Never at or after the first object of `before` (its
  // DROP ... IF EXISTS included), and never before the CREATE TABLE of
  // `after_table`, which the new object depends on.
  Place place_for(int section, std::initializer_list<Kind> kinds, std::initializer_list<Kind> before = {},
              const Table *after_table = nullptr) const {
    Place p;
    const auto r = msm_section(section);
    p.msm = r.has_value();
    Range within = r.value_or(Range{0, static_cast<uint32_t>(text().size())});
    bool cut = false;  // an object of `before` ends the range
    const Schema *h = home();
    if (h == nullptr) return p;
    for (const Kind b : before) {
      with_objects(*h, b, [&](const auto &objects) {
        for (const auto &o : objects) {
          const auto k = statement_of(o.origin);
          if (!k) continue;
          // Its DROP ... IF EXISTS, and the comment lines right above (its
          // banner), are its own.
          uint32_t at = statement(form_at(*k, o.name).drop.value_or(*k)).begin;
          const std::string_view t = text();
          if (at == line_begin(t, at)) {
            while (at > 0) {
              const uint32_t above = line_begin(t, at - 1);
              if (!is_comment_line(t.substr(above, at - 1 - above)) ||
                  t.substr(above, k_msm_marker.size()) == k_msm_marker) {
                break;
              }
              at = above;
            }
          }
          if (at >= within.begin && at < within.end) {
            within.end = at;
            cut = true;
          }
        }
      });
    }
    for (const Kind kind : kinds) {
      p.anchor = with_objects(*h, kind, [&](const auto &objects) { return last_of(objects, within); });
      if (p.anchor.k) break;
    }
    if (after_table != nullptr && !p.msm) {
      const auto kt = statement_of(after_table->origin);
      if (kt && (!p.anchor.k || *p.anchor.k < *kt)) p.anchor = {kt, &after_table->origin, after_table->name, Kind::table};
    }
    // In a section, or before the first object of `before`: at the end of
    // that range.
    if ((r || cut) && !p.anchor.k) p.spot = end_of_section(within, style_at(std::nullopt, std::nullopt).eol);
    return p;
  }
  std::optional<Spot> spot_of(const Place &p, const edit_text::Style &st) const {
    return p.anchor.k ? std::optional<Spot>(after_create(*p.anchor.k, st.eol)) : p.spot;
  }

  // Inserts the CREATE `sql` of a new table, view or sequence (`object`)
  // named `name` at `place`, laid out in style `st`, with statement `drop`
  // (a DROP ... IF EXISTS) before it, and migrates it.
  std::optional<Edit_error> insert_plain(size_t i, const Place &place, const edit_text::Style &st,
                                         const std::string &sql, Obj object, const std::string &name,
                                         Changes &changes, const std::string &drop = "") {
    const std::string laid = laid_out(sql, st, false);
    if (!insert_at(changes, spot_of(place, st), laid, st, false, drop)) {
      return error(i, "self-check", "no place for the CREATE " + upper(to_string(object)) + " of `" + name + "` was found");
    }
    migrate(i, laid, Act::create, object, name);
    return std::nullopt;
  }

  // Inserts the CREATE of a new routine, trigger or event (`object`, of
  // kind `kind`) named `name` at `place`, written as the object it follows
  // of its own kind is: dropped first or CREATE OR REPLACE, and in an MSM
  // script (Section 150, which every deployment runs again) always one of
  // the two; in a DELIMITER block when its body holds a semicolon.  `make`
  // writes the CREATE, given whether it is OR REPLACE.  Migrates it, with
  // a note that its CREATE cannot run inside a stored procedure.
  template <typename Make>
  std::optional<Edit_error> insert_create(size_t i, const Place &place, Kind kind, Migration_object object,
                                          const std::string &name, Make make, Changes &changes) {
    const Form form = place.anchor.kind == kind ? form_at(*place.anchor.k, place.anchor.name) : Form{};
    const edit_text::Style st = style_of(place.anchor);
    const std::string sql = make(st, form.or_replace);
    const std::string noun = to_string(object);  // procedure, function, trigger, event
    const bool drop_first = form.drop || (place.msm && !form.or_replace);
    const std::string drop = drop_first ? st.kw("DROP " + upper(noun) + " IF EXISTS") + " " + st.id(name) : "";
    const std::string what = noun + " `" + name + "`";
    if (const auto p = parse_problem(sql)) return error(i, "bad-request", what + " does not parse (" + *p + ")");
    const std::string laid = laid_out(sql, st, true);
    const auto at = insert_at(changes, spot_of(place, st), laid, st, laid.find(';') != std::string::npos, drop);
    if (!at) return error(i, "self-check", "no place for the CREATE of " + what + " was found");
    migrate(i, laid, Migration_action::create, object, name);
    const char *note_kind = kind == Kind::trigger ? "trigger-created" : kind == Kind::event ? "event-created" : "routine-created";
    note_at(i, note_kind, at->first, m_s.files()[at->first].log.to_original(at->second),
            "the migration creates the " + what + ", which cannot run inside a stored procedure: msm or a person must place it");
    return std::nullopt;
  }

  edit_text::Style style_of(const Anchor &a) const {
    return a.origin == nullptr ? style_at(std::nullopt, std::nullopt) : style_of(*a.origin);
  }

  // The items of a field list (CREATE TABLE's elements) with what each is:
  // the column it defines ("" for a key or constraint).
  struct Field_items {
    std::vector<Range> ranges;
    std::vector<uint32_t> nodes;
    std::vector<std::string> columns;
  };
  Field_items field_items(const Parsed &p, uint32_t list) const {
    Field_items out;
    const auto &t = p.tree;
    for (const uint32_t item : find_all_in(t, list, gram().field_list_item)) {
      out.ranges.push_back({t.begin(item), t.end(item)});
      out.nodes.push_back(item);
      const auto ch = t.children(item);
      std::string col;
      if (!ch.empty() && t.node(ch[0]).symbol == gram().column_def) {
        col = name_of(t, child_of(t, child_of(t, ch[0], gram().field_spec), gram().field_ident));
      }
      out.columns.push_back(std::move(col));
    }
    return out;
  }

  // The key parts of a key definition (key_def, a CREATE INDEX): the
  // searched range of each, its order included, and its column.
  struct Key_parts {
    std::vector<Range> ranges;
    std::vector<std::string> columns;
    bool found = false;
  };
  Key_parts key_parts_of(const Parsed &p, uint32_t n) const {
    Key_parts out;
    const auto &t = p.tree;
    const uint32_t list = child_of(t, n, gram().key_list);
    if (list == k_npos) {
      const uint32_t simple = find_in(t, n, gram().key_part_simple);
      if (simple == k_npos) return out;
      out.found = true;
      out.ranges.push_back({t.begin(simple), t.end(simple)});
      out.columns.push_back(name_of(t, find_in(t, simple, gram().ident)));
      return out;
    }
    out.found = true;
    const auto parts = find_all_in(t, list, gram().key_part);
    const auto dirs = find_all_in(t, list, gram().order_dir);
    for (size_t i = 0; i < parts.size(); ++i) {
      Range r{t.begin(parts[i]), t.end(parts[i])};
      const uint32_t next = i + 1 < parts.size() ? t.begin(parts[i + 1]) : t.end(list);
      for (const uint32_t d : dirs) {
        if (has_tokens(t, d) && t.begin(d) >= r.end && t.end(d) <= next) r.end = std::max(r.end, t.end(d));
      }
      out.ranges.push_back(r);
      out.columns.push_back(name_of(t, find_in(t, parts[i], gram().ident)));
    }
    return out;
  }

  // Removes `column` from a key: the whole of `whole` (true returned) when
  // it is the key's only column, else its key part.
  bool drop_key_part(Changes &changes, const Parsed &p, uint32_t key, const std::string &column,
                     std::vector<Range> *removed, bool *whole) {
    *whole = false;
    const auto kp = key_parts_of(p, key);
    std::set<size_t> gone;
    for (size_t i = 0; i < kp.columns.size(); ++i) {
      if (iequals(kp.columns[i], column)) gone.insert(i);
    }
    if (gone.empty()) return true;
    if (gone.size() == kp.columns.size()) {
      *whole = true;
      return true;
    }
    return remove_items(changes, kp.ranges, gone, removed);
  }

  // A column definition the server takes: a known type, and text that
  // parses in a CREATE TABLE.
  std::optional<Edit_error> check_column(size_t i, const Column &c, const std::string &def) {
    const std::string sql = "CREATE TABLE x (" + def + ")";
    if (const auto p = parse_problem(sql)) {
      return error(i, "bad-request", "column `" + c.name + "`: `" + c.type + "` and its attributes do not parse (" + *p + ")");
    }
    // The grammar takes any name as a type plugin's: a misspelt type parses.
    mdbp_parser *p = m_s.cache().get(m_s.options().index.sql_mode, m_s.options().index.version_id, true);
    mdbp_result r;
    if (p != nullptr && mdbp_parse_sql(p, sql.data(), sql.size(), &r) == MDBP_OK && r.accepted) {
      const Syntax_tree t = Syntax_tree::build(p, r, 0);
      // An attribute holding `, b INT` would add a column of its own.
      if (!t.empty() && find_all_in(t, t.roots()[0], gram().field_list_item).size() != 1) {
        return error(i, "bad-request", "column `" + c.name + "`: its attributes hold more than its definition");
      }
      const uint32_t udt = t.empty() ? k_npos : find_in(t, t.roots()[0], gram().udt_name);
      if (udt != k_npos && !edit_text::plugin_type(t.text(sql, udt))) {
        return error(i, "bad-request", "column `" + c.name + "`: `" + c.type + "` is not a MariaDB data type");
      }
    }
    return std::nullopt;
  }

  // ---------------------------------------------------------------------
  // Tables

  // Every key is named when it is created, so the migration can drop it by
  // the name the script gives it (the server's own name for an unnamed
  // foreign key depends on the deployed table's history).

  // `base`, else `base_2`, `base_3`, ...: the first that `taken` rejects
  // not; at most k_max_name_characters characters, as the server counts.
  template <typename Taken>
  static std::string unique_name(const std::string &base, Taken taken) {
    for (int n = 1;; ++n) {
      const std::string suffix = n == 1 ? "" : "_" + std::to_string(n);
      const size_t room = k_max_name_characters - suffix.size();
      size_t keep = 0;  // the bytes of `base`'s first `room` characters
      for (size_t chars = 0; keep < base.size(); ++keep) {
        if ((static_cast<unsigned char>(base[keep]) & 0xC0) != 0x80 && chars++ == room) break;
      }
      std::string name = base.substr(0, keep) + suffix;
      if (!taken(name)) return name;
    }
  }
  // An unnamed index's name, as the server would give it: its first
  // column's, made unique among `indexes` (and PRIMARY).  The unnamed
  // indexes before it hold the names the server gave them the same way
  // (a column's UNIQUE is one), so those are taken too.
  static std::string index_name(const std::vector<Index> &indexes, const Index &ix) {
    std::vector<std::string> given;  // the server's names of the unnamed indexes before `ix`
    const auto taken = [&](const std::string &n) {
      const auto is_n = [&](const std::string &x) { return iequals(x, n); };
      return iequals(n, "PRIMARY") || std::any_of(given.begin(), given.end(), is_n) ||
             std::any_of(indexes.begin(), indexes.end(), [&](const Index &x) { return is_n(x.name); });
    };
    const auto base_of = [](const Index &x) { return x.key_parts.empty() ? std::string("idx") : x.key_parts[0].column; };
    for (const auto &x : indexes) {
      if (&x == &ix) break;
      if (x.kind != "primary" && x.name.empty()) given.push_back(unique_name(base_of(x), taken));
    }
    return unique_name(base_of(ix), taken);
  }
  // The table whose foreign key is named `name`, in the request's schemas.
  const Table *foreign_key_owner(const std::string &name) const {
    for (const Schema *sc : lookup().schemas()) {
      for (const auto &x : sc->tables) {
        for (const auto &f : x.foreign_keys) {
          if (iequals(f.name, name)) return &x;
        }
      }
    }
    return nullptr;
  }
  // An unnamed foreign key's name: fk_<table>_<referenced table>, made
  // unique among the schema's foreign keys and `also`.
  std::string foreign_key_name(const std::string &table, const Foreign_key &fk,
                               const std::vector<Foreign_key> &also) const {
    return unique_name("fk_" + table + "_" + fk.ref_table, [&](const std::string &n) {
      return foreign_key_owner(n) != nullptr ||
             std::any_of(also.begin(), also.end(), [&](const Foreign_key &f) { return iequals(f.name, n); });
    });
  }
  // `e` with a name for each key it creates without one: the script and
  // the server then agree on it, and a later drop can name it.
  Schema_edit with_key_names(const Schema_edit &e) const {
    Schema_edit out = e;
    if (out.new_table) {
      Table &t = *out.new_table;
      for (auto &ix : t.indexes) {
        if (ix.kind != "primary" && ix.name.empty()) ix.name = index_name(t.indexes, ix);
      }
      for (auto &fk : t.foreign_keys) {
        if (fk.name.empty()) fk.name = foreign_key_name(t.name, fk, t.foreign_keys);
      }
    }
    const Table *table = out.new_index || out.new_foreign_key ? lookup().table(e.table).second : nullptr;
    if (table != nullptr && out.new_index && out.new_index->kind != "primary" && out.new_index->name.empty()) {
      out.new_index->name = index_name(table->indexes, *out.new_index);
    }
    if (table != nullptr && out.new_foreign_key && out.new_foreign_key->name.empty()) {
      out.new_foreign_key->name = foreign_key_name(table->name, *out.new_foreign_key, {});
    }
    return out;
  }

  std::optional<Edit_error> add_table(size_t i, const Schema_edit &e, Changes &changes) {
    const Table &t = *e.new_table;
    if (auto err = table_name_free(i, "", t.name)) return err;
    for (size_t a = 0; a < t.columns.size(); ++a) {
      for (size_t b = a + 1; b < t.columns.size(); ++b) {
        if (iequals(t.columns[a].name, t.columns[b].name)) {
          return error(i, "bad-request", "table `" + t.name + "` has two columns `" + t.columns[b].name + "`");
        }
      }
    }
    size_t primaries = 0;
    for (const auto &ix : t.indexes) {
      if (ix.kind == "primary") ++primaries;
      for (const auto &kp : ix.key_parts) {
        if (t.column(kp.column) == nullptr) {
          return error(i, "unknown-object", "an index of `" + t.name + "` names the column `" + kp.column +
                                                "`, which the table does not have");
        }
      }
    }
    if (primaries > 1) return error(i, "bad-request", "table `" + t.name + "` has two primary keys");
    for (const auto &ck : t.checks) {
      if (!ck.column.empty() && t.column(ck.column) == nullptr) {
        return error(i, "unknown-object", "a check of `" + t.name + "` is on the column `" + ck.column +
                                              "`, which the table does not have");
      }
    }
    for (const auto &fk : t.foreign_keys) {
      if (auto err = check_foreign_key(i, t, fk)) return err;
    }
    // An MSM script's tables are in Section 140.
    const Place place = place_for(140, {Kind::table});
    const edit_text::Style st = style_of(place.anchor);
    for (const auto &c : t.columns) {
      if (auto err = check_column(i, c, edit_text::column_sql(c, edit_text::column_checks(t, c.name), st))) {
        return err;
      }
    }
    const std::string sql = edit_text::create_table_sql(t, st);
    if (const auto problem = parse_problem(sql)) {
      // Which part: each key on its own.
      const std::string probe = "CREATE TABLE x (";
      for (const auto &ix : t.indexes) {
        if (const auto p = parse_problem(probe + "a INT, " + edit_text::index_sql(ix, st) + ")")) {
          const std::string which = ix.name.empty() ? ix.kind : "`" + ix.name + "`";
          return error(i, "bad-request", "index " + which + " does not parse (" + *p + ")");
        }
      }
      return error(i, "bad-request", "the CREATE TABLE of `" + t.name + "` does not parse (" + *problem + ")");
    }
    return insert_plain(i, place, st, sql, Obj::table, t.name, changes);
  }

  // The foreign keys (with their table) that use column `column` of `table`:
  // its own, and the other tables' that reference it.
  std::vector<std::pair<const Table *, const Foreign_key *>> foreign_keys_using(const Table &table,
                                                                                 const std::string &column) const {
    std::vector<std::pair<const Table *, const Foreign_key *>> out;
    for (const Schema *s : lookup().schemas()) {
      for (const auto &x : s->tables) {
        for (const auto &fk : x.foreign_keys) {
          const bool own = &x == &table && names(fk.columns, column);
          const bool refs = iequals(fk.ref_table, table.name) && names(fk.ref_columns, column);
          if (own || refs) out.push_back({&x, &fk});
        }
      }
    }
    return out;
  }

  // A foreign key of table `t` (new or existing): its columns, the table and
  // columns it references, a name no other foreign key of the schema has.
  std::optional<Edit_error> check_foreign_key(size_t i, const Table &t, const Foreign_key &fk) {
    for (const auto &c : fk.columns) {
      if (t.column(c) == nullptr) {
        return error(i, "unknown-object", "table `" + t.name + "` has no column `" + c + "`");
      }
    }
    const Table *ref = nullptr;
    if (iequals(fk.ref_table, t.name) && fk.ref_schema.empty()) {
      ref = &t;
    } else if (!fk.ref_schema.empty()) {
      if (const Schema *s = outline().catalog.find(fk.ref_schema)) ref = s->table(fk.ref_table);
      if (ref == nullptr && iequals(fk.ref_table, t.name) && home() != nullptr && iequals(home()->name, fk.ref_schema)) ref = &t;
    } else {
      ref = lookup().table(fk.ref_table).second;
    }
    if (ref == nullptr) return error(i, "unknown-object", "there is no table `" + fk.ref_table + "` to reference");
    for (const auto &c : fk.ref_columns) {
      if (ref->column(c) == nullptr) {
        return error(i, "unknown-object", "table `" + ref->name + "` has no column `" + c + "` to reference");
      }
    }
    if (!fk.name.empty()) {
      if (const Table *owner = foreign_key_owner(fk.name)) {
        return error(i, "name-taken", "a foreign key `" + fk.name + "` exists on table `" + owner->name + "`");
      }
    }
    return std::nullopt;
  }

  // The searched offset of the first (or, with `last`, the last) line
  // `DELIMITER ...` that starts in [b, e).
  std::optional<uint32_t> delimiter_line(uint32_t b, uint32_t e, bool last) const {
    const std::string_view t = text();
    std::optional<uint32_t> found;
    uint32_t p = b == line_begin(t, b) ? b : past_line_break(t, line_end(t, b));
    while (p < e && p < t.size()) {
      const uint32_t le = line_end(t, p);
      const std::string_view line = trim(t.substr(p, le - p));
      if (line.size() > 10 && iequals(line.substr(0, 10), "DELIMITER ")) {
        found = p;
        if (!last) break;
      }
      const uint32_t next = past_line_break(t, le);
      if (next == p) break;
      p = next;
    }
    return found;
  }

  // A DELIMITER block whose SQL statements are all among `gone` goes too,
  // its two DELIMITER lines with the blank lines above it, as adding a
  // routine, trigger or event in a block of its own wrote it.
  void remove_emptied_delimiter_blocks(Changes &changes, const std::set<size_t> &gone) {
    const std::string_view t = text();
    const auto &sts = index().statements();
    auto sql = [&](size_t j) { return sts[j].kind == Statement_kind::sql; };
    std::set<size_t> done;  // the statements of a block already looked at
    for (const size_t k : gone) {
      if (done.count(k) != 0) continue;
      const size_t d = statement(k).active_delimiter;
      if (index().delimiters()[d] == ";") continue;
      // The run of statements around `k` under its delimiter that all go,
      // and its neighbours.  A statement under another delimiter (a DROP
      // ... IF EXISTS above the block) bounds the run, gone or not.
      auto in_run = [&](size_t j) { return gone.count(j) != 0 && statement(j).active_delimiter == d; };
      size_t first = k, last = k;
      std::optional<size_t> prev, next;
      for (size_t j = k; j-- > 0;) {
        if (!sql(j)) continue;
        if (!in_run(j)) {
          prev = j;
          break;
        }
        first = j;
      }
      for (size_t j = k + 1; j < sts.size(); ++j) {
        if (!sql(j)) continue;
        if (!in_run(j)) {
          next = j;
          break;
        }
        last = j;
      }
      for (size_t j = first; j <= last; ++j) done.insert(j);
      // One block: no DELIMITER line between the run's statements.
      if (delimiter_line(statement(first).begin, statement(last).end, false)) continue;
      const auto open = delimiter_line(prev ? statement(*prev).end : 0, statement(first).begin, true);
      const auto close =
          delimiter_line(statement(last).end, next ? statement(*next).begin : static_cast<uint32_t>(t.size()), false);
      if (!open || !close) continue;
      const uint32_t end = past_line_break(t, line_end(t, *close));
      uint32_t above = *open;
      while (above > 0) {
        const uint32_t pb = line_begin(t, above - 1);
        if (!trim(t.substr(pb, above - 1 - pb)).empty() || !mappable(pb, end)) break;
        above = pb;
      }
      change(changes, above, end, "");  // overlaps the statements' removals: normalize() merges them
    }
  }

  // Removes the statements `gone` (with the word their banner names), then
  // notes what still refers to the object and migrates with its DROP.
  std::optional<Edit_error> drop_statements(size_t i, const std::vector<std::pair<size_t, std::string>> &gone,
                                            const std::optional<Object_id> &id, const std::string &name,
                                            const std::string &what, std::string_view rest_kind, Migration_object object,
                                            Changes &changes) {
    std::vector<Range> removed;
    std::set<size_t> ks;
    for (const auto &[k, banner] : gone) {
      const auto r = remove_statement(changes, k, banner);
      if (!r) return error(i, "self-check", "a statement on `" + name + "` cannot be removed");
      removed.push_back(*r);
      ks.insert(k);
    }
    remove_emptied_delimiter_blocks(changes, ks);
    still_referenced(i, id, removed, what);
    rest_notes(i, name, rest_kind, what);
    m_old_name = name;
    migrate_drop(i, object, name);
    return std::nullopt;
  }

  std::optional<Edit_error> drop_table(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, table] = lookup().table(e.table);
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    for (const Schema *s : lookup().schemas()) {
      for (const auto &x : s->tables) {
        if (iequals(x.name, table->name)) continue;
        for (const auto &fk : x.foreign_keys) {
          if (iequals(fk.ref_table, table->name) && (fk.ref_schema.empty() || iequals(fk.ref_schema, schema->name))) {
            return error(i, "referenced", "a foreign key of `" + x.name + "` references `" + table->name + "`");
          }
        }
      }
    }
    const std::string name = table->name;
    const auto id = resolve(table->origin, Target_kind::table);
    std::vector<std::pair<size_t, std::string>> gone;  // statement, banner word
    for (const size_t k : statements_about("TABLE", name, schema->name)) gone.push_back({k, name});
    for (const size_t k : statements_about("INDEX", name, schema->name, true)) gone.push_back({k, name});
    for (const auto &tr : schema->triggers) {
      if (!iequals(tr.table, name)) continue;
      for (const size_t k : statements_about("TRIGGER", tr.name, schema->name)) gone.push_back({k, tr.name});
    }
    return drop_statements(i, gone, id, name, "table `" + name + "`", "view", Obj::table, changes);
  }

  // ---------------------------------------------------------------------
  // Columns

  // The CREATE TABLE of `t` parsed, with its field list items.
  struct Create_table {
    size_t k = 0;
    Parsed parsed;
    Field_items items;
    uint32_t list = k_npos;
  };
  std::optional<Create_table> create_table_of(const Table &t) {
    const auto k = statement_of(t.origin);
    if (!k) return std::nullopt;
    auto p = parse(*k);
    if (!p || p->tree.node(p->verb).symbol != gram().create) return std::nullopt;
    Create_table c{*k, std::move(*p), {}, k_npos};
    c.list = find_in(c.parsed.tree, child_of(c.parsed.tree, c.parsed.verb, gram().create_body),
                     gram().create_field_list_parens);
    if (c.list != k_npos) c.items = field_items(c.parsed, c.list);
    return c;
  }

  std::optional<Edit_error> add_column(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Column &c = *e.new_column;
    if (table->column(c.name) != nullptr) {
      return error(i, "name-taken", "table `" + table->name + "` has a column `" + c.name + "`");
    }
    if (!e.after.empty() && table->column(e.after) == nullptr) {
      return error(i, "unknown-object", "table `" + table->name + "` has no column `" + e.after + "`");
    }
    if (table->columns.empty()) {
      return error(i, "unsupported", "the columns of `" + table->name + "` are not known (CREATE TABLE ... AS or LIKE)");
    }
    const std::string after = e.first ? "" : e.after.empty() ? table->columns.back().name : table->column(e.after)->name;
    m_columns = table->columns.size();
    m_position = 0;
    for (size_t k = 0; !e.first && k < table->columns.size(); ++k) {
      if (iequals(table->columns[k].name, after)) m_position = k + 1;
    }
    const edit_text::Style st = style_of(table->origin);
    const std::string def = edit_text::column_sql(c, {}, st);
    if (auto err = check_column(i, c, def)) return err;
    const auto ct = create_table_of(*table);
    bool done = false;
    if (ct && !ct->items.ranges.empty()) {
      if (e.first) {
        done = insert_item(changes, ct->items.ranges, std::nullopt, def, st.eol);
      } else {
        for (size_t k = 0; k < ct->items.columns.size(); ++k) {
          if (iequals(ct->items.columns[k], after)) done = insert_item(changes, ct->items.ranges, k, def, st.eol);
        }
      }
    }
    if (!done) {
      // `after` is added by an ALTER TABLE: one more after that one.
      changes.clear();
      const Column *a = after.empty() ? nullptr : table->column(after);
      const auto k = a != nullptr ? statement_of(a->origin) : statement_of(table->origin);
      std::string sql = st.kw("ALTER TABLE") + " " + st.id(table->name) + " " + st.kw("ADD COLUMN") + " " + def;
      sql += e.first ? " " + st.kw("FIRST") : " " + st.kw("AFTER") + " " + st.id(after);
      if (!k || !insert_statement(changes, k, laid_out(sql, st, false), st, false)) {
        return error(i, "self-check", "no place for column `" + c.name + "` was found");
      }
    }
    std::string clause = "ADD COLUMN " + edit_text::column_sql(c, {}, {});
    if (e.first) clause += " FIRST";
    else if (!e.after.empty()) clause += " AFTER " + backtick_quote(after);
    migrate_member(i, table->name, clause, Act::add, Obj::column, c.name);
    return std::nullopt;
  }

  std::optional<Edit_error> modify_column(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Column *column = table->column(e.column);
    if (column == nullptr) {
      return error(i, "unknown-object", "table `" + table->name + "` has no column `" + e.column + "`");
    }
    Column nc = *e.new_column;
    const std::string table_name = table->name;
    const std::string old_name = column->name;
    const bool renamed = old_name != nc.name;
    if (!iequals(old_name, nc.name) && table->column(nc.name) != nullptr) {
      return error(i, "name-taken", "table `" + table_name + "` has a column `" + nc.name + "`");
    }
    // The foreign keys using the column go before the change and come back
    // after it (MariaDB refuses to change such a column: 1833).
    std::vector<std::pair<std::string, Foreign_key>> fks;  // (table, key), copied: the rename rebuilds
    for (const auto &[x, fk] : foreign_keys_using(*table, old_name)) fks.push_back({x->name, *fk});
    m_old_name = old_name;
    if (renamed) {
      const auto id = resolve(column->origin, Target_kind::column);
      if (!id) return error(i, "self-check", "the definition of column `" + old_name + "` cannot be resolved");
      string_notes(i, {old_name, table_name}, "column `" + table_name + "`.`" + old_name + "`");
      Changes rename;
      if (auto err = rewrite_references(i, *id, column->origin, nc.name, rename)) return err;
      m_s.apply(rename);
      m_s.build();
      table = lookup().table(table_name).second;
      column = table == nullptr ? nullptr : table->column(nc.name);
      if (column == nullptr) return error(i, "self-check", "column `" + nc.name + "` is lost by its rename");
    }
    const std::vector<Check> checks = edit_text::column_checks(*table, column->name);
    // A column-level PRIMARY KEY or UNIQUE belongs to the definition.
    std::string key;
    bool in_primary = false;
    for (const auto &ix : table->indexes) {
      if (ix.kind == "primary" && names(ix.columns(), column->name)) in_primary = true;
      if (ix.key_parts.size() == 1 && column_level(*table, ix) == column) {
        key = ix.kind == "primary" ? "PRIMARY KEY" : "UNIQUE";
      }
    }
    const auto range = searched_range(column->origin.source, column->origin.statement);
    const auto name_range = searched_range(column->origin.source, column->origin.name);
    if (!range || !name_range) return error(i, "self-check", "the definition of column `" + nc.name + "` is not found");
    const auto k = index().find(range->begin);
    edit_text::Style st = style_at(k < index().statements().size() ? std::optional<size_t>(k) : std::nullopt,
                                   name_range->begin);
    const std::string token(text().substr(name_range->begin, name_range->end - name_range->begin));
    std::string def = edit_text::column_sql(nc, checks, st);
    def = spell_like(token, nc.name) + def.substr(st.id(nc.name).size());
    if (!key.empty()) def += " " + st.kw(key);
    if (auto err = check_column(i, nc, def)) return err;
    if (!change(changes, range->begin, range->end, def)) {
      return error(i, "self-check", "the definition of column `" + nc.name + "` cannot be changed");
    }
    m_in_primary = in_primary;
    for (const auto &[x, fk] : fks) {
      if (fk.name.empty()) {
        note(i, "no-migration", fk.origin.source, fk.origin.statement.begin,
             "an unnamed foreign key of `" + x + "` uses the column: the migration cannot drop and add it again");
        continue;
      }
      migrate_drop_foreign_key(i, x, fk.name);
    }
    const std::string body = edit_text::column_sql(nc, checks, {});
    migrate_member(i, table_name, (renamed ? "CHANGE COLUMN " + backtick_quote(old_name) + " " : "MODIFY COLUMN ") + body,
                   Act::alter, Obj::column, old_name, renamed ? nc.name : std::string());
    for (auto [x, fk] : fks) {
      if (fk.name.empty()) continue;
      for (auto &col : fk.columns) {
        if (iequals(x, table_name) && iequals(col, old_name)) col = nc.name;
      }
      for (auto &col : fk.ref_columns) {
        if (iequals(fk.ref_table, table_name) && iequals(col, old_name)) col = nc.name;
      }
      migrate_add_foreign_key(i, x, fk);
    }
    return std::nullopt;
  }

  std::optional<Edit_error> drop_column(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, table] = lookup().table(e.table);
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Column *column = table->column(e.column);
    if (column == nullptr) {
      return error(i, "unknown-object", "table `" + table->name + "` has no column `" + e.column + "`");
    }
    const std::string name = column->name;
    if (table->columns.size() == 1) {
      return error(i, "bad-request", "`" + name + "` is the only column of `" + table->name + "`: drop the table");
    }
    if (const auto fks = foreign_keys_using(*table, name); !fks.empty()) {
      return error(i, "referenced",
                   "a foreign key of `" + fks[0].first->name + "` uses the column `" + table->name + "`.`" + name + "`");
    }
    const auto id = resolve(column->origin, Target_kind::column);
    m_columns = table->columns.size();
    std::vector<Range> removed;
    std::set<size_t> done;
    auto fail = [&] { return error(i, "self-check", "the column `" + name + "` cannot be removed from a statement"); };
    std::vector<std::pair<size_t, bool>> sts;  // statement, CREATE INDEX
    for (const size_t k : statements_about("TABLE", table->name, schema->name)) sts.push_back({k, false});
    for (const size_t k : statements_about("INDEX", table->name, schema->name, true)) sts.push_back({k, true});
    for (const auto &[k, is_index] : sts) {
      if (!done.insert(k).second) continue;
      auto p = parse(k);
      if (!p) continue;
      const auto &t = p->tree;
      const int verb = t.node(p->verb).symbol;
      const uint32_t list =
          verb == gram().create && !is_index
              ? find_in(t, child_of(t, p->verb, gram().create_body), gram().create_field_list_parens)
              : k_npos;
      if (list != k_npos) {
        const auto items = field_items(*p, list);
        std::set<size_t> gone;
        for (size_t j = 0; j < items.nodes.size(); ++j) {
          if (iequals(items.columns[j], name)) {
            gone.insert(j);
            continue;
          }
          const auto ch = t.children(items.nodes[j]);
          if (ch.empty() || t.node(ch[0]).symbol != gram().key_def) continue;
          bool whole = false;
          if (!drop_key_part(changes, *p, ch[0], name, &removed, &whole)) return fail();
          if (whole) gone.insert(j);
        }
        if (gone.size() == items.nodes.size()) return fail();
        if (!gone.empty() && !remove_items(changes, items.ranges, gone, &removed)) return fail();
      } else if (verb == gram().create && is_index) {
        bool whole = false;
        if (!drop_key_part(changes, *p, p->verb, name, &removed, &whole)) return fail();
        if (whole) {
          const auto r = remove_statement(changes, k, table->name);
          if (!r) return fail();
          removed.push_back(*r);
        }
      } else if (verb == gram().alter) {
        const auto items = find_all_in(t, child_of(t, p->verb, gram().alter_commands), gram().alter_list_item);
        std::vector<Range> ranges;
        std::set<size_t> gone;
        for (size_t j = 0; j < items.size(); ++j) {
          const uint32_t item = items[j];
          ranges.push_back({t.begin(item), t.end(item)});
          const auto ch = t.children(item);
          if (ch.empty()) continue;
          const int first = t.node(ch[0]).symbol;
          const uint32_t spec = find_in(t, item, gram().field_spec);
          if ((first == gram().add_column || first == gram().CHANGE || first == gram().MODIFY_SYM) && spec != k_npos &&
              iequals(name_of(t, child_of(t, spec, gram().field_ident)), name)) {
            gone.insert(j);
          } else if (first == gram().ALTER && child_of(t, item, gram().field_ident) != k_npos &&
                     iequals(name_of(t, child_of(t, item, gram().field_ident)), name)) {
            gone.insert(j);
          } else if (first == gram().ADD && child_of(t, item, gram().key_def) != k_npos) {
            const uint32_t kd = child_of(t, item, gram().key_def);
            if (t.child(kd, gram().FOREIGN) != k_npos) continue;
            bool whole = false;
            if (!drop_key_part(changes, *p, kd, name, &removed, &whole)) return fail();
            if (whole) gone.insert(j);
          }
        }
        if (gone.empty()) continue;
        if (gone.size() == items.size()) {
          const auto r = remove_statement(changes, k, table->name);
          if (!r) return fail();
          removed.push_back(*r);
        } else if (!remove_items(changes, ranges, gone, &removed)) {
          return fail();
        }
      }
    }
    still_referenced(i, id, removed, "column `" + table->name + "`.`" + name + "`");
    m_old_name = name;
    migrate_member(i, table->name, "DROP COLUMN " + backtick_quote(name), Act::drop, Obj::column, name);
    return std::nullopt;
  }

  // ---------------------------------------------------------------------
  // Table options

  // The outline name of a create_table_option node, as the fold gives it;
  // "" for one it does not keep as an option.
  std::string option_key(const Syntax_tree &t, uint32_t o) const {
    const auto ch = t.children(o);
    if (ch.empty()) return "";
    const int sym = t.node(ch[0]).symbol;
    const auto &g = gram();
    if (sym == g.default_charset) return "charset";
    if (sym == g.default_collation) return "collation";
    if (sym == g.COMMENT_SYM) return "comment";
    if (sym == g.ENGINE_SYM) return "engine";
    if (sym == g.versioning_option) return "";
    if (sym == g.engine_defined_option) {
      const auto ech = t.children(ch[0]);
      return ech.empty() ? "" : lower(t.text(text(), ech[0]));
    }
    if (t.is_token(ch[0])) return lower(t.text(text(), ch[0]));
    return "";
  }

  std::optional<Edit_error> set_table_options(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    auto ct = create_table_of(*table);
    if (!ct || ct->list == k_npos) {
      return error(i, "unsupported", "the CREATE TABLE of `" + table->name + "` has no column list to set options after");
    }
    const auto &t = ct->parsed.tree;
    const uint32_t opts = child_of(t, child_of(t, ct->parsed.verb, gram().create_body), gram().opt_create_table_options);
    const auto nodes = find_all_in(t, opts, gram().create_table_option);
    const edit_text::Style st = style_of(table->origin);
    std::string appended;
    std::vector<std::string> sets;
    bool removes = false;
    for (const auto &[key, value] : e.options) {
      std::vector<uint32_t> found;
      for (const uint32_t n : nodes) {
        if (option_key(t, n) == key) found.push_back(n);
      }
      for (size_t f = 0; f < found.size(); ++f) {
        const uint32_t n = found[f];
        if (value && f + 1 == found.size()) {
          if (!change(changes, t.begin(n), t.end(n), edit_text::table_option_sql(key, *value, st))) {
            return error(i, "self-check", "the option " + key + " of `" + table->name + "` cannot be changed");
          }
          continue;
        }
        // Removed, with the blanks and comma before it; the first of a
        // comma-separated list with the comma after it.
        uint32_t b = t.begin(n);
        uint32_t end = t.end(n);
        while (b > 0 && (text()[b - 1] == ' ' || text()[b - 1] == '\t' || text()[b - 1] == '\n' || text()[b - 1] == '\r')) --b;
        if (b > 0 && text()[b - 1] == ',') {
          --b;
          while (b > 0 && (text()[b - 1] == ' ' || text()[b - 1] == '\t')) --b;
        } else {
          uint32_t q = end;
          while (q < text().size() && (text()[q] == ' ' || text()[q] == '\t')) ++q;
          if (q < text().size() && text()[q] == ',') end = q + 1;
        }
        if (!change(changes, b, end, "")) {
          return error(i, "self-check", "the option " + key + " of `" + table->name + "` cannot be removed");
        }
      }
      if (value && found.empty()) appended += " " + edit_text::table_option_sql(key, *value, st);
      if (value) sets.push_back(edit_text::table_option_sql(key, *value, {}));
      else removes = true;
    }
    if (!appended.empty()) {
      const uint32_t at = nodes.empty() ? t.end(ct->list) : t.end(nodes.back());
      if (!change(changes, at, at, appended)) {
        return error(i, "self-check", "the options of `" + table->name + "` cannot be added to");
      }
    }
    if (!sets.empty()) {
      migrate_member(i, table->name, join(sets, " "), Act::alter, Obj::table_options, table->name);
    }
    if (removes) {
      note(i, "no-migration", table->origin.source, table->origin.statement.begin,
           "a table option of `" + table->name + "` was removed: no statement takes it back to the server's default");
    }
    return std::nullopt;
  }

  // ---------------------------------------------------------------------
  // Views and routines

  std::optional<Edit_error> add_view(size_t i, const Schema_edit &e, Changes &changes) {
    const View &v = *e.new_view;
    if (auto err = table_name_free(i, "", v.name)) return err;
    // An MSM script's views are in Section 150, which every deployment runs
    // again: the view must be CREATE OR REPLACE (or dropped first).
    const Place place = place_for(150, {Kind::view, Kind::table});
    const Form form = place.anchor.kind == Kind::view ? form_at(*place.anchor.k, place.anchor.name) : Form{};
    const edit_text::Style st = style_of(place.anchor);
    const std::string sql = edit_text::create_view_sql(v, st, form.or_replace || (place.msm && !form.drop));
    const std::string drop = form.drop && !form.or_replace ? st.kw("DROP VIEW IF EXISTS") + " " + st.id(v.name) : "";
    if (const auto p = parse_problem(sql)) {
      return error(i, "bad-request", "view `" + v.name + "` does not parse (" + *p + ")");
    }
    return insert_plain(i, place, st, sql, Obj::view, v.name, changes, drop);
  }

  std::optional<Edit_error> drop_view(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, view] = lookup().view(e.view);
    if (view == nullptr) return error(i, "unknown-object", "there is no view `" + e.view + "`");
    const std::string name = view->name;
    std::vector<std::pair<size_t, std::string>> gone;
    for (const size_t k : statements_about("VIEW", name, schema->name)) gone.push_back({k, name});
    return drop_statements(i, gone, resolve(view->origin, Target_kind::view), name, "view `" + name + "`", "view",
                           Obj::view, changes);
  }

  std::optional<Edit_error> add_routine(size_t i, const Schema_edit &e, Changes &changes) {
    const Routine &r = *e.new_routine;
    const auto l = lookup();
    // A procedure and a function may share a name.
    if (!l.routines(r.name, r.kind).empty()) return error(i, "name-taken", "a " + r.kind + " `" + r.name + "` exists");
    m_routine_kind = r.kind;
    return insert_create(i, place_for(150, {Kind::routine, Kind::view, Kind::table}), Kind::routine,
                         routine_object(r.kind), r.name,
                         [&](const edit_text::Style &st, bool or_replace) {
                           return edit_text::create_routine_sql(r, st, or_replace);
                         },
                         changes);
  }

  std::optional<Edit_error> drop_routine(size_t i, const Schema_edit &e, Changes &changes) {
    const auto found = lookup().routines(e.routine, e.routine_kind);
    if (found.empty()) return error(i, "unknown-object", "there is no routine `" + e.routine + "`");
    if (found.size() > 1) {
      return error(i, "bad-request", "`" + e.routine + "` names a procedure and a function: give \"kind\"");
    }
    const auto [schema, routine] = found[0];
    const std::string name = routine->name;
    const std::string kind = routine->kind;
    const std::optional<Object_id> id = resolve(routine->origin, Target_kind::routine);
    std::vector<std::pair<size_t, std::string>> gone;
    for (const size_t k : statements_about(upper(kind), name, schema->name)) gone.push_back({k, name});
    m_routine_kind = kind;
    return drop_statements(i, gone, id, name, kind + " `" + name + "`", kind, routine_object(kind), changes);
  }


  // ---------------------------------------------------------------------
  // Triggers and events

  // The searched range of `word`, a view into the searched text.
  Range range_of(std::string_view word) const {
    const auto at = static_cast<uint32_t>(word.data() - text().data());
    return Range{at, at + static_cast<uint32_t>(word.size())};
  }

  // Respells the name at searched range `r` as `new_name`, quoted as it is.
  bool respell(Changes &changes, Range r, const std::string &new_name) {
    const std::string token(text().substr(r.begin, r.end - r.begin));
    return change(changes, r.begin, r.end, spell_like(token, new_name));
  }

  // The searched range of the trigger a CREATE TRIGGER statement `k` names
  // in its FOLLOWS or PRECEDES.
  std::optional<Range> order_name_range(size_t k) const {
    const std::string_view sql = index().text_of(text(), statement(k));
    // Not a fixed few words: a DEFINER and a long UPDATE OF list come
    // before FOR EACH ROW.
    const auto w = words_of(sql, 4096);
    auto is = [&](size_t x, const char *word) { return x < w.size() && !w[x].quoted && iequals(w[x].text, word); };
    for (size_t j = 2; j + 1 < w.size(); ++j) {
      if (!is(j - 2, "FOR") || !is(j - 1, "EACH") || !is(j, "ROW")) continue;
      if (!is(j + 1, "FOLLOWS") && !is(j + 1, "PRECEDES")) return std::nullopt;
      if (j + 2 >= w.size() || w[j + 2].punct) return std::nullopt;
      return range_of(w[j + 2].text);
    }
    return std::nullopt;
  }

  // Rewrites the subject name of every statement about `name` (its CREATE,
  // ALTER, DROP [IF EXISTS]) of keyword `kind` to `new_name`.
  std::optional<Edit_error> rename_subjects(size_t i, const std::string &kind, const std::string &name,
                                            const std::string &schema, const std::string &new_name,
                                            Changes &changes) {
    const auto ks = statements_about(kind, name, schema);
    if (ks.empty()) return error(i, "self-check", "the CREATE " + kind + " of `" + name + "` is not found");
    for (const size_t k : ks) {
      // statements_about() chose it by this subject: one name, `name`.
      const auto subject = subject_of(index().text_of(text(), statement(k)));
      if (!respell(changes, range_of(subject->name_words[0]), new_name)) {
        return error(i, "edit-outside-slice", "the name of a statement on `" + name + "` cannot be changed");
      }
    }
    return std::nullopt;
  }

  std::optional<Edit_error> add_trigger(size_t i, const Schema_edit &e, Changes &changes) {
    Trigger t = *e.new_trigger;
    const auto l = lookup();
    const Table *table = l.table(t.table).second;
    if (table == nullptr) {
      if (l.view(t.table).second != nullptr) {
        return error(i, "unknown-object", "`" + t.table + "` is a view: a trigger goes on a table");
      }
      return error(i, "unknown-object", "there is no table `" + t.table + "`");
    }
    t.table = table->name;
    for (auto &c : t.update_columns) {
      const Column *col = table->column(c);
      if (col == nullptr) return error(i, "unknown-object", "table `" + table->name + "` has no column `" + c + "`");
      c = col->name;
    }
    if (const Trigger *x = l.trigger(t.name).second) {
      return error(i, "name-taken", "a trigger `" + t.name + "` exists on table `" + x->table + "`");
    }
    if (!t.order_kind.empty()) {
      // On the same table, with the same timing, and firing on every event
      // of the new one: else MariaDB refuses it (error 4031).
      const Trigger *o = l.trigger(t.order_trigger).second;
      auto covers = [&] {
        const auto theirs = event_set(o->events);
        const auto ours = event_set(t.events);
        return std::includes(theirs.begin(), theirs.end(), ours.begin(), ours.end());
      };
      if (o == nullptr || !iequals(o->table, table->name) || !iequals(o->timing, t.timing) || !covers()) {
        return error(i, "unknown-object",
                     "there is no trigger `" + t.order_trigger + "` " + t.timing + " " + t.events + " on table `" +
                         table->name + "` to " + lower(t.order_kind));
      }
      t.order_trigger = o->name;
    }
    // Before the events, after its table.
    const Place place = place_for(150, {Kind::trigger, Kind::routine, Kind::view, Kind::table}, {Kind::event}, table);
    return insert_create(
        i, place, Kind::trigger, Obj::trigger, t.name,
        [&](const edit_text::Style &st, bool or_replace) { return edit_text::create_trigger_sql(t, st, or_replace); },
        changes);
  }

  std::optional<Edit_error> drop_trigger(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, trigger] = lookup().trigger(e.trigger);
    if (trigger == nullptr) return error(i, "unknown-object", "there is no trigger `" + e.trigger + "`");
    if (!e.table.empty() && !iequals(trigger->table, e.table)) {
      return error(i, "unknown-object", "trigger `" + trigger->name + "` is on `" + trigger->table + "`, not `" +
                                            e.table + "`");
    }
    const std::string name = trigger->name;
    // A trigger another one follows or precedes stays: that one's CREATE
    // fails without it.
    for (const auto &x : schema->triggers) {
      if (iequals(x.order_trigger, name) && !iequals(x.name, name)) {
        return error(i, "referenced", "trigger `" + x.name + "` " + lower(x.order_kind) + " `" + name + "`");
      }
    }
    std::vector<std::pair<size_t, std::string>> gone;
    for (const size_t k : statements_about("TRIGGER", name, schema->name)) gone.push_back({k, name});
    return drop_statements(i, gone, std::nullopt, name, "trigger `" + name + "`", "trigger", Obj::trigger, changes);
  }

  std::optional<Edit_error> rename_trigger(size_t i, const Schema_edit &e, Changes &changes) {
    const auto l = lookup();
    const auto [schema, trigger] = l.trigger(e.trigger);
    if (trigger == nullptr) return error(i, "unknown-object", "there is no trigger `" + e.trigger + "`");
    if (!iequals(trigger->name, e.new_name) && l.trigger(e.new_name).second != nullptr) {
      return error(i, "name-taken", "a trigger `" + e.new_name + "` exists");
    }
    const std::string old_name = trigger->name;
    string_notes(i, {old_name}, "trigger `" + old_name + "`");
    if (auto err = rename_subjects(i, "TRIGGER", old_name, schema->name, e.new_name, changes)) return err;
    for (const auto &x : schema->triggers) {
      if (!iequals(x.order_trigger, old_name) || iequals(x.name, old_name)) continue;
      const auto k = statement_of(x.origin);
      const auto r = k ? order_name_range(*k) : std::nullopt;
      if (!r) return error(i, "self-check", "the FOLLOWS or PRECEDES of trigger `" + x.name + "` is not found");
      if (!respell(changes, *r, e.new_name)) {
        return error(i, "edit-outside-slice", "the FOLLOWS or PRECEDES of trigger `" + x.name + "` cannot be changed");
      }
    }
    m_old_name = old_name;
    return std::nullopt;
  }

  std::optional<Edit_error> add_event(size_t i, const Schema_edit &e, Changes &changes) {
    const Event &ev = *e.new_event;
    if (lookup().event(ev.name).second != nullptr) return error(i, "name-taken", "an event `" + ev.name + "` exists");
    return insert_create(
        i, place_for(150, {Kind::event, Kind::trigger, Kind::routine, Kind::view, Kind::table}), Kind::event,
        Obj::event, ev.name,
        [&](const edit_text::Style &st, bool or_replace) { return edit_text::create_event_sql(ev, st, or_replace); },
        changes);
  }

  std::optional<Edit_error> drop_event(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, event] = lookup().event(e.event);
    if (event == nullptr) return error(i, "unknown-object", "there is no event `" + e.event + "`");
    const std::string name = event->name;
    std::vector<std::pair<size_t, std::string>> gone;
    for (const size_t k : statements_about("EVENT", name, schema->name)) gone.push_back({k, name});
    return drop_statements(i, gone, std::nullopt, name, "event `" + name + "`", "event", Obj::event, changes);
  }

  std::optional<Edit_error> rename_event(size_t i, const Schema_edit &e, Changes &changes) {
    const auto l = lookup();
    const auto [schema, event] = l.event(e.event);
    if (event == nullptr) return error(i, "unknown-object", "there is no event `" + e.event + "`");
    if (!iequals(event->name, e.new_name) && l.event(e.new_name).second != nullptr) {
      return error(i, "name-taken", "an event `" + e.new_name + "` exists");
    }
    const std::string old_name = event->name;
    string_notes(i, {old_name}, "event `" + old_name + "`");
    if (auto err = rename_subjects(i, "EVENT", old_name, schema->name, e.new_name, changes)) return err;
    // Event names do not tell case apart: a change of case has nothing to
    // migrate.
    if (!iequals(old_name, e.new_name)) {
      migrate(i, "ALTER EVENT " + backtick_quote(old_name) + " RENAME TO " + backtick_quote(e.new_name), Act::rename,
              Obj::event, old_name, e.new_name);
    }
    m_old_name = old_name;
    return std::nullopt;
  }


  // ---------------------------------------------------------------------
  // Sequences

  // The (table, column) pairs whose definition refers to `id`: a sequence
  // a column's default takes values from (NEXT VALUE FOR s, NEXTVAL(s)),
  // found as a rename finds references.
  std::vector<std::pair<std::string, std::string>> columns_referring_to(const Object_id &id) {
    const auto refs = references_in_text(id);
    std::vector<std::pair<std::string, std::string>> out;
    for (const Schema *s : lookup().schemas()) {
      for (const auto &t : s->tables) {
        for (const auto &c : t.columns) {
          // In its definition, or where a later ALTER TABLE sets its default.
          auto refers_in = [&](const Origin &o) {
            const auto r = searched_range(o.source, o.statement);
            return r && std::any_of(refs.begin(), refs.end(), [&](const Reference &x) {
                     return r->begin <= x.range.begin && x.range.end <= r->end;
                   });
          };
          if (refers_in(c.origin) || (c.default_origin && refers_in(*c.default_origin))) out.push_back({t.name, c.name});
        }
      }
    }
    return out;
  }

  std::optional<Edit_error> add_sequence(size_t i, const Schema_edit &e, Changes &changes) {
    const Sequence &q = *e.new_sequence;
    if (auto err = table_name_free(i, "", q.name)) return err;
    // After the last sequence, else before the first table (whose column
    // defaults may take values from it); in an MSM script in Section 140.
    Place place = place_for(140, {Kind::sequence});
    if (!place.anchor.k) place = place_for(140, {}, {Kind::table});
    const edit_text::Style st = style_of(place.anchor);
    const std::string sql = edit_text::create_sequence_sql(q, st);
    if (const auto p = parse_problem(sql)) {
      return error(i, "bad-request", "sequence `" + q.name + "` does not parse (" + *p + ")");
    }
    return insert_plain(i, place, st, sql, Obj::sequence, q.name, changes);
  }

  std::optional<Edit_error> drop_sequence(size_t i, const Schema_edit &e, Changes &changes) {
    const auto [schema, sequence] = lookup().sequence(e.sequence);
    if (sequence == nullptr) return error(i, "unknown-object", "there is no sequence `" + e.sequence + "`");
    const std::string name = sequence->name;
    const auto id = resolve(sequence->origin, Target_kind::table);
    if (!id) return error(i, "self-check", "the definition of sequence `" + name + "` cannot be resolved");
    // A CREATE TABLE or ALTER TABLE naming a sequence that does not exist
    // (a column default) fails (MariaDB error 1146): the script's, wherever
    // it is, not only the column's last definition.
    for (const auto &r : references_in_text(*id)) {
      const size_t k = index().find(r.range.begin);
      if (k >= index().statements().size()) continue;
      const auto subject = subject_of(index().text_of(text(), statement(k)));
      if (subject && subject->kind == "TABLE" && (subject->verb == "CREATE" || subject->verb == "ALTER")) {
        return error(i, "referenced", "the " + subject->verb + " TABLE of `" + subject->names[0].second +
                                          "` takes values from `" + name + "`");
      }
    }
    std::vector<std::pair<size_t, std::string>> gone;
    for (const size_t k : statements_about("SEQUENCE", name, schema->name)) gone.push_back({k, name});
    return drop_statements(i, gone, id, name, "sequence `" + name + "`", "view", Obj::sequence, changes);
  }

  std::optional<Edit_error> rename_sequence(size_t i, const Schema_edit &e, Changes &changes) {
    const Sequence *sequence = lookup().sequence(e.sequence).second;
    if (sequence == nullptr) return error(i, "unknown-object", "there is no sequence `" + e.sequence + "`");
    // The columns whose default takes values from it: verify() migrates
    // them, as the edit wrote them.
    m_default_columns.clear();
    if (const auto id = resolve(sequence->origin, Target_kind::table)) m_default_columns = columns_referring_to(*id);
    return rename_relation(i, e, "sequence", sequence->name, sequence->origin, Target_kind::table, Obj::sequence,
                           changes);
  }

  // ---------------------------------------------------------------------
  // Indexes and foreign keys (PR 3)

  // What an element of a CREATE TABLE is.
  enum class Element { column, primary, index, foreign_key, other };
  Element element_kind(const Syntax_tree &t, uint32_t item) const {
    const auto ch = t.children(item);
    if (ch.empty()) return Element::other;
    const int sym = t.node(ch[0]).symbol;
    if (sym == gram().column_def) return Element::column;
    if (sym != gram().key_def) return Element::other;
    if (t.child(ch[0], gram().FOREIGN) != k_npos) return Element::foreign_key;
    const uint32_t ckt = t.child(ch[0], gram().constraint_key_type);
    return ckt != k_npos && t.child(ckt, gram().PRIMARY_SYM) != k_npos ? Element::primary : Element::index;
  }

  // Adds `element` (a key or foreign key) to table `t`: into its CREATE
  // TABLE after the last element of the first of `after` that it has, else
  // in an ALTER TABLE ... ADD after the CREATE TABLE.
  bool add_element(Changes &changes, const Table &t, const std::string &element,
                   std::initializer_list<Element> after, const edit_text::Style &st) {
    if (auto ct = create_table_of(t); ct && !ct->items.ranges.empty()) {
      for (const Element kind : after) {
        std::optional<size_t> last;
        for (size_t j = 0; j < ct->items.nodes.size(); ++j) {
          if (element_kind(ct->parsed.tree, ct->items.nodes[j]) == kind) last = j;
        }
        if (last) return insert_item(changes, ct->items.ranges, last, element, st.eol);
      }
    }
    const auto k = statement_of(t.origin);
    const std::string sql = st.kw("ALTER TABLE") + " " + st.id(t.name) + " " + st.kw("ADD") + " " + element;
    return k && insert_statement(changes, k, laid_out(sql, st, false), st, false);
  }

  // Removes the key or constraint defined at searched range `r`: its item of
  // a CREATE TABLE or ALTER TABLE with its comma, the ALTER TABLE when that
  // was its only change, or the statement it is (CREATE INDEX).
  bool remove_element(Changes &changes, Range r, const std::string &banner) {
    const size_t k = index().find(r.begin);
    if (k >= index().statements().size()) return false;
    if (r.begin == statement(k).begin) return remove_statement(changes, k, banner).has_value();
    const auto p = parse(k);
    if (!p) return false;
    const auto &t = p->tree;
    for (const int sym : {gram().field_list_item, gram().alter_list_item}) {
      const auto items = find_all_in(t, t.roots()[0], sym);
      std::vector<Range> ranges;
      std::optional<size_t> at;
      for (size_t j = 0; j < items.size(); ++j) {
        ranges.push_back({t.begin(items[j]), t.end(items[j])});
        if (ranges.back().begin <= r.begin && r.end <= ranges.back().end) at = j;
      }
      if (!at) continue;
      if (items.size() == 1) {
        return sym == gram().alter_list_item && remove_statement(changes, k, banner).has_value();
      }
      std::vector<Range> removed;
      return remove_items(changes, ranges, {*at}, &removed);
    }
    return false;
  }

  // The parsed statement at searched `r` and its first `sym` node whose
  // range `fits(begin, end)` accepts.
  template <typename Fits>
  std::optional<std::pair<Parsed, uint32_t>> node_at(Range r, int sym, Fits fits) {
    const size_t k = index().find(r.begin);
    if (k >= index().statements().size()) return std::nullopt;
    auto p = parse(k);
    if (!p) return std::nullopt;
    for (const uint32_t n : find_all_in(p->tree, p->tree.roots()[0], sym)) {
      if (fits(p->tree.begin(n), p->tree.end(n))) return std::make_pair(std::move(*p), n);
    }
    return std::nullopt;
  }

  // Removes the PRIMARY KEY (`primary`) or UNIQUE attribute of the column
  // whose field_spec is at searched `r`.
  bool remove_key_attribute(Changes &changes, Range r, bool primary) {
    const auto at = node_at(r, gram().field_spec, [&](uint32_t b, uint32_t) { return b == r.begin; });
    if (!at) return false;
    const auto &t = at->first.tree;
    // find_all_in descends into asrow_attribute, where a vcol_attribute may sit.
    const int attribute = primary ? gram().asrow_attribute : gram().vcol_attribute;
    const int keyword = primary ? gram().KEY_SYM : gram().UNIQUE_SYM;
    std::vector<uint32_t> nodes;
    for (const uint32_t n : find_all_in(t, at->second, attribute)) {
      if (t.child(n, keyword) != k_npos) nodes.push_back(n);
    }
    if (nodes.size() != 1) return false;
    uint32_t b = t.begin(nodes[0]);
    while (b > r.begin && (text()[b - 1] == ' ' || text()[b - 1] == '\t')) --b;
    return change(changes, b, t.end(nodes[0]), "");
  }

  // Whether `ix` is a column's PRIMARY KEY or UNIQUE attribute: it carries
  // that column's origin.
  const Column *column_level(const Table &t, const Index &ix) const {
    for (const auto &c : t.columns) {
      if (ix.origin.source == c.origin.source && ix.origin.statement.begin == c.origin.statement.begin &&
          ix.origin.statement.end == c.origin.statement.end) {
        return &c;
      }
    }
    return nullptr;
  }

  // Whether an index on `index` serves a foreign key on `fk`: those are its
  // leading columns.
  static bool serves(const std::vector<std::string> &index, const std::vector<std::string> &fk) {
    return fk.size() <= index.size() && std::equal(fk.begin(), fk.end(), index.begin(), [](const auto &x, const auto &y) {
             return iequals(x, y);
           });
  }
  static bool same_columns(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    return a.size() == b.size() && serves(a, b);
  }

  // What would lose the index it needs if `table`'s index `gone` went
  // (replaced by an index on `instead`, when given): a foreign key (MariaDB
  // refuses to drop the index, 1553), or an AUTO_INCREMENT column, which
  // must lead an index (1075).
  std::optional<Edit_error> index_needed(size_t i, const Table &table, const Index *gone,
                                         const std::vector<std::string> &instead) {
    auto served = [&](const std::vector<std::string> &cols) {
      if (!instead.empty() && serves(instead, cols)) return true;
      for (const auto &ix : table.indexes) {
        if (&ix != gone && serves(ix.columns(), cols)) return true;
      }
      return false;
    };
    for (const auto &c : table.columns) {
      if (c.auto_increment && serves(gone->columns(), {c.name}) && !served({c.name})) {
        return error(i, "referenced", "the AUTO_INCREMENT column `" + c.name + "` of `" + table.name +
                                          "` must lead an index, and this is the one it leads");
      }
    }
    for (const Schema *s : lookup().schemas()) {
      for (const auto &x : s->tables) {
        for (const auto &fk : x.foreign_keys) {
          const bool own = &x == &table;
          const bool refs = iequals(fk.ref_table, table.name);
          if (own && serves(gone->columns(), fk.columns) && !served(fk.columns)) {
            return error(i, "referenced", "the foreign key " + (fk.name.empty() ? "" : "`" + fk.name + "` ") +
                                              "of `" + x.name + "` needs this index");
          }
          if (refs && serves(gone->columns(), fk.ref_columns) && !served(fk.ref_columns)) {
            return error(i, "referenced", "the foreign key " + (fk.name.empty() ? "" : "`" + fk.name + "` ") +
                                              "of `" + x.name + "` references the columns of this index");
          }
        }
      }
    }
    return std::nullopt;
  }

  std::optional<Edit_error> add_index(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Index &ix = *e.new_index;
    for (const auto &kp : ix.key_parts) {
      if (table->column(kp.column) == nullptr) {
        return error(i, "unknown-object", "table `" + table->name + "` has no column `" + kp.column + "`");
      }
    }
    for (const auto &x : table->indexes) {
      if (ix.kind == "primary" && x.kind == "primary") {
        return error(i, "name-taken", "table `" + table->name + "` has a primary key: use set_primary_key");
      }
      if (!ix.name.empty() && iequals(x.name, ix.name)) {
        return error(i, "name-taken", "table `" + table->name + "` has an index `" + ix.name + "`");
      }
    }
    const edit_text::Style st = style_of(table->origin);
    const std::string sql = edit_text::index_sql(ix, st);
    if (const auto p = parse_problem("CREATE TABLE x (a INT, " + sql + ")")) {
      return error(i, "bad-request", "the index does not parse (" + *p + ")");
    }
    m_count = table->indexes.size();
    const bool primary = ix.kind == "primary";
    const bool ok = primary ? add_element(changes, *table, sql, {Element::column}, st)
                            : add_element(changes, *table, sql, {Element::index, Element::primary, Element::column}, st);
    if (!ok) return error(i, "self-check", "no place for the index was found in `" + table->name + "`");
    migrate_member(i, table->name, "ADD " + edit_text::index_sql(ix, {}), Act::add,
                   primary ? Obj::primary_key : Obj::index, ix.name);
    return std::nullopt;
  }

  // The one item of `table`'s `items` that `pick` accepts; or an error
  // naming `what`.
  template <typename T, typename Pick>
  std::variant<const T *, Edit_error> the_one(size_t i, const Table &table, const std::vector<T> &items, Pick pick,
                                              const std::string &what) const {
    const T *found = nullptr;
    for (const auto &x : items) {
      if (!pick(x)) continue;
      if (found != nullptr) {
        return error(i, "bad-request", "table `" + table.name + "` has more than one " + what);
      }
      found = &x;
    }
    if (found == nullptr) return error(i, "unknown-object", "table `" + table.name + "` has no " + what);
    return found;
  }

  // The index of `table` that drop_index names; or an error.
  std::variant<const Index *, Edit_error> index_named(size_t i, const Table &table, const Schema_edit &e) const {
    const std::string what = !e.name.empty() ? "an index `" + e.name + "`"
                             : !e.kind.empty() ? std::string("a primary key")
                                               : "an unnamed index on (" + join(e.columns) + ")";
    return the_one(
        i, table, table.indexes,
        [&](const Index &x) {
          return !e.name.empty()   ? iequals(x.name, e.name)
                 : !e.kind.empty() ? x.kind == "primary"
                                   : x.name.empty() && same_columns(x.columns(), e.columns);
        },
        what);
  }

  // Removes index `ix` of `table` from where it is defined.
  bool remove_index(Changes &changes, const Table &table, const Index &ix) {
    const auto r = searched_range(ix.origin.source, ix.origin.statement);
    if (!r) return false;
    if (column_level(table, ix) != nullptr) return remove_key_attribute(changes, *r, ix.kind == "primary");
    return remove_element(changes, *r, table.name);
  }

  std::optional<Edit_error> drop_index(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const auto found = index_named(i, *table, e);
    if (const auto *err = std::get_if<Edit_error>(&found)) return *err;
    const Index &ix = *std::get<const Index *>(found);
    if (auto err = index_needed(i, *table, &ix, {})) return err;
    m_count = table->indexes.size();
    if (!remove_index(changes, *table, ix)) {
      return error(i, "self-check", "the index cannot be removed from `" + table->name + "`");
    }
    if (ix.kind == "primary") {
      migrate_member(i, table->name, "DROP PRIMARY KEY", Act::drop, Obj::primary_key, {});
    } else if (!ix.name.empty()) {
      migrate_member(i, table->name, "DROP INDEX " + backtick_quote(ix.name), Act::drop, Obj::index, ix.name);
    } else {
      note(i, "no-migration", table->origin.source, table->origin.statement.begin,
           "an unnamed index of `" + table->name + "` was removed: the server's name for it is not known");
    }
    return std::nullopt;
  }

  std::optional<Edit_error> set_primary_key(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    for (const auto &c : e.columns) {
      if (table->column(c) == nullptr) {
        return error(i, "unknown-object", "table `" + table->name + "` has no column `" + c + "`");
      }
    }
    const Index *old = nullptr;
    for (const auto &x : table->indexes) {
      if (x.kind == "primary") old = &x;
    }
    if (old != nullptr && same_columns(old->columns(), e.columns)) return std::nullopt;  // as it is
    if (old != nullptr) {
      if (auto err = index_needed(i, *table, old, e.columns)) return err;
    }
    const edit_text::Style st = style_of(table->origin);
    Index pk;
    pk.kind = "primary";
    for (const auto &c : e.columns) pk.key_parts.emplace_back().column = c;
    bool ok = false;
    if (old != nullptr && column_level(*table, *old) == nullptr) {
      // The key's columns where it is written: its other attributes stay.
      const auto r = searched_range(old->origin.source, old->origin.statement);
      const auto at = r ? node_at(*r, gram().key_def, [&](uint32_t b, uint32_t en) { return b <= r->begin && r->end <= en; })
                        : std::nullopt;
      if (at) {
        const auto &t = at->first.tree;
        const uint32_t list = child_of(t, at->second, gram().key_list);
        ok = list != k_npos && change(changes, t.begin(list), t.end(list), edit_text::ids(e.columns, st));
      }
    } else {
      ok = old == nullptr || remove_index(changes, *table, *old);
      ok = ok && add_element(changes, *table, edit_text::index_sql(pk, st), {Element::column}, st);
    }
    if (!ok) return error(i, "self-check", "the primary key of `" + table->name + "` cannot be written");
    // One statement: a DROP PRIMARY KEY of its own fails when an
    // AUTO_INCREMENT column is the key.
    migrate_member(i, table->name, (old != nullptr ? "DROP PRIMARY KEY, ADD " : "ADD ") + edit_text::index_sql(pk, {}),
                   old != nullptr ? Act::alter : Act::add, Obj::primary_key, {});
    return std::nullopt;
  }

  std::optional<Edit_error> add_foreign_key(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const Foreign_key &fk = *e.new_foreign_key;
    if (auto err = check_foreign_key(i, *table, fk)) return err;
    const edit_text::Style st = style_of(table->origin);
    const std::string sql = edit_text::foreign_key_sql(fk, st);
    if (const auto p = parse_problem("CREATE TABLE x (a INT, " + sql + ")")) {
      return error(i, "bad-request", "the foreign key does not parse (" + *p + ")");
    }
    m_count = table->foreign_keys.size();
    // A table the script creates after this one is referenced from an
    // ALTER TABLE after both.
    const Table *ref = fk.ref_schema.empty() ? lookup().table(fk.ref_table).second : nullptr;
    const auto k_table = statement_of(table->origin);
    const auto k_ref = ref != nullptr && ref != table ? statement_of(ref->origin) : std::nullopt;
    bool ok;
    if (k_table && k_ref && *k_ref > *k_table) {
      // After the last ALTER TABLE ... FOREIGN KEY after both, else after the
      // referenced table.
      size_t after = *k_ref;
      const std::vector<const Schema *> schemas = lookup().schemas();
      for (const Schema *h : schemas) {
        for (const auto &t : h->tables) {
          for (const auto &other : t.foreign_keys) {
            const auto k = statement_of(other.origin);
            if (!k || *k <= after) continue;
            const auto subject = subject_of(index().text_of(text(), statement(*k)));
            if (subject && subject->verb == "ALTER") after = *k;
          }
        }
      }
      const std::string alter = st.kw("ALTER TABLE") + " " + st.id(table->name) + " " + st.kw("ADD") + " " + sql;
      ok = insert_statement(changes, after, laid_out(alter, st, false), st, false).has_value();
    } else {
      ok = add_element(changes, *table, sql, {Element::foreign_key, Element::index, Element::primary, Element::column},
                       st);
    }
    if (!ok) return error(i, "self-check", "no place for the foreign key was found in `" + table->name + "`");
    migrate_add_foreign_key(i, table->name, fk);
    return std::nullopt;
  }

  std::optional<Edit_error> drop_foreign_key(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    const std::string what = !e.name.empty() ? "foreign key `" + e.name + "`"
                                             : "unnamed foreign key on (" + join(e.columns) + ")";
    const auto found = the_one(
        i, *table, table->foreign_keys,
        [&](const Foreign_key &x) {
          return !e.name.empty() ? iequals(x.name, e.name) : x.name.empty() && same_columns(x.columns, e.columns);
        },
        what);
    if (const auto *err = std::get_if<Edit_error>(&found)) return *err;
    const Foreign_key &fk = *std::get<const Foreign_key *>(found);
    m_count = table->foreign_keys.size();
    const auto r = searched_range(fk.origin.source, fk.origin.statement);
    bool ok = false;
    if (r) {
      // A column's REFERENCES (the origin is its column_def): from the end
      // of its field_spec to the end of the column.
      const auto at =
          node_at(*r, gram().column_def, [&](uint32_t b, uint32_t en) { return b == r->begin && en == r->end; });
      if (at) {
        const uint32_t spec = child_of(at->first.tree, at->second, gram().field_spec);
        ok = spec != k_npos && change(changes, at->first.tree.end(spec), r->end, "");
      } else {
        ok = remove_element(changes, *r, fk.name.empty() ? table->name : fk.name);
      }
    }
    if (!ok) return error(i, "self-check", "the " + what + " cannot be removed from `" + table->name + "`");
    if (!fk.name.empty()) {
      migrate_drop_foreign_key(i, table->name, fk.name);
    } else {
      note(i, "no-migration", fk.origin.source, fk.origin.statement.begin,
           "an unnamed foreign key of `" + table->name + "` was removed: the server's name for it is not known");
    }
    return std::nullopt;
  }

  // The changes of one edit, sorted; the same change twice (a file
  // included twice) once, overlapping removals as one.
  std::optional<Edit_error> normalize(size_t i, Changes &changes) {
    for (auto &[file, v] : changes) {
      std::stable_sort(v.begin(), v.end(), [](const Text_edit &a, const Text_edit &b) {
        return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
      });
      std::vector<Text_edit> out;
      for (auto &x : v) {
        if (!out.empty()) {
          Text_edit &y = out.back();
          if (x.begin == y.begin && x.end == y.end && x.text == y.text) continue;
          if (x.begin < y.end) {
            if (x.text.empty() && y.text.empty()) {
              y.end = std::max(y.end, x.end);
              continue;
            }
            return error(i, "self-check", "two changes of the edit overlap");
          }
        }
        out.push_back(std::move(x));
      }
      v = std::move(out);
    }
    return std::nullopt;
  }

  // No statement renames a routine or a trigger: the migration drops it
  // and creates it again, with the CREATE as it runs, from the expanded text
  // (a body that includes a file holds that file's text), and a note that
  // the CREATE cannot run inside a stored procedure.  `after`: triggers
  // dropped with it and created again after it, in that order.
  std::optional<Edit_error> migrate_recreate(size_t i, Obj o, const Origin &origin, const std::string &old_name,
                                             const std::string &new_name,
                                             const std::vector<const Trigger *> &after = {}) {
    // Every CREATE first: nothing is migrated when one is not found.
    std::vector<const Origin *> origins{&origin};
    for (const Trigger *t : after) origins.push_back(&t->origin);
    std::vector<std::string> sqls;
    for (const Origin *x : origins) {
      const auto k = statement_of(*x);
      if (!k) return error(i, "self-check", "the CREATE of `" + new_name + "` or one fired after it is not found");
      sqls.emplace_back(trim(index().text_of(text(), statement(*k))));
    }
    const std::string noun = to_string(o);
    migrate_drop(i, o, old_name);
    for (const Trigger *x : after) migrate_drop(i, Obj::trigger, x->name);
    migrate(i, sqls[0], Act::create, o, new_name);
    note(i, o == Obj::trigger ? "trigger-recreated" : "routine-recreated", origin.source, origin.statement.begin,
         "no statement renames a " + noun + ": the migration drops `" + old_name + "` and creates `" + new_name +
             "`, which cannot run inside a stored procedure");
    for (size_t n = 0; n < after.size(); ++n) {
      const Trigger *x = after[n];
      migrate(i, sqls[n + 1], Act::create, Obj::trigger, x->name);
      note(i, "trigger-recreated", x->origin.source, x->origin.statement.begin,
           "the migration drops and creates the trigger `" + x->name + "` again after `" + new_name +
               "`, to keep the order in which they fire; its CREATE cannot run inside a stored procedure");
    }
    return std::nullopt;
  }

  // The triggers that fire after trigger `t` of schema `s` because the
  // script creates them later (on its table, at its timing, on an event it
  // or one of them fires on): created again after it, they keep their order.
  std::vector<const Trigger *> fired_after(const Schema &s, const Trigger &t) const {
    const auto kt = statement_of(t.origin);
    if (!kt) return {};
    std::vector<std::pair<size_t, const Trigger *>> later;
    for (const auto &x : s.triggers) {
      if (!iequals(x.table, t.table) || !iequals(x.timing, t.timing)) continue;
      const auto k = statement_of(x.origin);
      if (k && *k > *kt) later.push_back({*k, &x});
    }
    std::sort(later.begin(), later.end());
    std::set<std::string> events = event_set(t.events);
    std::vector<const Trigger *> out;
    for (const auto &[k, x] : later) {
      const auto theirs = event_set(x->events);
      if (std::none_of(theirs.begin(), theirs.end(), [&](const std::string &ev) { return events.count(ev) != 0; })) {
        continue;
      }
      events.insert(theirs.begin(), theirs.end());
      out.push_back(x);
    }
    return out;
  }

  // The edited outline shows the edit.
  std::optional<Edit_error> verify(size_t i, const Schema_edit &e) {
    const auto l = lookup();
    const std::string &old_name = m_old_name;
    const bool case_only = iequals(old_name, e.new_name);
    auto fail = [&](const std::string &what) { return error(i, "self-check", what); };
    // A key op's table has one item more (`added`, one that `is_new`
    // accepts) or one fewer than before.
    auto check_keys = [&](const Table &t, const auto &items, const std::string &noun,
                          const std::string &nouns, bool added,
                          auto is_new) -> std::optional<Edit_error> {
      const size_t want = added ? m_count + 1 : m_count - 1;
      if (items.size() != want) {
        return fail("table `" + t.name + "` has " + std::to_string(items.size()) + " " + nouns + ", not " +
                    std::to_string(want));
      }
      if (added && std::none_of(items.begin(), items.end(), is_new)) {
        return fail("table `" + t.name + "` does not have the " + noun + " added");
      }
      return std::nullopt;
    };
    switch (e.op) {
      case Edit_op::rename_table:
      case Edit_op::rename_view: {
        const bool is_table = e.op == Edit_op::rename_table;
        const Table *t = l.table(e.new_name).second;
        const View *v = l.view(e.new_name).second;
        const std::string got = is_table ? (t ? t->name : "") : (v ? v->name : "");
        if (got != e.new_name) return fail("the edited script has no " + std::string(is_table ? "table" : "view") + " `" + e.new_name + "`");
        if (!case_only && l.relation(old_name)) return fail("the edited script still has `" + old_name + "`");
        if (case_only) break;
        for (const Schema *sc : l.schemas()) {
          for (const auto &tb : sc->tables) {
            for (const auto &fk : tb.foreign_keys) {
              if (iequals(fk.ref_table, old_name) && (fk.ref_schema.empty() || iequals(fk.ref_schema, sc->name))) {
                return fail("a foreign key of `" + tb.name + "` still references `" + old_name + "`");
              }
            }
          }
          for (const auto &tr : sc->triggers) {
            if (iequals(tr.table, old_name)) return fail("trigger `" + tr.name + "` is still on `" + old_name + "`");
          }
        }
        break;
      }
      case Edit_op::rename_column: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr) return fail("the edited script has no table `" + e.table + "`");
        const Column *c = t->column(e.new_name);
        if (c == nullptr || c->name != e.new_name) return fail("table `" + t->name + "` has no column `" + e.new_name + "`");
        if (case_only) break;
        if (t->column(old_name) != nullptr) return fail("table `" + t->name + "` still has a column `" + old_name + "`");
        for (const auto &ix : t->indexes) {
          for (const auto &kp : ix.key_parts) {
            if (iequals(kp.column, old_name)) return fail("an index of `" + t->name + "` still names `" + old_name + "`");
          }
        }
        for (const Schema *sc : l.schemas()) {
          for (const auto &tb : sc->tables) {
            for (const auto &fk : tb.foreign_keys) {
              if (&tb == t && names(fk.columns, old_name)) {
                return fail("a foreign key of `" + tb.name + "` still names `" + old_name + "`");
              }
              if (iequals(fk.ref_table, t->name) && names(fk.ref_columns, old_name)) {
                return fail("a foreign key of `" + tb.name + "` still references `" + old_name + "`");
              }
            }
          }
        }
        break;
      }
      case Edit_op::rename_routine: {
        const std::string &kind = m_routine_kind;
        const auto now = l.routines(e.new_name, kind);
        if (now.size() != 1 || now[0].second->name != e.new_name) {
          return fail("the edited script has no " + kind + " `" + e.new_name + "`");
        }
        if (!case_only && !l.routines(old_name, kind).empty()) {
          return fail("the edited script still has the " + kind + " `" + old_name + "`");
        }
        if (old_name == e.new_name) break;  // the same name again: nothing to migrate
        if (auto err = migrate_recreate(i, routine_object(kind), now[0].second->origin, old_name, e.new_name)) return err;
        break;
      }
      case Edit_op::add_table: {
        const Table &want = *e.new_table;
        const Table *t = l.table(want.name).second;
        if (t == nullptr || t->name != want.name) return fail("the edited script has no table `" + want.name + "`");
        if (t->columns.size() != want.columns.size()) return fail("table `" + want.name + "` lacks columns");
        for (size_t k = 0; k < want.columns.size(); ++k) {
          if (t->columns[k].name != want.columns[k].name) return fail("table `" + want.name + "` has its columns in another order");
        }
        if (t->indexes.size() != want.indexes.size()) return fail("table `" + want.name + "` lacks indexes");
        if (t->foreign_keys.size() != want.foreign_keys.size()) return fail("table `" + want.name + "` lacks foreign keys");
        break;
      }
      case Edit_op::drop_table:
        if (l.table(old_name).second != nullptr) return fail("the edited script still has table `" + old_name + "`");
        break;
      case Edit_op::add_column: {
        const Table *t = l.table(e.table).second;
        const std::string &name = e.new_column->name;
        if (t == nullptr || m_position >= t->columns.size() || t->columns[m_position].name != name) {
          return fail("table `" + e.table + "` has no column `" + name + "` where it was added");
        }
        if (t->columns.size() != m_columns + 1) return fail("table `" + e.table + "` changed more than the column added");
        break;
      }
      case Edit_op::modify_column: {
        const Column &want = *e.new_column;
        const Table *t = l.table(e.table).second;
        const Column *c = t == nullptr ? nullptr : t->column(want.name);
        if (c == nullptr || c->name != want.name) return fail("table `" + e.table + "` has no column `" + want.name + "`");
        if (!iequals(collapse(c->type), collapse(want.type))) return fail("column `" + want.name + "` has the type " + c->type);
        if (c->nullable != want.nullable && !(m_in_primary && !c->nullable)) {
          return fail("column `" + want.name + "` is " + (c->nullable ? "" : "not ") + "nullable");
        }
        if (c->default_value.has_value() != want.default_value.has_value() ||
            (c->default_value && !iequals(collapse(*c->default_value), collapse(*want.default_value)))) {
          return fail("column `" + want.name + "` has another default");
        }
        if (c->comment != want.comment) return fail("column `" + want.name + "` has another comment");
        if (!iequals(want.name, old_name)) {
          if (t->column(old_name) != nullptr) return fail("table `" + t->name + "` still has a column `" + old_name + "`");
          for (const auto &ix : t->indexes) {
            if (names(ix.columns(), old_name)) return fail("an index of `" + t->name + "` still names `" + old_name + "`");
          }
        }
        break;
      }
      case Edit_op::drop_column: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr || t->column(old_name) != nullptr) {
          return fail("table `" + e.table + "` still has the column `" + old_name + "`");
        }
        // A column an earlier definition had (CHANGE a b) must not come back.
        if (t->columns.size() + 1 != m_columns) return fail("table `" + t->name + "` lost or kept other columns");
        for (const auto &ix : t->indexes) {
          if (names(ix.columns(), old_name)) return fail("an index of `" + t->name + "` still names `" + old_name + "`");
        }
        break;
      }
      case Edit_op::set_table_options: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr) return fail("the edited script has no table `" + e.table + "`");
        for (const auto &[key, value] : e.options) {
          const std::string *got = nullptr;
          for (const auto &[k, v] : t->options) {
            if (k == key) got = &v;
          }
          if (!value && got != nullptr) return fail("table `" + t->name + "` still has the option " + key);
          if (value && (got == nullptr || (key == "comment" ? *got != *value : !iequals(*got, *value)))) {
            return fail("table `" + t->name + "` does not have the option " + key + " set");
          }
        }
        break;
      }
      case Edit_op::add_index:
      case Edit_op::drop_index: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr) return fail("the edited script has no table `" + e.table + "`");
        const bool added = e.op == Edit_op::add_index;
        if (auto err = check_keys(*t, t->indexes, "index", "indexes", added, [&](const Index &x) {
              const Index &ix = *e.new_index;
              return x.kind == ix.kind && same_columns(x.columns(), ix.columns()) &&
                     (ix.name.empty() || iequals(x.name, ix.name));
            })) {
          return err;
        }
        break;
      }
      case Edit_op::add_foreign_key:
      case Edit_op::drop_foreign_key: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr) return fail("the edited script has no table `" + e.table + "`");
        const bool added = e.op == Edit_op::add_foreign_key;
        if (auto err = check_keys(*t, t->foreign_keys, "foreign key", "foreign keys", added, [&](const Foreign_key &x) {
              const Foreign_key &fk = *e.new_foreign_key;
              return same_columns(x.columns, fk.columns) && iequals(x.ref_table, fk.ref_table) &&
                     (fk.name.empty() || iequals(x.name, fk.name));
            })) {
          return err;
        }
        break;
      }
      case Edit_op::set_primary_key: {
        const Table *t = l.table(e.table).second;
        if (t == nullptr) return fail("the edited script has no table `" + e.table + "`");
        const auto primaries = std::count_if(t->indexes.begin(), t->indexes.end(),
                                             [](const Index &x) { return x.kind == "primary"; });
        const auto right = std::count_if(t->indexes.begin(), t->indexes.end(), [&](const Index &x) {
          return x.kind == "primary" && same_columns(x.columns(), e.columns);
        });
        if (primaries != 1 || right != 1) return fail("the primary key of `" + t->name + "` is not on the columns given");
        break;
      }
      case Edit_op::add_view: {
        const View *v = l.view(e.new_view->name).second;
        if (v == nullptr || v->name != e.new_view->name) return fail("the edited script has no view `" + e.new_view->name + "`");
        break;
      }
      case Edit_op::drop_view:
        if (l.view(old_name).second != nullptr) return fail("the edited script still has view `" + old_name + "`");
        break;
      case Edit_op::add_routine: {
        const auto now = l.routines(e.new_routine->name, e.new_routine->kind);
        if (now.size() != 1 || now[0].second->name != e.new_routine->name) {
          return fail("the edited script has no " + e.new_routine->kind + " `" + e.new_routine->name + "`");
        }
        break;
      }
      case Edit_op::drop_routine:
        if (!l.routines(old_name, m_routine_kind).empty()) {
          return fail("the edited script still has the " + m_routine_kind + " `" + old_name + "`");
        }
        break;
      case Edit_op::add_trigger: {
        const Trigger &want = *e.new_trigger;
        const Trigger *t = l.trigger(want.name).second;
        if (t == nullptr || t->name != want.name || !iequals(t->table, want.table)) {
          return fail("the edited script has no trigger `" + want.name + "` on `" + want.table + "`");
        }
        break;
      }
      case Edit_op::drop_trigger:
        if (l.trigger(old_name).second != nullptr) return fail("the edited script still has trigger `" + old_name + "`");
        break;
      case Edit_op::rename_trigger: {
        const auto [sc, t] = l.trigger(e.new_name);
        if (t == nullptr || t->name != e.new_name) return fail("the edited script has no trigger `" + e.new_name + "`");
        if (!case_only) {
          if (l.trigger(old_name).second != nullptr) {
            return fail("the edited script still has trigger `" + old_name + "`");
          }
          for (const Schema *s : l.schemas()) {
            for (const auto &x : s->triggers) {
              if (iequals(x.order_trigger, old_name)) {
                return fail("trigger `" + x.name + "` still follows or precedes `" + old_name + "`");
              }
            }
          }
        }
        if (old_name == e.new_name) break;  // the same name again: nothing to migrate
        if (auto err = migrate_recreate(i, Obj::trigger, t->origin, old_name, e.new_name, fired_after(*sc, *t))) {
          return err;
        }
        break;
      }
      case Edit_op::add_event: {
        const Event *ev = l.event(e.new_event->name).second;
        if (ev == nullptr || ev->name != e.new_event->name) {
          return fail("the edited script has no event `" + e.new_event->name + "`");
        }
        break;
      }
      case Edit_op::drop_event:
        if (l.event(old_name).second != nullptr) return fail("the edited script still has event `" + old_name + "`");
        break;
      case Edit_op::rename_event: {
        const Event *ev = l.event(e.new_name).second;
        if (ev == nullptr || ev->name != e.new_name) return fail("the edited script has no event `" + e.new_name + "`");
        if (!case_only && l.event(old_name).second != nullptr) {
          return fail("the edited script still has event `" + old_name + "`");
        }
        break;
      }
      case Edit_op::add_sequence: {
        const Sequence *q = l.sequence(e.new_sequence->name).second;
        if (q == nullptr || q->name != e.new_sequence->name) {
          return fail("the edited script has no sequence `" + e.new_sequence->name + "`");
        }
        break;
      }
      case Edit_op::drop_sequence:
        if (l.sequence(old_name).second != nullptr) return fail("the edited script still has sequence `" + old_name + "`");
        break;
      case Edit_op::rename_sequence: {
        const Sequence *q = l.sequence(e.new_name).second;
        if (q == nullptr || q->name != e.new_name) return fail("the edited script has no sequence `" + e.new_name + "`");
        if (!case_only && l.sequence(old_name).second != nullptr) {
          return fail("the edited script still has sequence `" + old_name + "`");
        }
        if (old_name == e.new_name) break;  // the same name again: nothing to migrate
        // A deployed column default still names the old sequence after
        // RENAME TABLE: set it again, as the edit wrote it.
        for (const auto &[table, column] : m_default_columns) {
          const Table *t = l.table(table).second;
          const Column *c = t == nullptr ? nullptr : t->column(column);
          // A later ALTER TABLE dropped the default (or the column): the
          // deployed column no longer names the sequence.
          if (c == nullptr || !c->default_value) continue;
          migrate_member(i, table, "ALTER COLUMN " + backtick_quote(column) + " SET DEFAULT " + *c->default_value,
                         Act::alter, Obj::column, column);
        }
        break;
      }
      default:
        break;
    }
    return std::nullopt;
  }

  Session &m_s;
  const Edit_request &m_request;
  Edit_result &m_result;
  // Of the edit being run, for verify().
  std::string m_old_name;
  std::string m_routine_kind;
  std::vector<std::pair<std::string, std::string>> m_default_columns;  // rename_sequence: (table, column)
  size_t m_position = 0;     // add_column: where the column goes
  size_t m_columns = 0;      // add_column, drop_column: the table's columns before
  size_t m_count = 0;        // add_/drop_index, add_/drop_foreign_key: the table's keys before
  bool m_in_primary = false; // modify_column: the column is in the primary key
};

}  // namespace

std::variant<Edit_result, Edit_error> apply_schema_edits(std::string_view script,
                                                         const Edit_request &request,
                                                         Parser_pool &pool,
                                                         const Edit_options &options) {
  Session s(script, pool, options);
  s.build();
  s.record_selections();
  // The notes the input already has; the edited texts may have no others.
  std::multiset<std::string> before;
  for (const auto &n : s.outline().notes) {
    if (!n.applied) before.insert(note_key(n));
  }

  Edit_result result;
  Editor editor(s, request, result);
  for (size_t i = 0; i < request.edits.size(); ++i) {
    if (auto err = editor.run(i)) return *err;
  }
  for (const auto &n : s.outline().notes) {
    if (n.applied) continue;
    const auto it = before.find(note_key(n));
    if (it == before.end()) {
      return error(std::nullopt, "self-check", "the edited script has a statement that is not read: " + n.message);
    }
    before.erase(it);
  }

  for (auto &f : s.files()) {
    Edited_file out;
    out.path = f.path;
    out.original = f.log.original();
    out.text = f.log.text();
    out.edits = f.log.edits();
    result.files.push_back(std::move(out));
  }
  result.outline = std::move(*s.outline_ptr());
  return result;
}

// ---------------------------------------------------------------------------
// Output

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

void str(Writer &w, const char *key, std::string_view v) {
  w.Key(key);
  w.String(v.data(), static_cast<rapidjson::SizeType>(v.size()));
}

std::string file_name(const Edited_file &f, std::string_view script_file, const std::string &source_dir) {
  if (f.path.empty()) return std::string(script_file);
  if (source_dir.empty()) return f.path;
  const auto rel = std::filesystem::path(f.path).lexically_relative(std::filesystem::path(source_dir));
  return rel.empty() ? f.path : rel.generic_string();
}

}  // namespace

std::string edit_result_json(const Edit_result &result, std::string_view script_file,
                             const std::string &source_dir) {
  rapidjson::StringBuffer buf;
  Writer w(buf);
  std::vector<Line_map> lines;
  lines.reserve(result.files.size());
  for (const auto &f : result.files) lines.emplace_back(f.original);
  w.StartObject();
  w.Key("format");
  w.Uint(1);
  w.Key("files");
  w.StartArray();
  for (size_t k = 0; k < result.files.size(); ++k) {
    const auto &f = result.files[k];
    if (f.edits.empty()) continue;
    w.StartObject();
    str(w, "file", file_name(f, script_file, source_dir));
    w.Key("edits");
    w.StartArray();
    for (const auto &e : f.edits) {
      w.StartObject();
      w.Key("range");
      lsp::write_range(w, f.original, lines[k], e.begin, e.end);
      str(w, "new_text", e.text);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.Key("migration");
  w.StartArray();
  for (const auto &m : result.migration) {
    w.StartObject();
    w.Key("edit");
    w.Uint64(m.edit);
    str(w, "sql", m.sql);
    str(w, "action", to_string(m.action));
    str(w, "object", to_string(m.object));
    str(w, "name", m.name);
    if (!m.table.empty()) str(w, "table", m.table);
    if (!m.new_name.empty()) str(w, "new_name", m.new_name);
    w.EndObject();
  }
  w.EndArray();
  w.Key("notes");
  w.StartArray();
  for (const auto &n : result.notes) {
    w.StartObject();
    w.Key("edit");
    w.Uint64(n.edit);
    str(w, "kind", n.kind);
    str(w, "file", file_name(result.files[n.file], script_file, source_dir));
    if (n.include) {
      w.Key("source");
      w.Uint64(*n.include);
    }
    w.Key("start");
    const auto &f = result.files[n.file];
    lsp::write_position(w, lines[n.file].position(f.original, n.at));
    str(w, "message", n.message);
    w.EndObject();
  }
  w.EndArray();
  w.Key("outline");
  const std::string outline = outline_json(result.outline, result.files[0].text,
                                           Line_map(result.files[0].text), script_file);
  w.RawValue(outline.data(), outline.size(), rapidjson::kObjectType);
  w.EndObject();
  return buf.GetString();
}

std::string edit_error_json(const Edit_error &e) {
  rapidjson::StringBuffer buf;
  Writer w(buf);
  w.StartObject();
  w.Key("error");
  w.StartObject();
  w.Key("edit");
  if (e.edit) w.Uint64(*e.edit);
  else w.Null();
  str(w, "kind", e.kind);
  str(w, "message", e.message);
  w.EndObject();
  w.EndObject();
  return buf.GetString();
}

}  // namespace mdbls
