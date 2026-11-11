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

#include "mdbls/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <utility>

#include "mdbls/keywords.h"
#include "text_util.h"

namespace mdbls {

namespace {

// A string literal's contents: 'it''s' and 'it\'s' give it's.
std::string unquote_string(std::string_view s) {
  if (s.size() < 2 || (s.front() != '\'' && s.front() != '"') || s.back() != s.front()) {
    return std::string(s);
  }
  const char q = s.front();
  std::string out;
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    const char c = s[i];
    if (c == '\\' && i + 2 < s.size()) {
      const char n = s[++i];
      switch (n) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '0': out.push_back('\0'); break;
        default: out.push_back(n); break;
      }
    } else if (c == q && i + 2 < s.size() && s[i + 1] == q) {
      out.push_back(q);
      ++i;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// A name or a string literal without its quotes: `a`, 'a', "a" and a give a.
std::string unquote_any(std::string_view s) { return unquote_string(unquote_identifier(s)); }

// Whether `r` is the routine of `kind` (procedure, function) named `name`.
bool is_routine(const Routine &r, std::string_view kind, std::string_view name) {
  return r.kind == kind && iequals(r.name, name);
}

// The element of `v` (a vector, const or not) named `name`, or v.end().
template <typename V>
auto find_named(V &v, std::string_view name) {
  return std::find_if(v.begin(), v.end(), [&](const auto &x) { return iequals(x.name, name); });
}

// Replaces the element of v named like x, or appends x when there is none.
// `keep_existing`: an element of that name stays as it is (IF NOT EXISTS).
template <typename T>
void upsert_named(std::vector<T> &v, T x, bool keep_existing = false) {
  auto it = find_named(v, x.name);
  if (it == v.end()) {
    v.push_back(std::move(x));
  } else if (!keep_existing) {
    *it = std::move(x);
  }
}

// Removes the elements of v named `name`.
template <typename T>
void erase_named(std::vector<T> &v, std::string_view name) {
  std::erase_if(v, [&](const T &x) { return iequals(x.name, name); });
}

// Removes the elements of v (REST services or schemas) with request path `path`.
template <typename T>
void erase_by_path(std::vector<T> &v, std::string_view path) {
  std::erase_if(v, [&](const T &x) { return iequals(x.path, path); });
}

// The REST schema of `svc` with request path `path`, or nullptr.
Rest_schema *find_rest_schema(Rest_service &svc, std::string_view path) {
  auto it = std::find_if(svc.schemas.begin(), svc.schemas.end(),
                         [&](const Rest_schema &s) { return iequals(s.path, path); });
  return it == svc.schemas.end() ? nullptr : &*it;
}

// The grammar symbols the fold looks for, resolved once.
struct Syms {
#define SYM(x) int x = grammar::symbol(#x);
#include "catalog_symbols.inc"
#undef SYM
};

const Syms &syms() {
  static const Syms s;
  return s;
}

// Every name of Syms, for catalog_missing_grammar_symbols().
constexpr const char *k_sym_names[] = {
#define SYM(x) #x,
#include "catalog_symbols.inc"
#undef SYM
};

}  // namespace

const char *to_string(Note_kind kind) {
  switch (kind) {
    case Note_kind::syntax_error: return "syntax-error";
    case Note_kind::unsupported_sql_mode: return "unsupported-sql-mode";
    case Note_kind::unknown_table: return "unknown-table";
    case Note_kind::include_unreadable: return "include-unreadable";
    case Note_kind::include_nested: return "include-nested";
    case Note_kind::rest_none_in_use: return "rest-none-in-use";
    case Note_kind::unknown_rest_service: return "unknown-rest-service";
  }
  return "";
}

// --- lookups --------------------------------------------------------------

Column *Table::column(std::string_view n) {
  auto it = find_named(columns, n);
  return it == columns.end() ? nullptr : &*it;
}

Index Index::on(std::string kind, const std::string &column, Origin origin) {
  Index ix;
  ix.kind = std::move(kind);
  ix.origin = origin;
  ix.key_parts.emplace_back().column = column;
  return ix;
}

std::vector<std::string> Index::columns() const {
  std::vector<std::string> out;
  out.reserve(key_parts.size());
  for (const auto &kp : key_parts) out.push_back(kp.column);
  return out;
}

std::string Column::attributes() const {
  std::string out = type;
  if (charset) out += " CHARACTER SET " + *charset;
  if (collation) out += " COLLATE " + *collation;
  if (!nullable) out += " NOT NULL";
  if (default_value) out += " DEFAULT " + *default_value;
  if (auto_increment) out += " AUTO_INCREMENT";
  return out;
}

std::string Column::definition() const {
  std::string out = attributes();
  if (on_update) out += " ON UPDATE " + *on_update;
  if (generated) {
    out += " AS " + *generated;  // generated holds its parentheses
    if (generated_storage) out += " " + *generated_storage;
  } else if (generated_row) {
    out += " AS ROW " + *generated_row;
  }
  if (invisible) out += " INVISIBLE";
  return out;
}

std::string Routine::parameter_list() const {
  std::string out;
  for (const auto &p : parameters) {
    if (!out.empty()) out += ", ";
    out += (p.mode.empty() ? "" : p.mode + " ") + p.name + " " + p.type;
  }
  return out;
}

const Column *Table::column(std::string_view n) const {
  auto it = find_named(columns, n);
  return it == columns.end() ? nullptr : &*it;
}

Table *Schema::table(std::string_view n) {
  auto it = find_named(tables, n);
  return it == tables.end() ? nullptr : &*it;
}

const Table *Schema::table(std::string_view n) const {
  auto it = find_named(tables, n);
  return it == tables.end() ? nullptr : &*it;
}

const View *Schema::view(std::string_view n) const {
  auto it = find_named(views, n);
  return it == views.end() ? nullptr : &*it;
}

const Trigger *Schema::trigger(std::string_view n) const {
  auto it = find_named(triggers, n);
  return it == triggers.end() ? nullptr : &*it;
}

const Event *Schema::event(std::string_view n) const {
  auto it = find_named(events, n);
  return it == events.end() ? nullptr : &*it;
}

const Sequence *Schema::sequence(std::string_view n) const {
  auto it = find_named(sequences, n);
  return it == sequences.end() ? nullptr : &*it;
}

const Routine *Schema::routine(std::string_view n, std::string_view kind) const {
  auto it = std::find_if(routines.begin(), routines.end(), [&](const Routine &r) {
    return iequals(r.name, n) && (kind.empty() || iequals(r.kind, kind));
  });
  return it == routines.end() ? nullptr : &*it;
}

const Schema *Catalog::find(std::string_view n) const {
  auto it = find_named(m_schemas, n);
  return it == m_schemas.end() ? nullptr : &*it;
}

void Catalog::for_each_origin(const std::function<void(Origin &)> &f) {
  for (auto &s : m_schemas) {
    f(s.origin);
    for (auto &t : s.tables) {
      f(t.origin);
      for (auto &c : t.columns) f(c.origin);
      for (auto &i : t.indexes) f(i.origin);
      for (auto &k : t.foreign_keys) f(k.origin);
      for (auto &c : t.checks) f(c.origin);
    }
    for (auto &v : s.views) f(v.origin);
    for (auto &r : s.routines) f(r.origin);
    for (auto &t : s.triggers) f(t.origin);
    for (auto &e : s.events) f(e.origin);
    for (auto &q : s.sequences) f(q.origin);
  }
  for (auto &svc : m_services) {
    f(svc.origin);
    for (auto &rs : svc.schemas) {
      f(rs.origin);
      for (auto &o : rs.objects) f(o.origin);
    }
    for (auto &c : svc.content_sets) f(c.origin);
    for (auto &a : svc.auth_apps) f(a.origin);
  }
  for (auto *list : {&m_auth_apps, &m_roles, &m_users})
    for (auto &o : *list) f(o.origin);
}

Catalog Catalog::from_schemas(std::vector<Schema> schemas) {
  Catalog c;
  c.m_schemas = std::move(schemas);
  return c;
}

bool Schema::empty() const {
  return tables.empty() && views.empty() && routines.empty() && triggers.empty() &&
         events.empty() && sequences.empty();
}

Schema *Catalog::find_schema(std::string_view n) {
  auto it = find_named(m_schemas, n);
  return it == m_schemas.end() ? nullptr : &*it;
}

Schema &Catalog::schema(const std::string &n) {
  if (Schema *s = find_schema(n)) return *s;
  m_schemas.push_back(Schema{});
  m_schemas.back().name = n;
  return m_schemas.back();
}

Rest_service *Catalog::find_service(std::string_view path) {
  auto it = std::find_if(m_services.begin(), m_services.end(),
                         [&](const Rest_service &s) { return iequals(s.path, path); });
  return it == m_services.end() ? nullptr : &*it;
}

Rest_service &Catalog::service(const std::string &path) {
  if (Rest_service *s = find_service(path)) return *s;
  m_services.push_back(Rest_service{});
  m_services.back().path = path;
  return m_services.back();
}

std::vector<std::string> catalog_missing_grammar_symbols() {
  std::vector<std::string> out;
  for (const char *n : k_sym_names) {
    if (grammar::symbol(n) < 0) out.emplace_back(n);
  }
  return out;
}

bool may_change_catalog(std::string_view s) {
  size_t i = 0;
  for (;;) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (s.substr(i, 3) == "/*!" || s.substr(i, 4) == "/*M!") {
      i += s[i + 2] == 'M' ? 4 : 3;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
      continue;
    }
    if (s.substr(i, 2) == "/*") {
      const auto e = s.find("*/", i + 2);
      if (e == std::string_view::npos) return false;
      i = e + 2;
      continue;
    }
    if (s.substr(i, 1) == "#" || (s.substr(i, 2) == "--")) {
      const auto e = s.find('\n', i);
      if (e == std::string_view::npos) return false;
      i = e + 1;
      continue;
    }
    break;
  }
  size_t j = i;
  while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
  const auto w = s.substr(i, j - i);
  return iequals(w, "CREATE") || iequals(w, "ALTER") || iequals(w, "DROP") ||
         iequals(w, "RENAME") || iequals(w, "USE");
}

// --- the fold -------------------------------------------------------------

class Catalog_fold {
 public:
  Catalog_fold(Catalog &c, const Syntax_tree &t, std::string_view text)
      : c_(c), t_(t), text_(text), s_(syms()) {}

