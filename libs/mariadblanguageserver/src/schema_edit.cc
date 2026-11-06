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

#include "mdbls/catalog_view.h"
#include "mdbls/keywords.h"
#include "mdbls/lsp/json_position.h"
#include "mdbls/navigation.h"
#include "mdbls/sql_names.h"
#include "mdbls/workspace_catalog.h"
#include "mdbparse.h"
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
    if (!op) return error(i, "bad-request", std::string("unknown op \"") + e["op"].GetString() + "\"");
    Schema_edit se;
    se.op = *op;
    std::optional<Edit_error> bad;
    auto name = [&](const char *member, std::string *to, bool required = true) {
      if (bad) return;
      if (!e.HasMember(member) || e[member].IsNull()) {
        if (required) bad = error(i, "bad-request", std::string("the edit has no \"") + member + "\"");
        return;
      }
      if (!e[member].IsString()) {
        bad = error(i, "bad-request", std::string("\"") + member + "\" is not a string");
        return;
      }
      *to = std::string(e[member].GetString(), e[member].GetStringLength());
      if (required && to->empty()) bad = error(i, "bad-request", std::string("\"") + member + "\" is empty");
    };
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
        name("kind", &se.routine_kind, false);
        name("new_name", &se.new_name);
        if (!bad && !se.routine_kind.empty() && se.routine_kind != "procedure" &&
            se.routine_kind != "function") {
          bad = error(i, "bad-request", "\"kind\" is procedure or function");
        }
        break;
      default:
        break;  // apply_schema_edits() answers unsupported
    }
    if (bad) return *bad;
    if (!se.new_name.empty()) {
      if (char_count(se.new_name) > k_max_name_characters) {
        return error(i, "bad-request", "the new name is longer than 64 characters");
      }
      if (se.new_name.back() == ' ' || se.new_name.find('\0') != std::string::npos) {
        return error(i, "bad-request", "the new name ends in a space or holds a NUL");
      }
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
  const std::vector<const Schema *> &schemas() const { return m_schemas; }

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
  // The routines of that name, of `kind` when it is not "".
  std::vector<std::pair<const Schema *, const Routine *>> routines(std::string_view name,
                                                                   std::string_view kind) const {
    std::vector<std::pair<const Schema *, const Routine *>> out;
    for (const Schema *s : m_schemas) {
      for (const auto &r : s->routines) {
        if (iequals(r.name, name) && (kind.empty() || r.kind == kind)) out.push_back({s, &r});
      }
    }
    return out;
  }

 private:
  std::vector<const Schema *> m_schemas;
};

class Editor {
 public:
  Editor(Session &s, const Edit_request &request, Edit_result &result)
      : m_s(s), m_request(request), m_result(result) {}

