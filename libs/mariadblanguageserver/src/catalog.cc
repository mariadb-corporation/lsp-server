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
#include <utility>

#include "text_util.h"

namespace mdbls {

namespace {

// Runs of blanks become one space; leading and trailing blanks go.
std::string collapse(std::string_view s) {
  std::string out;
  bool space = false;
  for (const char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      space = !out.empty();
    } else {
      if (space) out.push_back(' ');
      space = false;
      out.push_back(c);
    }
  }
  return out;
}

// `name`, "name" (ANSI_QUOTES) and plain names, without their quotes.
std::string unquote_identifier(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '`' || s.front() == '"') && s.back() == s.front()) {
    const char q = s.front();
    std::string out;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
      out.push_back(s[i]);
      if (s[i] == q && i + 2 < s.size() && s[i + 1] == q) ++i;  // doubled quote
    }
    return out;
  }
  return std::string(s);
}

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

template <typename T>
auto find_named(std::vector<T> &v, std::string_view name) {
  return std::find_if(v.begin(), v.end(), [&](const T &x) { return iequals(x.name, name); });
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

// --- lookups --------------------------------------------------------------

Column *Table::column(std::string_view n) {
  auto it = find_named(columns, n);
  return it == columns.end() ? nullptr : &*it;
}

const Column *Table::column(std::string_view n) const {
  return const_cast<Table *>(this)->column(n);
}

Table *Schema::table(std::string_view n) {
  auto it = find_named(tables, n);
  return it == tables.end() ? nullptr : &*it;
}

const Table *Schema::table(std::string_view n) const {
  return const_cast<Schema *>(this)->table(n);
}

const View *Schema::view(std::string_view n) const {
  auto &v = const_cast<std::vector<View> &>(views);
  auto it = find_named(v, n);
  return it == v.end() ? nullptr : &*it;
}

const Routine *Schema::routine(std::string_view n) const {
  auto &v = const_cast<std::vector<Routine> &>(routines);
  auto it = find_named(v, n);
  return it == v.end() ? nullptr : &*it;
}

const Schema *Catalog::find(std::string_view n) const {
  return const_cast<Catalog *>(this)->find_schema(n);
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

  // schema.name of a table_ident / sp_name (ident '.' ident | ident | '.' ident)
  std::pair<std::string, uint32_t> qualified(uint32_t n) const {
    std::vector<uint32_t> ids;
    for (const uint32_t c : t_.children(n)) {
      if (t_.node(c).symbol == s_.ident) ids.push_back(c);
    }
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
  void field_list_items(uint32_t n, Table &t);
  void column_def(uint32_t n, Table &t, std::optional<size_t> at = std::nullopt);
  Column column(uint32_t field_spec, Table &t);
  void key_def(uint32_t n, Table &t);
  void check_def(uint32_t n, Table &t);
  std::vector<std::string> key_columns(uint32_t key_list) const;
  Foreign_key references(uint32_t refs, std::string name, std::vector<std::string> columns,
                         uint32_t at) const;
  std::string constraint_name(uint32_t opt_constraint) const;
  void table_options(uint32_t n, std::vector<std::pair<std::string, std::string>> &opts);
  static void set_option(std::vector<std::pair<std::string, std::string>> &opts,
                         const std::string &key, std::string value);

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
    sc.sequences.push_back({name, origin(id)});
    return true;
  }
  if (has_child(n, s_.INDEX_SYM)) {  // CREATE [UNIQUE|FULLTEXT|SPATIAL] INDEX i ON t (...)
    auto [schema_name, tid] = qualified(child(n, s_.table_ident));
    Table *tb = c_.schema(schema_name).table(name_of(tid));
    if (tb == nullptr) {
      c_.m_notes.push_back({stmt_.begin, "CREATE INDEX on table " + name_of(tid) +
                                             ", which the script does not create"});
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
    ix.columns = key_columns(child(n, s_.key_list));
    if (ix.columns.empty()) ix.columns.push_back(text(find(n, s_.key_part)));
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
    upsert_named(c_.schema(schema_name).events, Named_object{name_of(id), origin(id)});
    return true;
  }
  return false;  // USER, ROLE, SERVER
}

// CREATE VIEW and ALTER VIEW: view_list_opt, view_select.
View Catalog_fold::make_view(uint32_t n, uint32_t id) const {
  View v;
  v.name = name_of(id);
  for (const uint32_t c : find_all(child(n, s_.view_list_opt), s_.ident)) v.columns.push_back(name_of(c));
  v.definition = text(child(n, s_.view_select));
  v.origin = origin(id);
  return v;
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
  tb.name = name;
  tb.temporary = non_empty(child(n, s_.opt_temporary));
  tb.origin = origin(id);
  const uint32_t body = child(n, s_.create_body);
  if (const uint32_t like = child(body, s_.create_like); like != npos) {
    auto [src_schema, src_id] = qualified(find(like, s_.table_ident));
    if (Table *src = c_.schema(src_schema).table(name_of(src_id))) {
      tb.columns = src->columns;
      tb.indexes = src->indexes;
      tb.checks = src->checks;
      tb.options = src->options;
    } else {
      c_.m_notes.push_back({stmt_.begin, "CREATE TABLE ... LIKE " + name_of(src_id) +
                                             ", which the script does not create"});
    }
  }
  if (const uint32_t list = child(body, s_.create_field_list_parens); list != npos) {
    field_list_items(list, tb);
  }
  table_options(child(body, s_.opt_create_table_options), tb.options);

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
    c.type = text(q);
  } else {
    // SERIAL: BIGINT UNSIGNED NOT NULL AUTO_INCREMENT UNIQUE
    c.type = "SERIAL";
    c.nullable = false;
    c.auto_increment = true;
  }
  const uint32_t def = child(type, s_.field_def);
  if (def == npos) return c;
  if (const uint32_t g = child(def, s_.virtual_column_func); g != npos) c.generated = text(g);

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
        c.collation = lower(text(child(x, s_.collation_name)));
      }
    } else if (sym == s_.asrow_attribute) {
      if (has_child(x, s_.NULL_SYM)) {
        c.nullable = false;  // NOT NULL
      } else if (has_child(x, s_.KEY_SYM)) {  // [PRIMARY] KEY
        Index ix{"", "primary", {c.name}, c.origin};
        tb.indexes.push_back(std::move(ix));
        c.nullable = false;
      }
    } else if (sym == s_.vcol_attribute) {
      if (has_child(x, s_.UNIQUE_SYM)) {
        tb.indexes.push_back(Index{"", "unique", {c.name}, c.origin});
      } else if (const uint32_t cm = child(x, s_.TEXT_STRING_sys); cm != npos) {
        c.comment = unquote_string(raw(cm));
      }
    }
    const auto ch = t_.children(x);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(*it);
  }
  return c;
}