  bool run();

 private:
  static constexpr uint32_t npos = Syntax_tree::npos;

  // --- tree helpers
  std::string_view raw(uint32_t n) const { return t_.text(text_, n); }
  std::string text(uint32_t n) const { return n == npos ? "" : collapse(raw(n)); }
  std::string name_of(uint32_t n) const { return n == npos ? "" : unquote_identifier(collapse(raw(n))); }
  Range range(uint32_t n) const { return n == npos ? stmt_ : Range{t_.begin(n), t_.end(n)}; }
  Origin origin(uint32_t name_node) const { return {stmt_, range(name_node), c_.m_source}; }
  Origin origin(Range statement, Range name) const { return {statement, name, c_.m_source}; }
  uint32_t child(uint32_t n, int sym) const { return n == npos ? npos : t_.child(n, sym); }
  uint32_t find(uint32_t n, int sym) const { return n == npos ? npos : t_.find(n, sym); }
  std::vector<uint32_t> find_all(uint32_t n, int sym) const {
    return n == npos ? std::vector<uint32_t>{} : t_.find_all(n, sym);
  }
  bool has_child(uint32_t n, int sym) const { return child(n, sym) != npos; }
  int first_symbol(uint32_t n) const {
    const auto ch = t_.children(n);
    for (const uint32_t c : ch) {
      if (t_.node(c).token_count > 0 || t_.is_token(c)) return t_.node(c).symbol;
    }
    return -1;
  }
  // The i-th child (skipping nothing).
  uint32_t nth(uint32_t n, size_t i) const {
    const auto ch = t_.children(n);
    return i < ch.size() ? ch[i] : npos;
  }
  bool non_empty(uint32_t n) const { return n != npos && t_.node(n).token_count > 0; }
  // The text from offset `from` to the end of node `to`, blanks collapsed.
  std::string text_span(uint32_t from, uint32_t to) const {
    return collapse(text_.substr(from, t_.end(to) - from));
  }
  // A charset or collation name: unquoted, lower-cased.
  std::string charset_name(uint32_t n) const { return lower(unquote_any(text(n))); }

  // The ident children of a table_ident / sp_name: [schema,] name.
  std::vector<uint32_t> idents_of(uint32_t n) const {
    std::vector<uint32_t> ids;
    for (const uint32_t c : t_.children(n)) {
      if (t_.node(c).symbol == s_.ident) ids.push_back(c);
    }
    return ids;
  }
  // schema.name of a table_ident / sp_name (ident '.' ident | ident | '.' ident)
  std::pair<std::string, uint32_t> qualified(uint32_t n) const {
    const auto ids = idents_of(n);
    if (ids.size() >= 2) return {name_of(ids[0]), ids[1]};
    if (ids.size() == 1) return {c_.m_current, ids[0]};
    return {c_.m_current, n};
  }

  // --- statements
  bool create(uint32_t n);
  bool create_table(uint32_t n);
  bool create_routine(uint32_t n);
  bool alter(uint32_t n);
  bool drop(uint32_t n);
  bool rename(uint32_t n);
  bool rest(uint32_t n);

  // --- parts
  View make_view(uint32_t n, uint32_t id) const;
  // A WITH clause of a view's query: whether RECURSIVE, its elements and
  // their names in order.
  struct With_scope {
    uint32_t node;
    bool recursive;
    std::vector<std::pair<uint32_t, std::string>> elements;
  };
  std::vector<View_read> view_reads(uint32_t query) const;
  bool names_cte(uint32_t table_ident, const std::string &name, uint32_t query,
                 const std::vector<With_scope> &withs) const;
  std::pair<std::string, std::string> written_name(const std::vector<uint32_t> &parts) const;
  std::string text_without(uint32_t n, const std::vector<uint32_t> &cut) const;
  std::string definer_of(uint32_t n) const;
  std::string body_of(uint32_t n) const;
  void index_details(uint32_t n, Index &ix) const;
  std::vector<Key_part> key_parts(uint32_t n) const;
  void routine_characteristics(uint32_t n, Routine &r) const;
  void event_details(uint32_t holder, uint32_t body, Event &e) const;
  void sequence_options(uint32_t n, Sequence &q) const;
  Check make_check(uint32_t check_constraint, std::string name, Range def) const;
  void column_check(uint32_t field_spec, const std::string &column, Table &t) const;
  void add_period(uint32_t n, Table &t);
  static void rename_column_refs(Table &t, const std::string &from, const std::string &to);
  void engine_option(uint32_t n, std::vector<std::pair<std::string, std::string>> &opts) const;
  void field_list_items(uint32_t n, Table &t);
  void column_def(uint32_t n, Table &t, std::optional<size_t> at = std::nullopt);
  Column column(uint32_t field_spec, Table &t);
  void key_def(uint32_t n, Table &t);
  void check_def(uint32_t n, Table &t);
  Foreign_key references(uint32_t refs, std::string name, std::vector<std::string> columns,
                         uint32_t at) const;
  std::string constraint_name(uint32_t opt_constraint) const;
  bool table_options(uint32_t n, std::vector<std::pair<std::string, std::string>> &opts);
  static void set_option(std::vector<std::pair<std::string, std::string>> &opts,
                         const std::string &key, std::string value);

  // A note on the statement being folded.
  void note(Note_kind kind, bool applied, std::string message) {
    c_.m_notes.push_back({stmt_.begin, kind, applied, std::move(message), c_.m_source});
  }

  Catalog &c_;
  const Syntax_tree &t_;
  std::string_view text_;
  const Syms &s_;
  Range stmt_;
};

bool Catalog_fold::run() {
  if (t_.roots().size() != 1) return false;
  const uint32_t root = t_.roots()[0];
  stmt_ = range(root);
  const uint32_t verb = find(root, s_.verb_clause);
  if (verb == npos) return false;
  const auto ch = t_.children(verb);
  if (ch.empty()) return false;
  const uint32_t n = ch[0];
  const int sym = t_.node(n).symbol;
  if (sym == s_.create) return create(n);
  if (sym == s_.alter) return alter(n);
  if (sym == s_.drop) return drop(n);
  if (sym == s_.rename) return rename(n);
  if (sym == s_.use) {
    c_.m_current = name_of(child(n, s_.ident));
    return true;
  }
  if (sym == s_.mrs_statement) return rest(n);
  return false;
}

void Catalog_fold::set_option(std::vector<std::pair<std::string, std::string>> &opts,
                              const std::string &key, std::string value) {
  for (auto &[k, v] : opts) {
    if (k == key) {
      v = std::move(value);
      return;
    }
  }
  opts.emplace_back(key, std::move(value));
}

bool Catalog_fold::create(uint32_t n) {
  if (has_child(n, s_.create_routine)) return create_routine(child(n, s_.create_routine));
  if (has_child(n, s_.TABLE_SYM)) return create_table(n);

  const bool replace = find(child(n, s_.create_or_replace), s_.OR_SYM) != npos;
  const bool if_not_exists = non_empty(child(n, s_.opt_if_not_exists));

  if (has_child(n, s_.DATABASE)) {
    const uint32_t id = child(n, s_.ident);
    const std::string name = name_of(id);
    Schema *existing = c_.find_schema(name);
    if (existing != nullptr && existing->declared && if_not_exists) return true;
    Schema &sc = c_.schema(name);
    sc.declared = true;
    sc.origin = origin(id);
    for (const uint32_t o : find_all(child(n, s_.opt_create_database_options), s_.create_database_option)) {
      if (const uint32_t cs = child(o, s_.default_charset); cs != npos) {
        set_option(sc.options, "charset", lower(text(child(cs, s_.charset_name_or_default))));
      } else if (const uint32_t co = child(o, s_.default_collation); co != npos) {
        set_option(sc.options, "collation", lower(text(child(co, s_.collation_name_or_default))));
      } else if (const uint32_t cm = child(o, s_.TEXT_STRING_sys); cm != npos) {
        set_option(sc.options, "comment", unquote_string(raw(cm)));
      }
    }
    return true;
  }
  if (has_child(n, s_.SEQUENCE_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.table_ident));
    Schema &sc = c_.schema(schema_name);
    const std::string name = name_of(id);
    auto it = find_named(sc.sequences, name);
    if (it != sc.sequences.end()) {
      if (if_not_exists) return true;
      sc.sequences.erase(it);
    }
    Sequence q;
    q.name = name;
    q.origin = origin(id);
    sequence_options(child(n, s_.opt_sequence), q);
    table_options(child(n, s_.opt_create_table_options), q.options);
    sc.sequences.push_back(std::move(q));
    return true;
  }
  if (has_child(n, s_.INDEX_SYM)) {  // CREATE [UNIQUE|FULLTEXT|SPATIAL] INDEX i ON t (...)
    auto [schema_name, tid] = qualified(child(n, s_.table_ident));
    Table *tb = c_.schema(schema_name).table(name_of(tid));
    if (tb == nullptr) {
      note(Note_kind::unknown_table, false,
           "CREATE INDEX on table " + name_of(tid) + ", which the script does not create");
      return false;
    }
    Index ix;
    const uint32_t id = child(n, s_.ident);
    ix.name = name_of(id);
    ix.kind = has_child(n, s_.UNIQUE_SYM) ? "unique"
              : has_child(n, s_.fulltext) ? "fulltext"
              : has_child(n, s_.spatial_or_vector)
                  ? lower(text(child(n, s_.spatial_or_vector)))
                  : "index";
    index_details(n, ix);
    ix.origin = origin(id);
    upsert_named(tb->indexes, std::move(ix));
    return true;
  }
  if (has_child(n, s_.VIEW_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.table_ident));
    upsert_named(c_.schema(schema_name).views, make_view(n, id), if_not_exists && !replace);
    return true;
  }
  if (has_child(n, s_.TRIGGER_SYM)) {
    const uint32_t tail = find(n, s_.trigger_tail);
    auto [schema_name, id] = qualified(child(tail, s_.sp_name));
    auto [table_schema, tid] = qualified(child(tail, s_.table_ident));
    Trigger tr;
    tr.name = name_of(id);
    tr.timing = upper(text(child(tail, s_.trg_action_time)));
    tr.events = upper(text(child(tail, s_.trg_events)));
    tr.table = name_of(tid);
    tr.origin = origin(id);
    tr.body = body_of(child(tail, s_.sp_proc_stmt));
    tr.definer = definer_of(n);
    if (const uint32_t fp = child(tail, s_.trigger_follows_precedes_clause); non_empty(fp)) {
      tr.order_kind = upper(text(child(fp, s_.trigger_action_order)));
      tr.order_trigger = unquote_any(text(child(fp, s_.ident_or_text)));
    }
    for (const uint32_t c : find_all(child(tail, s_.opt_on_update_cols), s_.ident)) {
      tr.update_columns.push_back(name_of(c));
    }
    Schema &sc = c_.schema(child(child(tail, s_.sp_name), s_.ident) != npos &&
                                   t_.children(child(tail, s_.sp_name)).size() >= 3
                               ? schema_name
                               : table_schema);
    upsert_named(sc.triggers, std::move(tr));
    return true;
  }
  if (has_child(n, s_.EVENT_SYM)) {
    const uint32_t tail = find(n, s_.event_tail);
    auto [schema_name, id] = qualified(child(tail, s_.sp_name));
    Event e;
    e.name = name_of(id);
    e.origin = origin(id);
    e.definer = definer_of(n);
    event_details(tail, child(tail, s_.ev_sql_stmt), e);
    upsert_named(c_.schema(schema_name).events, std::move(e));
    return true;
  }
  return false;  // USER, ROLE, SERVER
}