  std::optional<Edit_error> run(size_t i) {
    const Schema_edit &e = m_request.edits[i];
    Changes changes;
    std::optional<Edit_error> err;
    switch (e.op) {
      case Edit_op::rename_table: err = rename_table(i, e, changes); break;
      case Edit_op::rename_column: err = rename_column(i, e, changes); break;
      case Edit_op::rename_view: err = rename_view(i, e, changes); break;
      case Edit_op::rename_routine: err = rename_routine(i, e, changes); break;
      default:
        return error(i, "unsupported", std::string("this binary does not do ") + to_string(e.op) + " yet");
    }
    if (err) return err;
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

  // REST objects naming `name`: their SQL is not rewritten.
  void rest_notes(size_t edit, const std::string &name, bool routine, const std::string &what) {
    for (const auto &svc : outline().catalog.rest_services()) {
      for (const auto &sch : svc.schemas) {
        for (const auto &o : sch.objects) {
          if ((o.kind == "view") == routine) continue;
          if (!iequals(last_name_part(o.object), name)) continue;
          note(edit, "rest-reference", o.origin.source, o.origin.statement.begin,
               "the REST object " + o.path + " names " + what + "; its REST SQL was not changed");
        }
      }
    }
  }

  // The name is free among tables and views (they share a namespace).
  std::optional<Edit_error> table_name_free(size_t edit, const std::string &old_name,
                                            const std::string &new_name) {
    if (iequals(old_name, new_name)) return std::nullopt;
    const auto l = lookup();
    if (l.table(new_name).second != nullptr || l.view(new_name).second != nullptr) {
      return error(edit, "name-taken", "a table or view `" + new_name + "` exists");
    }
    return std::nullopt;
  }

  void migrate(size_t edit, std::string sql) { m_result.migration.push_back({edit, std::move(sql)}); }

  std::optional<Edit_error> rename_table(size_t i, const Schema_edit &e, Changes &changes) {
    const Table *table = lookup().table(e.table).second;
    if (table == nullptr) return error(i, "unknown-object", "there is no table `" + e.table + "`");
    return rename_relation(i, e, "table", table->name, table->origin, Target_kind::table, changes);
  }

  std::optional<Edit_error> rename_view(size_t i, const Schema_edit &e, Changes &changes) {
    const View *view = lookup().view(e.view).second;
    if (view == nullptr) return error(i, "unknown-object", "there is no view `" + e.view + "`");
    return rename_relation(i, e, "view", view->name, view->origin, Target_kind::view, changes);
  }

  // A table or a view: they share a namespace and RENAME TABLE.
  std::optional<Edit_error> rename_relation(size_t i, const Schema_edit &e, const std::string &noun,
                                            const std::string &name, const Origin &origin,
                                            Target_kind kind, Changes &changes) {
    if (auto err = table_name_free(i, name, e.new_name)) return err;
    const auto id = resolve(origin, kind);
    if (!id) return error(i, "self-check", "the definition of " + noun + " `" + name + "` cannot be resolved");
    m_old_name = name;
    const std::string what = noun + " `" + name + "`";
    string_notes(i, {name}, what);
    rest_notes(i, name, false, what);
    if (auto err = rewrite_references(i, *id, origin, e.new_name, changes)) return err;
    // The same name again changes nothing: RENAME TABLE t TO t fails on the
    // server (the table exists).
    if (name != e.new_name) {
      migrate(i, "RENAME TABLE " + backtick_quote(name) + " TO " + backtick_quote(e.new_name));
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
      migrate(i, "ALTER TABLE " + backtick_quote(table->name) + " RENAME COLUMN " + backtick_quote(old_name) +
                     " TO " + backtick_quote(e.new_name));
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
    // References do not tell a procedure from a function of one name.
    if (l.routines(e.routine, "").size() > 1) {
      return error(i, "unsupported",
                   "a procedure and a function are both named `" + routine->name +
                       "`: renaming one of them is not supported yet");
    }
    if (!iequals(routine->name, e.new_name) && !l.routines(e.new_name, routine->kind).empty()) {
      return error(i, "name-taken", "a " + routine->kind + " `" + e.new_name + "` exists");
    }
    const auto id = resolve(routine->origin, Target_kind::routine);
    if (!id) return error(i, "self-check", "the definition of `" + routine->name + "` cannot be resolved");
    const std::string old_name = routine->name;
    string_notes(i, {old_name}, routine->kind + " `" + old_name + "`");
    rest_notes(i, old_name, true, routine->kind + " `" + old_name + "`");
    if (auto err = rewrite_references(i, *id, routine->origin, e.new_name, changes)) return err;
    m_old_name = old_name;
    m_routine_kind = routine->kind;
    return std::nullopt;
  }

  // The edited outline shows the edit.
  std::optional<Edit_error> verify(size_t i, const Schema_edit &e) {
    const auto l = lookup();
    const std::string &old_name = m_old_name;
    const bool case_only = iequals(old_name, e.new_name);
    auto fail = [&](const std::string &what) { return error(i, "self-check", what); };
    switch (e.op) {
      case Edit_op::rename_table:
      case Edit_op::rename_view: {
        const bool is_table = e.op == Edit_op::rename_table;
        const Table *t = l.table(e.new_name).second;
        const View *v = l.view(e.new_name).second;
        const std::string got = is_table ? (t ? t->name : "") : (v ? v->name : "");
        if (got != e.new_name) return fail("the edited script has no " + std::string(is_table ? "table" : "view") + " `" + e.new_name + "`");
        if (!case_only && (l.table(old_name).second != nullptr || l.view(old_name).second != nullptr)) {
          return fail("the edited script still has `" + old_name + "`");
        }
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
        // No DDL renames a routine: drop it, create it again.
        // The CREATE as it runs: from the expanded text, so a body that
        // includes a file holds that file's text.
        const Routine &r = *now[0].second;
        const auto at = m_s.to_searched(r.origin.source, r.origin.statement.begin);
        const auto &index = m_s.searched_index();
        const size_t k = at ? index.find(*at) : index.statements().size();
        if (k >= index.statements().size()) return fail("the CREATE of `" + e.new_name + "` is not found");
        migrate(i, (kind == "function" ? "DROP FUNCTION " : "DROP PROCEDURE ") + backtick_quote(old_name));
        migrate(i, std::string(trim(index.text_of(m_s.searched(), index.statements()[k]))));
        note(i, "routine-recreated", r.origin.source, r.origin.statement.begin,
             "no statement renames a " + kind + ": the migration drops `" + old_name +
                 "` and creates `" + e.new_name + "`, which cannot run inside a stored procedure");
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