void Catalog_fold::column_def(uint32_t n, Table &tb, std::optional<size_t> at) {
  Column c = column(child(n, s_.field_spec), tb);
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

std::vector<std::string> Catalog_fold::key_columns(uint32_t key_list) const {
  std::vector<std::string> cols;
  for (const uint32_t part : find_all(key_list, s_.key_part)) {
    const uint32_t id = find(part, s_.ident);
    cols.push_back(id != npos ? name_of(id) : text(part));
  }
  return cols;
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
  fk.origin = origin(range(at), range(at));
  return fk;
}

void Catalog_fold::key_def(uint32_t n, Table &tb) {
  const int first = first_symbol(n);
  const std::string cname = constraint_name(child(n, s_.opt_constraint));
  uint32_t id = child(n, s_.opt_ident);
  if (id == npos || !non_empty(id)) id = child(n, s_.ident);
  std::string name = non_empty(id) ? name_of(id) : cname;
  const auto cols = key_columns(child(n, s_.key_list));

  if (has_child(n, s_.FOREIGN)) {
    Foreign_key fk = references(child(n, s_.references), name, cols, n);
    fk.origin = origin(range(n), non_empty(id) ? range(id) : range(n));
    auto it = find_named(tb.foreign_keys, fk.name);
    if (!fk.name.empty() && it != tb.foreign_keys.end()) *it = std::move(fk);
    else tb.foreign_keys.push_back(std::move(fk));
    return;
  }
  Index ix;
  ix.name = name;
  ix.columns = cols;
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
    for (const auto &c : ix.columns) {
      if (Column *col = tb.column(c)) col->nullable = false;
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
  Check ck;
  ck.name = constraint_name(child(n, s_.opt_constraint));
  const uint32_t cc = child(n, s_.check_constraint);
  const uint32_t e = find(cc, s_.expr);
  ck.expression = text(e != npos ? e : cc);
  ck.origin = origin(range(n), range(n));
  tb.checks.push_back(std::move(ck));
}

void Catalog_fold::table_options(uint32_t n,
                                 std::vector<std::pair<std::string, std::string>> &opts) {
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
    } else if (t_.is_token(first)) {
      set_option(opts, lower(raw(first)), text(ch.back()));
    }
  }
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
  } else if (has_child(n, s_.FUNCTION_SYM)) {
    r.kind = "function";
    name_node = child(n, s_.sp_name);
    if (name_node == npos) name_node = child(n, s_.ident);  // a UDF: ... SONAME
    params = n;
    const uint32_t ret = child(n, s_.sf_returned_type_clause);
    const uint32_t rt = child(ret, s_.sf_return_type);
    r.returns = text(rt != npos ? rt : ret);
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
  for (const uint32_t p : find_all(params, s_.sp_param_default)) {
    Parameter prm;
    const uint32_t nm = find(p, s_.sp_param_name_and_mode);
    prm.mode = upper(text(child(nm, s_.sp_parameter_type)));
    prm.name = name_of(find(nm, s_.ident));
    prm.type = collapse(text_.substr(t_.end(nm), t_.end(p) - t_.end(nm)));
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
  if (!has_child(n, s_.TABLE_SYM)) return false;

  auto [schema_name, id] = qualified(child(n, s_.table_ident));
  Schema *sc = &c_.schema(schema_name);
  Table *tb = sc->table(name_of(id));
  if (tb == nullptr) {
    c_.m_notes.push_back({stmt_.begin, "ALTER TABLE " + name_of(id) +
                                           ": the script does not create this table"});
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
      if (const uint32_t kd = child(item, s_.key_def); kd != npos) key_def(kd, *tb);
      else if (const uint32_t cd = child(item, s_.constraint_def); cd != npos) check_def(cd, *tb);
    } else if (first == s_.CHANGE || first == s_.MODIFY_SYM) {
      const uint32_t spec = child(item, s_.field_spec);
      Column c = column(spec, *tb);
      const std::string old = first == s_.CHANGE ? name_of(child(item, s_.field_ident)) : c.name;
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
      }
    } else if (first == s_.ALTER && has_child(item, s_.field_ident)) {
      if (Column *c = tb->column(name_of(child(item, s_.field_ident)))) {
        if (const uint32_t d = child(item, s_.column_default_expr); d != npos) c->default_value = text(d);
        else if (has_child(item, s_.DROP)) c->default_value.reset();
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
        if (Column *c = tb->column(name_of(ids[0]))) c->name = name_of(ids[1]);
      } else if (has_child(item, s_.key_or_index) && ids.size() == 2) {
        auto it = find_named(tb->indexes, name_of(ids[0]));
        if (it != tb->indexes.end()) it->name = name_of(ids[1]);
      }
    } else if (first == s_.CONVERT_SYM) {
      set_option(tb->options, "charset", lower(text(find(item, s_.charset_name_or_default))));
    } else if (first == s_.create_table_options_space_separated) {
      table_options(ch[0], tb->options);
    }
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
      return iequals(x.name, name) && x.kind == (proc ? "procedure" : "function");
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
  bool changed = false;
  for (const uint32_t tt : find_all(n, s_.table_to_table)) {
    std::vector<uint32_t> idents;
    for (const uint32_t c : t_.children(tt)) {
      if (t_.node(c).symbol == s_.table_ident) idents.push_back(c);
    }
    if (idents.size() != 2) continue;
    auto [from_schema, from_id] = qualified(idents[0]);
    auto [to_schema, to_id] = qualified(idents[1]);
    Schema *from = c_.find_schema(from_schema);
    if (from == nullptr) continue;
    auto it = find_named(from->tables, name_of(from_id));
    if (it == from->tables.end()) continue;
    Table moved = std::move(*it);
    from->tables.erase(it);
    moved.name = name_of(to_id);
    c_.schema(to_schema).tables.push_back(std::move(moved));
    changed = true;
  }
  return changed;
}