// CREATE VIEW and ALTER VIEW: view_list_opt, view_select.
View Catalog_fold::make_view(uint32_t n, uint32_t id) const {
  View v;
  v.name = name_of(id);
  for (const uint32_t c : find_all(child(n, s_.view_list_opt), s_.ident)) v.columns.push_back(name_of(c));
  const uint32_t query = child(n, s_.view_select);
  v.definition = text(query);
  v.reads = view_reads(query);
  v.origin = origin(id);
  if (const uint32_t a = child(n, s_.view_algorithm); a != npos) v.algorithm = upper(text(t_.children(a).back()));
  v.definer = definer_of(n);
  if (const uint32_t su = find(child(n, s_.opt_view_suid), s_.view_suid); su != npos) {
    v.security = upper(text(t_.children(su).back()));
  }
  if (const uint32_t co = child(child(n, s_.view_select), s_.view_check_option); non_empty(co)) {
    v.check_option = has_child(co, s_.LOCAL_SYM) ? "LOCAL" : "CASCADED";
  }
  return v;
}

// [schema.]name from the name parts of a table_ident or a function call:
// the qualifier ("" when none) and the name, unquoted.
std::pair<std::string, std::string> Catalog_fold::written_name(const std::vector<uint32_t> &parts) const {
  if (parts.empty()) return {};
  if (parts.size() >= 2) return {name_of(parts[0]), name_of(parts[1])};
  return {"", name_of(parts[0])};
}

// Whether `name` at `table_ident` (unqualified) is a common table expression
// in scope: one of a WITH clause (`withs`, those of `query`) of a query
// around it, the innermost first.
bool Catalog_fold::names_cte(uint32_t ti, const std::string &name, uint32_t query,
                             const std::vector<With_scope> &withs) const {
  for (uint32_t c = ti; c != query;) {
    const uint32_t a = t_.node(c).parent;
    if (a == npos) break;
    for (const auto &w : withs) {
      if (t_.node(w.node).parent != a) continue;
      for (const auto &[e, e_name] : w.elements) {
        // Without RECURSIVE an element sees only the elements before it.
        if (!w.recursive && c == w.node && t_.begin(e) <= t_.begin(ti) && t_.end(ti) <= t_.end(e)) break;
        if (iequals(e_name, name)) return true;  // CTE names are case-insensitive
      }
    }
    c = a;
  }
  return false;
}

// What a view's query reads (docs/outline-format.md, "reads"), in text order.
std::vector<View_read> Catalog_fold::view_reads(uint32_t query) const {
  std::vector<View_read> out;
  auto add = [&](const char *kind, std::pair<std::string, std::string> sn) {
    for (const auto &r : out) {
      if (r.kind == kind && r.schema == sn.first && r.name == sn.second) return;
    }
    out.push_back({kind, std::move(sn.first), std::move(sn.second)});
  };
  auto sequence_call = [&](uint32_t n) {
    return has_child(n, s_.NEXTVAL_SYM) || has_child(n, s_.NEXT_SYM) || has_child(n, s_.PREVIOUS_SYM) ||
           has_child(n, s_.LASTVAL_SYM) || has_child(n, s_.SETVAL_SYM);
  };
  // The WITH clauses met so far: the walk is in text order, so a clause is
  // met before every name in its scope.
  std::vector<With_scope> withs;
  std::vector<uint32_t> stack;
  if (query != npos) stack.push_back(query);
  while (!stack.empty()) {
    const uint32_t n = stack.back();
    stack.pop_back();
    const int sym = t_.node(n).symbol;
    if (sym == s_.with_clause) {
      With_scope w{n, has_child(child(n, s_.opt_recursive), s_.RECURSIVE_SYM), {}};
      for (const uint32_t e : find_all(child(n, s_.with_list), s_.with_list_element)) {
        w.elements.emplace_back(e, name_of(find(child(e, s_.with_element_head), s_.ident)));
      }
      withs.push_back(std::move(w));
    } else if (sym == s_.table_ident) {
      auto sn = written_name(idents_of(n));
      if (sn.second.empty()) continue;
      if (sequence_call(t_.node(n).parent)) {
        add("sequence", std::move(sn));
      } else if (!sn.first.empty() || withs.empty() || !names_cte(n, sn.second, query, withs)) {
        add("table_or_view", std::move(sn));
      }
      continue;
    } else if (sym == s_.function_call_generic) {
      // ident_cli ['.' ident_cli] '(' ...: the names before the parenthesis;
      // after a qualifier also a keyword (db.REPLACE, db.SUBSTRING, db.TRIM).
      std::vector<uint32_t> parts;
      for (const uint32_t c : t_.children(n)) {
        const int cs = t_.node(c).symbol;
        if (is_left_paren(cs)) break;
        if (t_.node(c).token_count == 0) continue;
        if (!grammar::is_terminal(cs) || (!parts.empty() && text(c) != ".")) parts.push_back(c);
      }
      auto sn = written_name(parts);
      if (!sn.second.empty() && (!sn.first.empty() || builtin_function(sn.second) == nullptr)) {
        add("function", std::move(sn));
      }
    }
    const auto ch = t_.children(n);
    for (size_t k = ch.size(); k-- > 0;) stack.push_back(ch[k]);
  }
  return out;
}

// The text of `n` without the nodes `cut`, blanks collapsed.
std::string Catalog_fold::text_without(uint32_t n, const std::vector<uint32_t> &cut) const {
  if (n == npos) return "";
  std::vector<Range> rs;
  for (const uint32_t c : cut) {
    if (non_empty(c)) rs.push_back(range(c));
  }
  if (rs.empty()) return text(n);
  std::sort(rs.begin(), rs.end(), [](const Range &a, const Range &b) { return a.begin < b.begin; });
  std::string out;
  uint32_t at = t_.begin(n);
  for (const Range &r : rs) {
    if (r.begin < at) continue;
    out.append(text_.substr(at, r.begin - at));
    out.push_back(' ');
    at = r.end;
  }
  if (at < t_.end(n)) out.append(text_.substr(at, t_.end(n) - at));
  return collapse(out);
}

// DEFINER = ... of a CREATE/ALTER node holding `definer` or `definer_opt`:
// user@host without quotes, CURRENT_USER, CURRENT_ROLE, or a role.
std::string Catalog_fold::definer_of(uint32_t n) const {
  uint32_t d = child(n, s_.definer);
  if (d == npos) d = child(child(n, s_.definer_opt), s_.definer);
  if (d == npos) return "";
  if (const uint32_t u = find(d, s_.user_name); u != npos) {
    std::string out;
    for (const uint32_t c : t_.children(u)) {
      if (t_.node(c).symbol != s_.ident_or_text) continue;
      if (!out.empty()) out += "@";
      out += unquote_any(text(c));
    }
    return out;
  }
  if (find(d, s_.CURRENT_USER) != npos) return "CURRENT_USER";
  if (find(d, s_.current_role) != npos) return "CURRENT_ROLE";
  return unquote_any(text(t_.children(d).back()));
}

// A routine, trigger or event body: exactly as written.
std::string Catalog_fold::body_of(uint32_t n) const {
  return non_empty(n) ? std::string(raw(n)) : std::string();
}

// An engine-defined option (PAGE_COMPRESSED=1, ENCRYPTED=YES): the
// lower-cased name, the value as written.
void Catalog_fold::engine_option(uint32_t n,
                                 std::vector<std::pair<std::string, std::string>> &opts) const {
  const auto ch = t_.children(n);
  if (ch.size() < 2) return;
  set_option(opts, lower(text(ch[0])), text(ch.back()));
}

// The key parts of a key_def or CREATE INDEX node.
std::vector<Key_part> Catalog_fold::key_parts(uint32_t n) const {
  std::vector<Key_part> out;
  const uint32_t list = child(n, s_.key_list);
  if (list == npos) {  // SPATIAL / VECTOR: one key_part_simple
    if (const uint32_t kp = child(n, s_.key_part_simple); kp != npos) {
      out.emplace_back().column = name_of(find(kp, s_.ident));
    }
    return out;
  }
  const auto parts = find_all(list, s_.key_part);
  const auto dirs = find_all(list, s_.order_dir);
  for (size_t i = 0; i < parts.size(); ++i) {
    Key_part kp;
    const uint32_t id = find(parts[i], s_.ident);
    kp.column = id != npos ? name_of(id) : text(parts[i]);
    if (const uint32_t num = child(parts[i], s_.NUM); num != npos) {
      kp.length = static_cast<uint32_t>(std::strtoul(std::string(raw(num)).c_str(), nullptr, 10));
    }
    if (i < dirs.size()) kp.order = upper(text(dirs[i]));
    out.push_back(std::move(kp));
  }
  return out;
}

// Key parts, USING, COMMENT, IGNORED and other options of a key_def or
// CREATE INDEX node.
void Catalog_fold::index_details(uint32_t n, Index &ix) const {
  ix.key_parts = key_parts(n);
  if (const uint32_t b = find(n, s_.btree_or_rtree); b != npos) ix.using_ = upper(text(b));
  if (const uint32_t wo = child(n, s_.opt_without_overlaps); non_empty(wo)) {
    ix.without_overlaps = name_of(child(wo, s_.ident));
  }
  for (const uint32_t o : find_all(n, s_.all_key_opt)) {
    const int first = first_symbol(o);
    if (first == s_.COMMENT_SYM) {
      ix.comment = unquote_string(raw(child(o, s_.TEXT_STRING_sys)));
    } else if (first == s_.KEY_BLOCK_SIZE) {
      set_option(ix.options, "key_block_size", text(t_.children(o).back()));
    } else if (const uint32_t ig = child(o, s_.ignorability); ig != npos) {
      ix.ignored = !has_child(ig, s_.NOT_SYM);
    } else if (const uint32_t eo = child(o, s_.engine_defined_option); eo != npos) {
      engine_option(eo, ix.options);
    }
  }
  for (const uint32_t o : find_all(n, s_.fulltext_key_opt)) {
    if (has_child(o, s_.PARSER_SYM)) set_option(ix.options, "parser", text(child(o, s_.IDENT_sys)));
  }
}

// The characteristics of a routine (sp_c_chistics, sp_a_chistics).
void Catalog_fold::routine_characteristics(uint32_t n, Routine &r) const {
  for (const uint32_t c : find_all(n, s_.sp_c_chistic)) {
    if (has_child(c, s_.DETERMINISTIC_SYM)) r.deterministic = !non_empty(child(c, s_.opt_not));
  }
  for (const uint32_t h : find_all(n, s_.sp_chistic)) {
    const int first = first_symbol(h);
    if (first == s_.COMMENT_SYM) {
      r.comment = unquote_string(raw(child(h, s_.TEXT_STRING_sys)));
    } else if (first == s_.sp_suid) {
      r.security = upper(text(t_.children(child(h, s_.sp_suid)).back()));
    } else if (first != s_.LANGUAGE_SYM) {
      r.data_access = upper(text(h));
    }
  }
}

// What CREATE EVENT (`holder` its event_tail) or ALTER EVENT (the alter
// node) writes; `body` is its ev_sql_stmt.  ALTER EVENT keeps what it does
// not write.
void Catalog_fold::event_details(uint32_t holder, uint32_t body, Event &e) const {
  uint32_t sched = holder;
  if (const uint32_t a = child(holder, s_.ev_alter_on_schedule_completion); a != npos) sched = a;
  if (const uint32_t st = child(sched, s_.ev_schedule_time); st != npos) e.schedule = text(st);
  uint32_t oc = child(sched, s_.ev_on_completion);
  if (oc == npos) oc = child(child(holder, s_.opt_ev_on_completion), s_.ev_on_completion);
  if (oc != npos) e.on_completion_preserve = !non_empty(child(oc, s_.opt_not));
  if (const uint32_t st = child(holder, s_.opt_ev_status); non_empty(st)) {
    e.status = has_child(st, s_.ENABLE_SYM) ? "ENABLE"
               : has_child(st, s_.ON)       ? "DISABLE ON SLAVE"
                                            : "DISABLE";
  }
  if (const uint32_t cm = child(holder, s_.opt_ev_comment); non_empty(cm)) {
    e.comment = unquote_string(raw(child(cm, s_.TEXT_STRING_sys)));
  }
  if (const uint32_t b = child(body, s_.sp_proc_stmt); b != npos) e.body = body_of(b);
}

// The sequence_def list of CREATE / ALTER SEQUENCE.
void Catalog_fold::sequence_options(uint32_t n, Sequence &q) const {
  for (const uint32_t d : find_all(n, s_.sequence_def)) {
    const auto ch = t_.children(d);
    if (ch.empty()) continue;
    const int first = t_.node(ch[0]).symbol;
    const std::string value = text(ch.back());
    if (first == s_.AS) {
      q.type = text_span(t_.begin(ch[1]), d);
    } else if (first == s_.MINVALUE_SYM) {
      q.minvalue = value;
    } else if (first == s_.MAXVALUE_SYM) {
      q.maxvalue = value;
    } else if (first == s_.NOMINVALUE_SYM || (first == s_.NO_SYM && has_child(d, s_.MINVALUE_SYM))) {
      q.minvalue.clear();
    } else if (first == s_.NOMAXVALUE_SYM || (first == s_.NO_SYM && has_child(d, s_.MAXVALUE_SYM))) {
      q.maxvalue.clear();
    } else if (first == s_.START_SYM) {
      q.start = value;
    } else if (first == s_.INCREMENT_SYM) {
      q.increment = value;
    } else if (first == s_.CACHE_SYM) {
      q.cache = value;
    } else if (first == s_.NOCACHE_SYM) {
      q.cache = "0";
    } else if (first == s_.CYCLE_SYM) {
      q.cycle = true;
    } else if (first == s_.NOCYCLE_SYM) {
      q.cycle = false;
    }
  }
}

// The CHECK (expr) of a check_constraint node; `def` is its whole definition.
Check Catalog_fold::make_check(uint32_t cc, std::string name, Range def) const {
  Check ck;
  ck.name = std::move(name);
  const uint32_t e = find(cc, s_.expr);
  ck.expression = text(e != npos ? e : cc);
  ck.origin = origin(def, def);
  return ck;
}

// Drops the column-level CHECK of `column`.
static void drop_column_checks(Table &tb, const std::string &column) {
  std::erase_if(tb.checks, [&](const Check &k) { return !k.column.empty() && iequals(k.column, column); });
}

// A column-level CHECK (expr) of `field_spec`, as a check of the table.
void Catalog_fold::column_check(uint32_t spec, const std::string &column, Table &tb) const {
  const uint32_t cc = child(child(spec, s_.opt_check_constraint), s_.check_constraint);
  if (cc == npos) return;
  Check ck = make_check(cc, "", range(cc));
  ck.column = column;
  tb.checks.push_back(std::move(ck));
}

// PERIOD FOR SYSTEM_TIME (s, e) or PERIOD FOR p (s, e) in `n`.
void Catalog_fold::add_period(uint32_t n, Table &tb) {
  uint32_t node = child(n, s_.period_for_system_time);
  const bool system_time = node != npos;
  if (!system_time) node = child(n, s_.period_for_application_time);
  if (node == npos) return;
  std::vector<uint32_t> ids;
  for (const uint32_t c : t_.children(node)) {
    if (t_.node(c).symbol == s_.ident) ids.push_back(c);
  }
  const size_t at = system_time ? 0 : 1;  // the application period's name comes first
  if (ids.size() < at + 2) return;
  Period p;
  p.name = system_time ? "SYSTEM_TIME" : name_of(ids[0]);
  p.start = name_of(ids[at]);
  p.end = name_of(ids[at + 1]);
  upsert_named(tb.periods, std::move(p));
}

// A column renamed: its name in keys, checks and periods follows.
void Catalog_fold::rename_column_refs(Table &tb, const std::string &from, const std::string &to) {
  if (iequals(from, to)) return;
  auto fix = [&](std::string &s) {
    if (iequals(s, from)) s = to;
  };
  for (auto &ix : tb.indexes) {
    for (auto &kp : ix.key_parts) fix(kp.column);
  }
  for (auto &fk : tb.foreign_keys) {
    for (auto &c : fk.columns) fix(c);
  }
  for (auto &ck : tb.checks) fix(ck.column);
  for (auto &p : tb.periods) {
    fix(p.start);
    fix(p.end);
  }
}

bool Catalog_fold::create_table(uint32_t n) {
  auto [schema_name, id] = qualified(child(n, s_.table_ident));
  const bool replace = find(child(n, s_.create_or_replace), s_.OR_SYM) != npos;
  const bool if_not_exists = non_empty(child(n, s_.opt_if_not_exists));
  const std::string name = name_of(id);
  if (c_.schema(schema_name).table(name) != nullptr && if_not_exists && !replace) {
    return true;  // IF NOT EXISTS: the existing table stays
  }

  Table tb;
  const uint32_t body = child(n, s_.create_body);
  if (const uint32_t like = child(body, s_.create_like); like != npos) {
    auto [src_schema, src_id] = qualified(find(like, s_.table_ident));
    if (const Table *src = c_.schema(src_schema).table(name_of(src_id))) {
      // Everything but the name, foreign keys and where it was written.
      tb = *src;
      tb.foreign_keys.clear();
      tb.stub = false;
    } else {
      note(Note_kind::unknown_table, true,
           "CREATE TABLE ... LIKE " + name_of(src_id) + ", which the script does not create");
    }
  }
  tb.name = name;
  tb.temporary = non_empty(child(n, s_.opt_temporary));
  tb.origin = origin(id);
  if (const uint32_t list = child(body, s_.create_field_list_parens); list != npos) {
    field_list_items(list, tb);
  }
  if (table_options(child(body, s_.opt_create_table_options), tb.options)) tb.system_versioning = true;
  if (const uint32_t p = find(child(body, s_.opt_create_partitioning), s_.partitioning); p != npos) {
    tb.partitioning = text(p);
  }

  // Looked up again: LIKE may have added a schema, moving the others.
  upsert_named(c_.schema(schema_name).tables, std::move(tb));
  return true;
}

void Catalog_fold::field_list_items(uint32_t n, Table &tb) {
  for (const uint32_t item : find_all(n, s_.field_list_item)) {
    const auto ch = t_.children(item);
    if (ch.empty()) continue;
    const int sym = t_.node(ch[0]).symbol;
    if (sym == s_.column_def) column_def(ch[0], tb);
    else if (sym == s_.key_def) key_def(ch[0], tb);
    else if (sym == s_.constraint_def) check_def(ch[0], tb);
    else if (sym == s_.period_for_system_time || sym == s_.PERIOD_SYM) add_period(item, tb);
  }
}