bool Catalog_fold::rest(uint32_t n) {
  const auto ch = t_.children(n);
  if (ch.empty()) return false;
  const uint32_t st = ch[0];
  const int sym = t_.node(st).symbol;
  const std::string service_path = text(find(st, s_.mrs_service_request_path));
  auto target_service = [&]() -> Rest_service & {
    return c_.service(service_path.empty() ? c_.m_current_service : service_path);
  };
  auto object = [&](const char *kind, int path_sym) {
    Rest_service &svc = target_service();
    const std::string schema_path = text(find(st, s_.mrs_schema_request_path));
    auto it = std::find_if(svc.schemas.begin(), svc.schemas.end(),
                           [&](const Rest_schema &s) { return iequals(s.path, schema_path); });
    if (it == svc.schemas.end()) {
      svc.schemas.push_back(Rest_schema{schema_path, "", {}, {stmt_, stmt_}});
      it = svc.schemas.end() - 1;
    }
    const uint32_t p = find(st, path_sym);
    Rest_object o{kind, text(p), text(find(st, s_.mrs_qualified_identifier)), origin(p)};
    auto ex = std::find_if(it->objects.begin(), it->objects.end(), [&](const Rest_object &x) {
      return iequals(x.path, o.path) && x.kind == o.kind;
    });
    if (ex != it->objects.end()) *ex = std::move(o); else it->objects.push_back(std::move(o));
  };
  auto named = [&](std::vector<Named_object> &v, int name_sym) {
    const uint32_t p = find(st, name_sym);
    const std::string name = unquote_string(unquote_identifier(text(p)));
    upsert_named(v, Named_object{name, origin(p)});
  };
  auto unnamed = [&](std::vector<Named_object> &v, int name_sym) {
    const std::string name = unquote_string(unquote_identifier(text(find(st, name_sym))));
    std::erase_if(v, [&](const Named_object &x) { return iequals(x.name, name); });
  };

  if (sym == s_.mrs_create_rest_service_statement) {
    Rest_service &svc = c_.service(service_path);
    svc.origin = origin(find(st, s_.mrs_service_request_path));
    return true;
  }
  if (sym == s_.mrs_use_statement) {
    if (!service_path.empty()) c_.m_current_service = service_path;
    return false;
  }
  if (sym == s_.mrs_create_rest_schema_statement) {
    Rest_service &svc = target_service();
    const uint32_t p = find(st, s_.mrs_schema_request_path);
    Rest_schema rs;
    rs.path = text(p);
    rs.schema = name_of(child(st, s_.mrs_identifier));
    rs.origin = origin(p);
    auto it = std::find_if(svc.schemas.begin(), svc.schemas.end(),
                           [&](const Rest_schema &s) { return iequals(s.path, rs.path); });
    if (it != svc.schemas.end()) {
      rs.objects = std::move(it->objects);
      *it = std::move(rs);
    } else {
      svc.schemas.push_back(std::move(rs));
    }
    return true;
  }
  if (sym == s_.mrs_create_rest_view_statement) { object("view", s_.mrs_view_request_path); return true; }
  if (sym == s_.mrs_create_rest_procedure_statement) { object("procedure", s_.mrs_procedure_request_path); return true; }
  if (sym == s_.mrs_create_rest_function_statement) { object("function", s_.mrs_function_request_path); return true; }
  if (sym == s_.mrs_create_rest_content_set_statement) {
    named(target_service().content_sets, s_.mrs_content_set_request_path);
    return true;
  }
  if (sym == s_.mrs_create_rest_auth_app_statement) { named(c_.m_auth_apps, s_.mrs_auth_app_name); return true; }
  if (sym == s_.mrs_create_rest_role_statement) { named(c_.m_roles, s_.mrs_role_name); return true; }
  if (sym == s_.mrs_create_rest_user_statement) {
    const std::string user = unquote_string(unquote_identifier(text(find(st, s_.mrs_user_name))));
    const std::string app = unquote_string(unquote_identifier(text(find(st, s_.mrs_auth_app_name))));
    const std::string name = user + "@" + app;
    auto it = find_named(c_.m_users, name);
    const Origin o = origin(find(st, s_.mrs_user_name));
    if (it != c_.m_users.end()) *it = {name, o}; else c_.m_users.push_back({name, o});
    return true;
  }
  if (sym == s_.mrs_drop_rest_service_statement) {
    std::erase_if(c_.m_services, [&](const Rest_service &s) { return iequals(s.path, service_path); });
    return true;
  }
  if (sym == s_.mrs_drop_rest_schema_statement) {
    const std::string path = text(find(st, s_.mrs_schema_request_path));
    std::erase_if(target_service().schemas, [&](const Rest_schema &s) { return iequals(s.path, path); });
    return true;
  }
  // A DROP names its service with FROM SERVICE, else it is the current one;
  // an object's schema with FROM [SERVICE ...] DATABASE, else every schema of
  // the service is searched.  A service the script never created is left
  // alone rather than made.
  auto dropped_from = [&]() -> Rest_service * {
    return c_.find_service(service_path.empty() ? c_.m_current_service : service_path);
  };
  auto drop_object = [&](const char *kind, int path_sym) {
    Rest_service *svc = dropped_from();
    if (svc == nullptr) return;
    const std::string path = text(find(st, path_sym));
    const std::string schema_path = text(find(st, s_.mrs_schema_request_path));
    for (auto &sch : svc->schemas) {
      if (!schema_path.empty() && !iequals(sch.path, schema_path)) continue;
      std::erase_if(sch.objects, [&](const Rest_object &o) {
        return o.kind == kind && iequals(o.path, path);
      });
    }
  };
  if (sym == s_.mrs_drop_rest_view_statement) { drop_object("view", s_.mrs_view_request_path); return true; }
  if (sym == s_.mrs_drop_rest_procedure_statement) { drop_object("procedure", s_.mrs_procedure_request_path); return true; }
  if (sym == s_.mrs_drop_rest_function_statement) { drop_object("function", s_.mrs_function_request_path); return true; }
  if (sym == s_.mrs_drop_rest_content_set_statement) {
    if (Rest_service *svc = dropped_from()) {
      const std::string path = text(find(st, s_.mrs_content_set_request_path));
      std::erase_if(svc->content_sets, [&](const Named_object &o) { return iequals(o.name, path); });
    }
    return true;
  }
  if (sym == s_.mrs_drop_rest_auth_app_statement) { unnamed(c_.m_auth_apps, s_.mrs_auth_app_name); return true; }
  if (sym == s_.mrs_drop_rest_role_statement) { unnamed(c_.m_roles, s_.mrs_role_name); return true; }
  if (sym == s_.mrs_drop_rest_user_statement) {
    const std::string name =
        unquote_string(unquote_identifier(text(find(st, s_.mrs_user_name)))) + "@" +
        unquote_string(unquote_identifier(text(find(st, s_.mrs_auth_app_name))));
    std::erase_if(c_.m_users, [&](const Named_object &o) { return iequals(o.name, name); });
    return true;
  }
  return false;
}

bool Catalog::apply(const Syntax_tree &tree, std::string_view text) {
  return Catalog_fold(*this, tree, text).run();
}

}  // namespace mdbls