Column Catalog_fold::column(uint32_t spec, Table &tb) {
  Column c;
  const uint32_t id = child(spec, s_.field_ident);
  c.name = name_of(id);
  c.origin = origin(id);
  c.origin.statement = range(spec);
  const uint32_t type = child(spec, s_.field_type_or_serial);
  if (const uint32_t q = child(type, s_.qualified_field_type); q != npos) {
    // CHARACTER SET x, ASCII, UNICODE and COLLATE y go to charset and
    // collation; BINARY and BYTE stay in the type.
    std::vector<uint32_t> cut;
    if (const uint32_t bin = find(q, s_.binary); bin != npos) {
      bool after_collate = false;
      for (const uint32_t ch : t_.children(bin)) {
        const int sym = t_.node(ch).symbol;
        if (sym == s_.charset_or_alias) {
          cut.push_back(ch);
          if (const uint32_t cn = child(ch, s_.charset_name); cn != npos) {
            c.charset = charset_name(cn);
          } else if (has_child(ch, s_.ASCII_SYM)) {
            c.charset = "latin1";
          } else if (has_child(ch, s_.UNICODE_SYM)) {
            c.charset = "ucs2";
          }
        } else if (sym == s_.COLLATE_SYM) {
          cut.push_back(ch);
          after_collate = true;
        } else if (after_collate && sym == s_.collation_name) {
          cut.push_back(ch);
          c.collation = charset_name(ch);
        } else if (after_collate && sym == s_.DEFAULT) {
          cut.push_back(ch);
          c.collation = "default";
        }
      }
    }
    c.type = text_without(q, cut);
  } else {
    // SERIAL: BIGINT UNSIGNED NOT NULL AUTO_INCREMENT UNIQUE
    c.type = "SERIAL";
    c.nullable = false;
    c.auto_increment = true;
  }
  const uint32_t def = child(type, s_.field_def);
  if (def == npos) return c;
  if (const uint32_t g = child(def, s_.virtual_column_func); g != npos) {
    c.generated = text(g);
    if (const uint32_t st = child(def, s_.vcol_opt_specifier); non_empty(st)) {
      c.generated_storage = upper(text(st));
    }
  } else if (has_child(def, s_.ROW_SYM)) {  // AS ROW START / AS ROW END
    c.generated_row = has_child(def, s_.START_SYM) ? "START" : "END";
  }

  // Walk the attributes, not descending into expressions.
  std::vector<uint32_t> todo{def};
  while (!todo.empty()) {
    const uint32_t x = todo.back();
    todo.pop_back();
    const int sym = t_.node(x).symbol;
    if (sym == s_.column_default_expr || sym == s_.virtual_column_func) continue;
    if (sym == s_.attribute) {
      const int first = first_symbol(x);
      if (first == s_.NULL_SYM) {
        c.nullable = true;
      } else if (first == s_.DEFAULT) {
        c.default_value = text(child(x, s_.column_default_expr));
      } else if (first == s_.AUTO_INC) {
        c.auto_increment = true;
      } else if (first == s_.SERIAL_SYM) {
        c.auto_increment = true;
        c.nullable = false;
      } else if (first == s_.COLLATE_SYM) {
        c.collation = charset_name(child(x, s_.collation_name));
      } else if (first == s_.ON) {  // ON UPDATE CURRENT_TIMESTAMP[(n)]
        c.on_update = text_span(t_.begin(child(x, s_.NOW_SYM)), x);
      }
    } else if (sym == s_.serial_attribute) {
      if (const uint32_t w = child(x, s_.with_or_without_system); w != npos) {
        c.system_versioning = has_child(w, s_.WITH_SYSTEM_SYM);
      }
    } else if (sym == s_.engine_defined_option) {
      engine_option(x, c.options);
      continue;
    } else if (sym == s_.asrow_attribute) {
      if (has_child(x, s_.NULL_SYM)) {
        c.nullable = false;  // NOT NULL
      } else if (has_child(x, s_.KEY_SYM)) {  // [PRIMARY] KEY
        tb.indexes.push_back(Index::on("primary", c.name, c.origin));
        c.nullable = false;
      }
    } else if (sym == s_.vcol_attribute) {
      if (has_child(x, s_.UNIQUE_SYM)) {
        tb.indexes.push_back(Index::on("unique", c.name, c.origin));
      } else if (const uint32_t cm = child(x, s_.TEXT_STRING_sys); cm != npos) {
        c.comment = unquote_string(raw(cm));
      } else if (has_child(x, s_.INVISIBLE_SYM)) {
        c.invisible = true;
      }
    }
    const auto ch = t_.children(x);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(*it);
  }
  return c;
}

void Catalog_fold::column_def(uint32_t n, Table &tb, std::optional<size_t> at) {
  const uint32_t spec = child(n, s_.field_spec);
  Column c = column(spec, tb);
  if (tb.column(c.name) != nullptr) drop_column_checks(tb, c.name);  // redefined
  column_check(spec, c.name, tb);
  if (const uint32_t refs = child(n, s_.references); refs != npos) {
    tb.foreign_keys.push_back(
        references(refs, constraint_name(child(n, s_.opt_constraint)), {c.name}, n));
  }
  if (Column *existing = tb.column(c.name)) {
    *existing = std::move(c);
  } else if (at && *at <= tb.columns.size()) {
    tb.columns.insert(tb.columns.begin() + static_cast<ptrdiff_t>(*at), std::move(c));
  } else {
    tb.columns.push_back(std::move(c));
  }
}

std::string Catalog_fold::constraint_name(uint32_t opt_constraint) const {
  const uint32_t c = child(opt_constraint, s_.constraint);
  return c == npos ? "" : name_of(child(c, s_.opt_ident));
}

Foreign_key Catalog_fold::references(uint32_t refs, std::string name,
                                     std::vector<std::string> columns, uint32_t at) const {
  Foreign_key fk;
  fk.name = std::move(name);
  fk.columns = std::move(columns);
  const uint32_t ti = child(refs, s_.table_ident);
  std::vector<uint32_t> ids;
  for (const uint32_t c : t_.children(ti)) {
    if (t_.node(c).symbol == s_.ident) ids.push_back(c);
  }
  if (ids.size() >= 2) {
    fk.ref_schema = name_of(ids[0]);
    fk.ref_table = name_of(ids[1]);
  } else if (!ids.empty()) {
    fk.ref_table = name_of(ids[0]);
  }
  for (const uint32_t c : find_all(child(refs, s_.opt_ref_list), s_.ident)) {
    fk.ref_columns.push_back(name_of(c));
  }
  // ON UPDATE x / ON DELETE y, in either order
  const uint32_t od = child(refs, s_.opt_on_update_delete);
  const auto ch = t_.children(od == npos ? at : od);
  for (size_t i = 0; od != npos && i + 2 < ch.size(); ++i) {
    if (t_.node(ch[i + 2]).symbol != s_.delete_option) continue;
    const std::string action = upper(text(ch[i + 2]));
    if (t_.node(ch[i + 1]).symbol == s_.UPDATE_SYM) fk.on_update = action;
    if (t_.node(ch[i + 1]).symbol == s_.DELETE_SYM) fk.on_delete = action;
  }
  if (const uint32_t m = child(refs, s_.opt_match_clause); non_empty(m)) {
    fk.match = upper(text(t_.children(m).back()));
  }
  fk.origin = origin(range(at), range(at));
  return fk;
}

void Catalog_fold::key_def(uint32_t n, Table &tb) {
  const int first = first_symbol(n);
  const std::string cname = constraint_name(child(n, s_.opt_constraint));
  uint32_t id = child(n, s_.opt_ident);
  if (id == npos || !non_empty(id)) id = child(n, s_.ident);
  // The name after KEY / FOREIGN KEY, else the CONSTRAINT name.
  const std::string index_name = non_empty(id) ? name_of(id) : "";
  const std::string name = index_name.empty() ? cname : index_name;

  if (has_child(n, s_.FOREIGN)) {
    // The constraint is named by CONSTRAINT c, else by the name after
    // FOREIGN KEY, which also names the index.
    std::vector<std::string> cols;
    for (auto &kp : key_parts(n)) cols.push_back(std::move(kp.column));
    Foreign_key fk = references(child(n, s_.references), cname.empty() ? index_name : cname,
                                std::move(cols), n);
    fk.index_name = index_name;
    fk.origin = origin(range(n), non_empty(id) ? range(id) : range(n));
    auto it = find_named(tb.foreign_keys, fk.name);
    if (!fk.name.empty() && it != tb.foreign_keys.end()) *it = std::move(fk);
    else tb.foreign_keys.push_back(std::move(fk));
    return;
  }
  Index ix;
  ix.name = name;
  index_details(n, ix);
  ix.origin = origin(range(n), non_empty(id) ? range(id) : range(n));
  if (const uint32_t ckt = child(n, s_.constraint_key_type); ckt != npos) {
    ix.kind = has_child(ckt, s_.PRIMARY_SYM) ? "primary" : "unique";
  } else if (first == s_.fulltext) {
    ix.kind = "fulltext";
  } else if (first == s_.spatial_or_vector) {
    ix.kind = lower(text(child(n, s_.spatial_or_vector)));
  } else {
    ix.kind = "index";
  }
  if (ix.kind == "primary") {
    for (const auto &kp : ix.key_parts) {
      if (Column *col = tb.column(kp.column)) col->nullable = false;
    }
    auto it = std::find_if(tb.indexes.begin(), tb.indexes.end(),
                           [](const Index &i) { return i.kind == "primary"; });
    if (it != tb.indexes.end()) {
      *it = std::move(ix);
      return;
    }
  }
  // A named key replaces an index of that name, as CREATE INDEX does; the
  // server would refuse the duplicate, so either reading is a guess.
  if (ix.name.empty()) {
    tb.indexes.push_back(std::move(ix));
  } else {
    upsert_named(tb.indexes, std::move(ix));
  }
}

void Catalog_fold::check_def(uint32_t n, Table &tb) {
  tb.checks.push_back(make_check(child(n, s_.check_constraint),
                                 constraint_name(child(n, s_.opt_constraint)), range(n)));
}

// Returns whether the options hold WITH SYSTEM VERSIONING.
bool Catalog_fold::table_options(uint32_t n,
                                 std::vector<std::pair<std::string, std::string>> &opts) {
  bool versioning = false;
  for (const uint32_t o : find_all(n, s_.create_table_option)) {
    const auto ch = t_.children(o);
    if (ch.empty()) continue;
    const uint32_t first = ch[0];
    const int sym = t_.node(first).symbol;
    if (sym == s_.default_charset) {
      set_option(opts, "charset", lower(text(child(first, s_.charset_name_or_default))));
    } else if (sym == s_.default_collation) {
      set_option(opts, "collation", lower(text(child(first, s_.collation_name_or_default))));
    } else if (sym == s_.COMMENT_SYM) {
      set_option(opts, "comment", unquote_string(raw(ch.back())));
    } else if (sym == s_.ENGINE_SYM) {
      set_option(opts, "engine", unquote_string(text(ch.back())));
    } else if (sym == s_.versioning_option) {
      versioning = true;
    } else if (sym == s_.engine_defined_option) {
      engine_option(first, opts);
    } else if (t_.is_token(first)) {
      // The value: what follows `=`, else the key word (UNION=(a,b), STORAGE DISK).
      size_t from = 1;
      for (size_t i = 0; i < ch.size(); ++i) {
        if (t_.node(ch[i]).symbol == s_.opt_equal) from = i + 1;
      }
      set_option(opts, lower(raw(first)),
                 from < ch.size() ? text_span(t_.begin(ch[from]), o) : text(ch.back()));
    }
  }
  return versioning;
}

bool Catalog_fold::create_routine(uint32_t n) {
  Routine r;
  uint32_t name_node = npos;
  uint32_t params = npos;
  if (has_child(n, s_.PROCEDURE_SYM)) {
    r.kind = "procedure";
    const uint32_t tail = child(n, s_.sp_tail_standalone);
    name_node = child(tail, s_.sp_name);
    params = tail;
    routine_characteristics(child(tail, s_.sp_c_chistics), r);
    r.body = body_of(child(tail, s_.sp_proc_stmt));
  } else if (has_child(n, s_.FUNCTION_SYM)) {
    r.kind = "function";
    name_node = child(n, s_.sp_name);
    if (name_node == npos) name_node = child(n, s_.ident);  // a UDF: ... SONAME
    params = n;
    const uint32_t ret = child(n, s_.sf_returned_type_clause);
    const uint32_t rt = child(ret, s_.sf_return_type);
    r.returns = text(rt != npos ? rt : ret);
    r.aggregate = non_empty(child(n, s_.opt_aggregate));
    if (const uint32_t cb = child(n, s_.sf_c_chistics_and_body_standalone); cb != npos) {
      routine_characteristics(child(cb, s_.sp_c_chistics), r);
      r.body = body_of(child(cb, s_.sp_proc_stmt_in_returns_clause));
    } else if (const uint32_t so = child(n, s_.TEXT_STRING_sys); so != npos) {
      r.soname = unquote_string(raw(so));  // a UDF: RETURNS type SONAME 'lib'
      if (const uint32_t ut = find(n, s_.udf_type); ut != npos) r.returns = upper(text(ut));
    }
  } else {
    return false;  // PACKAGE (Oracle mode only)
  }
  std::string schema_name = c_.m_current;
  uint32_t id = name_node;
  if (t_.node(name_node).symbol == s_.sp_name) {
    auto q = qualified(name_node);
    schema_name = q.first;
    id = q.second;
  }
  r.name = name_of(id);
  r.origin = origin(id);
  r.definer = definer_of(n);
  for (const uint32_t p : find_all(params, s_.sp_param_default)) {
    Parameter prm;
    const uint32_t nm = find(p, s_.sp_param_name_and_mode);
    prm.mode = upper(text(child(nm, s_.sp_parameter_type)));
    prm.name = name_of(find(nm, s_.ident));
    prm.type = text_span(t_.end(nm), p);
    r.parameters.push_back(std::move(prm));
  }
  Schema &sc = c_.schema(schema_name);
  auto it = std::find_if(sc.routines.begin(), sc.routines.end(), [&](const Routine &x) {
    return iequals(x.name, r.name) && x.kind == r.kind;
  });
  if (it != sc.routines.end()) *it = std::move(r); else sc.routines.push_back(std::move(r));
  return true;
}

bool Catalog_fold::alter(uint32_t n) {
  if (has_child(n, s_.DATABASE)) {
    const uint32_t id = child(n, s_.ident);
    Schema &sc = c_.schema(id != npos ? name_of(id) : c_.m_current);
    for (const uint32_t o : find_all(n, s_.create_database_option)) {
      if (const uint32_t cs = child(o, s_.default_charset); cs != npos) {
        set_option(sc.options, "charset", lower(text(child(cs, s_.charset_name_or_default))));
      } else if (const uint32_t co = child(o, s_.default_collation); co != npos) {
        set_option(sc.options, "collation", lower(text(child(co, s_.collation_name_or_default))));
      }
    }
    return true;
  }
  if (has_child(n, s_.VIEW_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.table_ident));
    upsert_named(c_.schema(schema_name).views, make_view(n, id));
    return true;
  }
  if (has_child(n, s_.PROCEDURE_SYM) || has_child(n, s_.FUNCTION_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.sp_name));
    const char *kind = has_child(n, s_.PROCEDURE_SYM) ? "procedure" : "function";
    const std::string name = name_of(id);
    Schema *sc = c_.find_schema(schema_name);
    if (sc == nullptr) return false;
    auto it = std::find_if(sc->routines.begin(), sc->routines.end(),
                           [&](const Routine &r) { return is_routine(r, kind, name); });
    if (it == sc->routines.end()) return false;
    routine_characteristics(child(n, s_.sp_a_chistics), *it);
    return true;
  }
  if (has_child(n, s_.EVENT_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.sp_name));
    Schema *sc = c_.find_schema(schema_name);
    if (sc == nullptr) return false;
    auto it = find_named(sc->events, name_of(id));
    if (it == sc->events.end()) return false;
    if (const std::string d = definer_of(n); !d.empty()) it->definer = d;
    event_details(n, child(child(n, s_.opt_ev_sql_stmt), s_.ev_sql_stmt), *it);
    if (const uint32_t rn = child(child(n, s_.opt_ev_rename_to), s_.sp_name); rn != npos) {
      auto [to_schema, to_id] = qualified(rn);
      Event moved = std::move(*it);
      sc->events.erase(it);
      moved.name = name_of(to_id);
      upsert_named(c_.schema(to_schema).events, std::move(moved));
    }
    return true;
  }
  if (has_child(n, s_.SEQUENCE_SYM)) {
    auto [schema_name, id] = qualified(child(n, s_.table_ident));
    Schema *sc = c_.find_schema(schema_name);
    if (sc == nullptr) return false;
    auto it = find_named(sc->sequences, name_of(id));
    if (it == sc->sequences.end()) return false;
    sequence_options(child(n, s_.sequence_defs), *it);
    return true;
  }
  if (!has_child(n, s_.TABLE_SYM)) return false;

  auto [schema_name, id] = qualified(child(n, s_.table_ident));
  Schema *sc = &c_.schema(schema_name);
  Table *tb = sc->table(name_of(id));
  if (tb == nullptr) {
    note(Note_kind::unknown_table, false,
         "ALTER TABLE " + name_of(id) + ": the script does not create this table");
    return false;
  }
  for (const uint32_t item : find_all(child(n, s_.alter_commands), s_.alter_list_item)) {
    const auto ch = t_.children(item);
    if (ch.empty()) continue;
    const int first = t_.node(ch[0]).symbol;
    auto place = [&](uint32_t opt_place) -> std::optional<size_t> {
      if (!non_empty(opt_place)) return std::nullopt;
      if (has_child(opt_place, s_.FIRST_SYM)) return size_t{0};
      const std::string after = name_of(find(opt_place, s_.ident));
      for (size_t i = 0; i < tb->columns.size(); ++i) {
        if (iequals(tb->columns[i].name, after)) return i + 1;
      }
      return std::nullopt;
    };
    if (first == s_.add_column) {
      if (const uint32_t cd = child(item, s_.column_def); cd != npos) {
        column_def(cd, *tb, place(child(item, s_.opt_place)));
      } else {
        field_list_items(child(item, s_.create_field_list), *tb);
      }
    } else if (first == s_.ADD) {
      if (const uint32_t kd = child(item, s_.key_def); kd != npos) {
        key_def(kd, *tb);
      } else if (const uint32_t cd = child(item, s_.constraint_def); cd != npos) {
        check_def(cd, *tb);
      } else if (const uint32_t cc = child(item, s_.check_constraint); cc != npos) {
        // ADD CONSTRAINT IF NOT EXISTS c CHECK (...)
        const std::string cname = name_of(child(item, s_.field_ident));
        if (find_named(tb->checks, cname) == tb->checks.end()) {
          tb->checks.push_back(make_check(cc, cname, {t_.begin(child(item, s_.CONSTRAINT)), t_.end(item)}));
        }
      } else if (has_child(item, s_.VERSIONING_SYM)) {  // ADD SYSTEM VERSIONING
        tb->system_versioning = true;
      } else {
        add_period(item, *tb);
      }
    } else if (first == s_.CHANGE || first == s_.MODIFY_SYM) {
      const uint32_t spec = child(item, s_.field_spec);
      Column c = column(spec, *tb);
      const std::string old = first == s_.CHANGE ? name_of(child(item, s_.field_ident)) : c.name;
      // The column's own CHECK is part of its definition.
      drop_column_checks(*tb, old);
      column_check(spec, c.name, *tb);
      rename_column_refs(*tb, old, c.name);
      auto it = find_named(tb->columns, old);
      const auto at = place(child(item, s_.opt_place));
      if (it != tb->columns.end()) {
        if (at) {
          tb->columns.erase(it);
          tb->columns.insert(tb->columns.begin() + static_cast<ptrdiff_t>(std::min(*at, tb->columns.size())), std::move(c));
        } else {
          *it = std::move(c);
        }
      } else {
        tb->columns.push_back(std::move(c));
      }
    } else if (first == s_.DROP && has_child(item, s_.PERIOD_SYM)) {
      const uint32_t pid = child(item, s_.ident);
      const std::string what = pid == npos ? "SYSTEM_TIME" : name_of(pid);
      std::erase_if(tb->periods, [&](const Period &p) { return iequals(p.name, what); });
    } else if (first == s_.DROP && has_child(item, s_.VERSIONING_SYM)) {
      // DROP SYSTEM VERSIONING drops the row start/end columns and the period.
      tb->system_versioning = false;
      std::erase_if(tb->columns, [](const Column &c) { return c.generated_row.has_value(); });
      std::erase_if(tb->periods, [](const Period &p) { return iequals(p.name, "SYSTEM_TIME"); });
    } else if (first == s_.DROP) {
      const std::string what = name_of(child(item, s_.field_ident));
      if (has_child(item, s_.FOREIGN)) {
        auto it = find_named(tb->foreign_keys, what);
        if (it != tb->foreign_keys.end()) tb->foreign_keys.erase(it);
      } else if (has_child(item, s_.PRIMARY_SYM)) {
        std::erase_if(tb->indexes, [](const Index &i) { return i.kind == "primary"; });
      } else if (has_child(item, s_.key_or_index)) {
        auto it = find_named(tb->indexes, what);
        if (it != tb->indexes.end()) tb->indexes.erase(it);
      } else if (has_child(item, s_.CONSTRAINT)) {
        std::erase_if(tb->checks, [&](const Check &c) { return iequals(c.name, what); });
        std::erase_if(tb->foreign_keys, [&](const Foreign_key &f) { return iequals(f.name, what); });
        std::erase_if(tb->indexes, [&](const Index &i) { return iequals(i.name, what); });
      } else if (!what.empty()) {
        auto it = find_named(tb->columns, what);
        if (it != tb->columns.end()) tb->columns.erase(it);
        drop_column_checks(*tb, what);
        // The server drops the column from its indexes, and an index left empty.
        for (auto &ix : tb->indexes) {
          std::erase_if(ix.key_parts, [&](const Key_part &kp) { return iequals(kp.column, what); });
        }
        std::erase_if(tb->indexes, [](const Index &i) { return i.key_parts.empty(); });
      }
    } else if (first == s_.ALTER && has_child(item, s_.ignorability)) {
      // ALTER INDEX i [NOT] IGNORED
      auto it = find_named(tb->indexes, name_of(child(item, s_.ident)));
      if (it != tb->indexes.end()) it->ignored = !has_child(child(item, s_.ignorability), s_.NOT_SYM);
    } else if (first == s_.ALTER && has_child(item, s_.field_ident)) {
      if (Column *c = tb->column(name_of(child(item, s_.field_ident)))) {
        if (const uint32_t d = child(item, s_.column_default_expr); d != npos) {
          c->default_value = text(d);
          c->default_origin = origin(range(d), range(d));
        } else if (has_child(item, s_.DROP)) {
          c->default_value.reset();
          c->default_origin.reset();
        }
      }
    } else if (first == s_.RENAME) {
      std::vector<uint32_t> ids;
      for (const uint32_t c : ch) {
        const int sym = t_.node(c).symbol;
        if (sym == s_.ident || sym == s_.field_ident) ids.push_back(c);
      }
      if (const uint32_t to = child(item, s_.table_ident); to != npos) {
        auto [to_schema, to_id] = qualified(to);
        Table moved = std::move(*tb);
        moved.name = name_of(to_id);
        std::erase_if(sc->tables, [&](const Table &x) { return iequals(x.name, name_of(id)); });
        Schema &dst = c_.schema(to_schema);
        dst.tables.push_back(std::move(moved));
        sc = &dst;
        tb = &dst.tables.back();
      } else if (has_child(item, s_.COLUMN_SYM) && ids.size() == 2) {
        if (Column *c = tb->column(name_of(ids[0]))) {
          c->name = name_of(ids[1]);
          rename_column_refs(*tb, name_of(ids[0]), c->name);
        }
      } else if (has_child(item, s_.key_or_index) && ids.size() == 2) {
        auto it = find_named(tb->indexes, name_of(ids[0]));
        if (it != tb->indexes.end()) it->name = name_of(ids[1]);
      }
    } else if (first == s_.CONVERT_SYM) {
      set_option(tb->options, "charset", lower(text(find(item, s_.charset_name_or_default))));
      if (const uint32_t co = child(item, s_.collation_name_or_default); co != npos) {
        set_option(tb->options, "collation", lower(text(co)));
      }
    } else if (first == s_.create_table_options_space_separated) {
      if (table_options(ch[0], tb->options)) tb->system_versioning = true;
    }
  }
  const uint32_t cmds = child(n, s_.alter_commands);
  if (const uint32_t p = child(cmds, s_.partitioning); p != npos) {
    tb->partitioning = text(p);
  } else if (const uint32_t op = find(child(cmds, s_.opt_partitioning), s_.partitioning); op != npos) {
    tb->partitioning = text(op);
  } else if (child(cmds, s_.remove_partitioning) != npos) {
    tb->partitioning.clear();
  }
  return true;
}

bool Catalog_fold::drop(uint32_t n) {
  if (const uint32_t r = child(n, s_.drop_routine); r != npos) {
    const bool proc = find(child(r, s_.sp_handler), s_.PROCEDURE_SYM) != npos;
    std::vector<uint32_t> ids;
    for (const uint32_t c : t_.children(r)) {
      if (t_.node(c).symbol == s_.ident) ids.push_back(c);
    }
    if (ids.empty()) return false;
    Schema *sc = c_.find_schema(ids.size() >= 2 ? name_of(ids[0]) : c_.m_current);
    if (sc == nullptr) return true;
    const std::string name = name_of(ids.back());
    std::erase_if(sc->routines, [&](const Routine &x) {
      return is_routine(x, proc ? "procedure" : "function", name);
    });
    return true;
  }
  auto drop_named = [&](auto member, uint32_t sp_or_table) {
    auto [schema_name, id] = qualified(sp_or_table);
    if (Schema *sc = c_.find_schema(schema_name)) {
      auto &v = sc->*member;
      std::erase_if(v, [&](const auto &x) { return iequals(x.name, name_of(id)); });
    }
  };
  if (has_child(n, s_.DATABASE)) {
    const std::string name = name_of(child(n, s_.ident));
    std::erase_if(c_.m_schemas, [&](const Schema &s) { return iequals(s.name, name); });
    return true;
  }
  if (has_child(n, s_.INDEX_SYM)) {
    auto [schema_name, tid] = qualified(child(n, s_.table_ident));
    if (Table *tb = c_.schema(schema_name).table(name_of(tid))) {
      const std::string name = name_of(child(n, s_.ident));
      std::erase_if(tb->indexes, [&](const Index &i) { return iequals(i.name, name); });
    }
    return true;
  }
  if (has_child(n, s_.TRIGGER_SYM)) {
    drop_named(&Schema::triggers, child(n, s_.sp_name));
    return true;
  }
  if (has_child(n, s_.EVENT_SYM)) {
    drop_named(&Schema::events, child(n, s_.sp_name));
    return true;
  }
  const uint32_t list = child(n, s_.table_list);
  if (list == npos) return false;  // USER, ROLE, SERVER
  for (const uint32_t ti : find_all(list, s_.table_ident)) {
    if (has_child(n, s_.VIEW_SYM)) drop_named(&Schema::views, ti);
    else if (has_child(n, s_.SEQUENCE_SYM)) drop_named(&Schema::sequences, ti);
    else drop_named(&Schema::tables, ti);
  }
  return true;
}

bool Catalog_fold::rename(uint32_t n) {
  // RENAME TABLE renames a table, a view or a sequence (one namespace).
  // MariaDB refuses to move a view to another schema (error 1450), and then
  // renames nothing of the statement.
  struct Pair {
    std::string from_schema, from_name, to_schema, to_name;
  };
  std::vector<Pair> pairs;
  // The views an earlier pair of the statement renamed (a chain a TO b, b TO c).
  std::vector<std::pair<std::string, std::string>> renamed_views;
  auto renamed_view = [&](const std::string &schema, const std::string &name) {
    return std::find_if(renamed_views.begin(), renamed_views.end(), [&](const auto &v) {
      return iequals(v.first, schema) && iequals(v.second, name);
    });
  };
  for (const uint32_t tt : find_all(n, s_.table_to_table)) {
    std::vector<uint32_t> idents;
    for (const uint32_t c : t_.children(tt)) {
      if (t_.node(c).symbol == s_.table_ident) idents.push_back(c);
    }
    if (idents.size() != 2) continue;
    auto [from_schema, from_id] = qualified(idents[0]);
    auto [to_schema, to_id] = qualified(idents[1]);
    const std::string from_name = name_of(from_id);
    const std::string to_name = name_of(to_id);
    // Not looked up here: an earlier pair may create its schema.
    const Schema *from = c_.find_schema(from_schema);
    const auto rv = renamed_view(from_schema, from_name);
    const bool is_view = rv != renamed_views.end() || (from != nullptr && from->view(from_name) != nullptr);
    if (is_view) {
      if (!iequals(to_schema, from_schema)) return false;
      if (rv != renamed_views.end()) renamed_views.erase(rv);
      renamed_views.push_back({to_schema, to_name});
    }
    pairs.push_back({from_schema, from_name, to_schema, to_name});
  }
  bool changed = false;
  for (const auto &p : pairs) {
    // Looked up here: c_.schema() below may add a schema and move them.
    Schema *from = c_.find_schema(p.from_schema);
    if (from == nullptr) continue;
    auto move = [&](auto member) {
      auto &v = from->*member;
      auto it = find_named(v, p.from_name);
      if (it == v.end()) return false;
      auto moved = std::move(*it);
      v.erase(it);
      moved.name = p.to_name;
      (c_.schema(p.to_schema).*member).push_back(std::move(moved));
      return true;
    };
    if (move(&Schema::tables) || move(&Schema::views) || move(&Schema::sequences)) changed = true;
  }
  return changed;
}

// REST statements resolve the service and schema they do not name as
// MariaDB Shell's MRS DDL executor (mysqlshdk/libs/mrs/ddl_executor*.cc)
// does: only USE REST SERVICE sets the current service, and clears the
// current schema; USE REST SCHEMA sets the current schema of the current
// service; CREATE REST SCHEMA makes the first schema of the current service
// the current one.  A statement that needs a service or schema when none is
// named or in use fails in the shell, and gets a note here.
bool Catalog_fold::rest(uint32_t n) {
  const auto ch = t_.children(n);
  if (ch.empty()) return false;
  const uint32_t st = ch[0];
  const int sym = t_.node(st).symbol;
  const std::string service_path = text(find(st, s_.mrs_service_request_path));
  const uint32_t schema_node = find(st, s_.mrs_schema_request_path);
  const std::string schema_path = text(schema_node);
  // The statement up to the first path or name it writes, for notes:
  // "CREATE REST VIEW /v".
  auto what = [&]() {
    uint32_t first = npos;
    for (const int path_sym : {s_.mrs_service_request_path, s_.mrs_schema_request_path,
                               s_.mrs_view_request_path, s_.mrs_procedure_request_path,
                               s_.mrs_function_request_path, s_.mrs_content_set_request_path,
                               s_.mrs_identifier}) {
      const uint32_t p = find(st, path_sym);
      if (p != npos && (first == npos || t_.begin(p) < t_.begin(first))) first = p;
    }
    return first == npos ? text(st) : text_span(t_.begin(st), first);
  };
  auto none_in_use = [&](const char *which) {
    note(Note_kind::rest_none_in_use, false,
         what() + ": names no REST " + which + ", and none is in use (USE REST " + which + ")");
  };
  // The service the statement names, else the current one; nullopt (with a
  // note) when there is neither.
  auto service_named = [&]() -> std::optional<std::string> {
    if (!service_path.empty()) return service_path;
    if (!c_.m_current_service.empty()) return c_.m_current_service;
    none_in_use("SERVICE");
    return std::nullopt;
  };
  // The service and schema of a REST object: ON [SERVICE ...] SCHEMA, else
  // the current schema.
  // (The grammar names a service only together with a schema.)
  auto object_place = [&]() -> std::optional<std::pair<std::string, std::string>> {
    auto svc = service_named();
    if (!svc) return std::nullopt;
    if (!schema_path.empty()) return std::pair{*svc, schema_path};
    if (c_.m_current_rest_schema.empty()) { none_in_use("SCHEMA"); return std::nullopt; }
    return std::pair{*svc, c_.m_current_rest_schema};
  };
  auto object = [&](const char *kind, int path_sym) {
    const auto place = object_place();
    if (!place) return false;
    Rest_service &svc = c_.service(place->first);
    Rest_schema *it = find_rest_schema(svc, place->second);
    if (it == nullptr) it = &svc.schemas.emplace_back(Rest_schema{place->second, "", {}, {stmt_, stmt_}});
    const uint32_t p = find(st, path_sym);
    Rest_object o{kind, text(p), text(find(st, s_.mrs_qualified_identifier)), origin(p)};
    auto ex = std::find_if(it->objects.begin(), it->objects.end(), [&](const Rest_object &x) {
      return iequals(x.path, o.path) && x.kind == o.kind;
    });
    if (ex != it->objects.end()) *ex = std::move(o); else it->objects.push_back(std::move(o));
    return true;
  };
  auto name_at = [&](uint32_t p) { return unquote_any(text(p)); };
  auto named = [&](std::vector<Named_object> &v, int name_sym) {
    const uint32_t p = find(st, name_sym);
    upsert_named(v, Named_object{name_at(p), origin(p)});
  };
  // ADD AUTH APP links (a linked app stays linked, its range moves to this
  // statement), then REMOVE AUTH APP unlinks, as the shell does; a CREATE
  // of a new service ignores REMOVE (`removes` false), as the shell does.
  auto link_auth_apps = [&](Rest_service &svc, bool removes = true) {
    for (const uint32_t a : find_all(st, s_.mrs_add_auth_app)) {
      const uint32_t p = find(a, s_.mrs_auth_app_name);
      upsert_named(svc.auth_apps, Named_object{name_at(p), origin(p)});
    }
    if (!removes) return;
    for (const uint32_t r : find_all(st, s_.mrs_remove_auth_app)) {
      erase_named(svc.auth_apps, name_at(find(r, s_.mrs_auth_app_name)));
    }
  };

  if (sym == s_.mrs_create_rest_service_statement) {
    // OR REPLACE (and a CREATE of a service the file has) updates the
    // service in place: its schemas, content sets and the links the
    // statement does not name stay.  IF NOT EXISTS keeps it as it is.
    const uint32_t prefix = child(st, s_.mrs_create_rest_service_prefix);
    const bool existed = c_.find_service(service_path) != nullptr;
    if (existed && find(prefix, s_.IF_SYM) != npos) return true;
    Rest_service &svc = c_.service(service_path);
    svc.origin = origin(find(st, s_.mrs_service_request_path));
    link_auth_apps(svc, existed);
    return true;
  }
  if (sym == s_.mrs_alter_rest_service_statement) {
    const std::string path = text(child(st, s_.mrs_service_request_path));
    Rest_service *svc = c_.find_service(path);
    if (svc == nullptr) {
      note(Note_kind::unknown_rest_service, false,
           what() + ": the file does not create this REST service");
      return false;
    }
    const uint32_t renamed = child(st, s_.mrs_new_service_request_path);
    if (renamed != npos) {
      const std::string to = text(find(renamed, s_.mrs_service_request_path));
      if (iequals(c_.m_current_service, svc->path)) c_.m_current_service = to;
      svc->path = to;
    }
    link_auth_apps(*svc);
    return true;
  }
  if (sym == s_.mrs_use_statement) {
    // USE REST SERVICE p, USE REST [SERVICE p] SCHEMA s; USE REST METADATA
    // names neither.
    if (!service_path.empty()) {
      c_.m_current_service = service_path;
      c_.m_current_rest_schema.clear();
    }
    if (!schema_path.empty()) {
      if (c_.m_current_service.empty()) {
        none_in_use("SERVICE");
        return false;
      }
      c_.m_current_rest_schema = schema_path;
    }
    return false;
  }
  if (sym == s_.mrs_create_rest_schema_statement) {
    const auto svc_path = service_named();
    if (!svc_path) return false;
    Rest_service &svc = c_.service(*svc_path);
    Rest_schema rs;
    rs.schema = name_of(child(st, s_.mrs_identifier));
    // Without a request path the shell takes /<database schema>.
    rs.path = schema_node != npos ? schema_path : "/" + rs.schema;
    rs.origin = origin(schema_node != npos ? schema_node : child(st, s_.mrs_identifier));
    if (Rest_schema *it = find_rest_schema(svc, rs.path)) {
      // IF NOT EXISTS keeps the schema as it is, as the shell does.
      if (find(child(st, s_.mrs_create_rest_schema_prefix), s_.IF_SYM) != npos) return true;
      rs.objects = std::move(it->objects);
      *it = std::move(rs);
    } else {
      svc.schemas.push_back(std::move(rs));
      if (svc.schemas.size() == 1 && iequals(svc.path, c_.m_current_service))
        c_.m_current_rest_schema = svc.schemas[0].path;
    }
    return true;
  }
  if (sym == s_.mrs_create_rest_view_statement) return object("view", s_.mrs_view_request_path);
  if (sym == s_.mrs_create_rest_procedure_statement) return object("procedure", s_.mrs_procedure_request_path);
  if (sym == s_.mrs_create_rest_function_statement) return object("function", s_.mrs_function_request_path);
  if (sym == s_.mrs_create_rest_content_set_statement) {
    const auto svc_path = service_named();
    if (!svc_path) return false;
    named(c_.service(*svc_path).content_sets, s_.mrs_content_set_request_path);
    return true;
  }
  if (sym == s_.mrs_create_rest_auth_app_statement) { named(c_.m_auth_apps, s_.mrs_auth_app_name); return true; }
  if (sym == s_.mrs_create_rest_role_statement) { named(c_.m_roles, s_.mrs_role_name); return true; }
  if (sym == s_.mrs_create_rest_user_statement) {
    const std::string user = name_at(find(st, s_.mrs_user_name));
    const std::string app = name_at(find(st, s_.mrs_auth_app_name));
    const std::string name = user + "@" + app;
    auto it = find_named(c_.m_users, name);
    const Origin o = origin(find(st, s_.mrs_user_name));
    if (it != c_.m_users.end()) *it = {name, o}; else c_.m_users.push_back({name, o});
    return true;
  }
  if (sym == s_.mrs_drop_rest_service_statement) {
    erase_by_path(c_.m_services, service_path);
    if (iequals(c_.m_current_service, service_path)) {
      c_.m_current_service.clear();
      c_.m_current_rest_schema.clear();
    }
    return true;
  }
  // A DROP of an object the file never created changes nothing; a service
  // the script never created is left alone rather than made.
  if (sym == s_.mrs_drop_rest_schema_statement) {
    const auto svc_path = service_named();
    if (!svc_path) return false;
    if (Rest_service *svc = c_.find_service(*svc_path)) erase_by_path(svc->schemas, schema_path);
    if (iequals(*svc_path, c_.m_current_service) && iequals(schema_path, c_.m_current_rest_schema))
      c_.m_current_rest_schema.clear();
    return true;
  }
  auto drop_object = [&](const char *kind, int path_sym) {
    const auto place = object_place();
    if (!place) return false;
    Rest_service *svc = c_.find_service(place->first);
    Rest_schema *sch = svc != nullptr ? find_rest_schema(*svc, place->second) : nullptr;
    if (sch == nullptr) return true;
    const std::string path = text(find(st, path_sym));
    std::erase_if(sch->objects, [&](const Rest_object &o) { return o.kind == kind && iequals(o.path, path); });
    return true;
  };
  if (sym == s_.mrs_drop_rest_view_statement) return drop_object("view", s_.mrs_view_request_path);
  if (sym == s_.mrs_drop_rest_procedure_statement) return drop_object("procedure", s_.mrs_procedure_request_path);
  if (sym == s_.mrs_drop_rest_function_statement) return drop_object("function", s_.mrs_function_request_path);
  if (sym == s_.mrs_drop_rest_content_set_statement) {
    const auto svc_path = service_named();
    if (!svc_path) return false;
    if (Rest_service *svc = c_.find_service(*svc_path)) {
      erase_named(svc->content_sets, name_at(find(st, s_.mrs_content_set_request_path)));
    }
    return true;
  }
  if (sym == s_.mrs_drop_rest_auth_app_statement) {
    // Dropping an app drops its links.
    const std::string name = name_at(find(st, s_.mrs_auth_app_name));
    for (auto &svc : c_.m_services) erase_named(svc.auth_apps, name);
    erase_named(c_.m_auth_apps, name);
    return true;
  }
  if (sym == s_.mrs_drop_rest_role_statement) { erase_named(c_.m_roles, name_at(find(st, s_.mrs_role_name))); return true; }
  if (sym == s_.mrs_drop_rest_user_statement) {
    const std::string name = name_at(find(st, s_.mrs_user_name)) + "@" + name_at(find(st, s_.mrs_auth_app_name));
    erase_named(c_.m_users, name);
    return true;
  }
  return false;
}

bool Catalog::apply(const Syntax_tree &tree, std::string_view text) {
  return Catalog_fold(*this, tree, text).run();
}

}  // namespace mdbls
