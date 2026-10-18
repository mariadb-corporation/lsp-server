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

#include "mdbls/sql_names.h"

#include <algorithm>

#include "mdbls/keywords.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

constexpr uint32_t npos = Syntax_tree::npos;

// The grammar symbols the analysis looks for, resolved once.
struct Name_syms {
#define S(x) int x = grammar::symbol(#x);
  // wrappers around one name token
  S(ident) S(ident_cli) S(IDENT_sys) S(IDENT_cli) S(ident_cli_func) S(ident_cli_set_usual_case)
  S(label_ident)
  // names
  S(table_ident) S(simple_ident) S(simple_ident_nospvar) S(field_ident) S(key_part_simple)
  S(insert_ident) S(select_alias) S(ident_table_alias) S(table_alias_clause) S(table_alias_ref)
  S(table_ident_opt_wild) S(select_sublist_qualified_asterisk) S(function_call_generic)
  S(sp_name) S(call) S(use) S(create) S(alter) S(drop) S(table_to_table)
  S(references) S(ref_list) S(using_list) S(view_list) S(alter_list_item)
  S(field_spec) S(key_def) S(sp_param_name) S(sp_decl_ident) S(sp_decl_idents)
  S(sp_decl_idents_init_vars) S(with_element_head) S(with_list_element) S(with_column_list)
  S(ident_sys_alloc) S(option_value_no_option_type) S(trigger_tail) S(event_tail)
  S(optionally_qualified_column_ident)
  // scopes and tables
  S(query_specification) S(query_expression_body_ext) S(update) S(insert) S(replace)
  S(table_primary_ident) S(table_primary_derived) S(table_name_with_opt_use_partition)
  S(delete_single_table) S(select_item) S(select_item_list) S(expr) S(sp_param_name_and_mode)
  S(query_specification_start) S(from_clause) S(table_reference_list) S(DUAL_SYM)
  // tokens
  S(TABLE_SYM) S(VIEW_SYM) S(SEQUENCE_SYM) S(INDEX_SYM) S(DATABASE)
#undef S
  int delete_ = grammar::symbol("delete");
  int dot = grammar::symbol("'.'");
  int comma = grammar::symbol("','");
  int semicolon = grammar::symbol("';'");
  int rparen = grammar::symbol("')'");
};

const Name_syms &syms() {
  static const Name_syms s;
  return s;
}

// A nonterminal that only wraps one name token: ident, IDENT_sys, keyword_*.
bool is_wrapper(int symbol) {
  const auto &s = syms();
  if (symbol == s.ident || symbol == s.ident_cli || symbol == s.IDENT_sys ||
      symbol == s.IDENT_cli || symbol == s.ident_cli_func ||
      symbol == s.ident_cli_set_usual_case || symbol == s.label_ident) {
    return true;
  }
  return !grammar::is_terminal(symbol) &&
         std::string_view(grammar::name(symbol)).substr(0, 7) == "keyword";
}

// A node whose FROM list (or target table) names the tables in scope.
bool is_scope(int symbol) {
  const auto &s = syms();
  return symbol == s.query_specification || symbol == s.update || symbol == s.delete_ ||
         symbol == s.insert || symbol == s.replace;
}

using Words = std::initializer_list<const char *>;

// The keywords the token scanners go by.  A FROM list starts after these:
constexpr Words k_from_words = {"FROM", "JOIN", "STRAIGHT_JOIN"};
// and ends at these (as does the search for its start):
constexpr Words k_clause_words = {"WHERE",  "ON",     "USING", "GROUP",  "ORDER",
                                  "HAVING", "LIMIT",  "SET",   "VALUES", "SELECT",
                                  "WINDOW", "UNION",  "EXCEPT", "INTERSECT"};
// A table name also follows these (INTO only after INSERT or REPLACE):
constexpr Words k_table_words = {"UPDATE", "TABLE", "TABLES", "REFERENCES", "DESCRIBE",
                                 "DESC", "TRUNCATE"};
// After a table name, these are not its alias:
constexpr Words k_not_alias = {"JOIN", "LEFT", "RIGHT", "INNER", "CROSS", "NATURAL",
                               "STRAIGHT_JOIN", "USE", "IGNORE", "FORCE", "PARTITION",
                               "FOR", "LOCK"};

// The tokens of a statement, for the scanners: their text and kind.
struct Token_view {
  const std::vector<Syntax_tree::Token> &t;
  std::string_view sql;
  uint32_t base;

  size_t size() const { return t.size(); }
  int symbol(size_t i) const { return t[i].symbol; }
  std::string_view text(size_t i) const { return sql.substr(t[i].begin - base, t[i].end - t[i].begin); }
  bool name(size_t i) const { return i < t.size() && is_name_symbol(t[i].symbol); }
  // Token i is one of `words`, as a keyword (reserved or not), not a name.
  bool word(size_t i, Words words) const {
    static const int ident = grammar::symbol("IDENT");
    static const int quoted = grammar::symbol("IDENT_QUOTED");
    if (i >= t.size() || t[i].symbol == ident || t[i].symbol == quoted) return false;
    const auto x = text(i);
    for (const char *w : words) {
      if (iequals(x, w)) return true;
    }
    return false;
  }
  // INTO at i names a table: INSERT or REPLACE (and their options) is near
  // before it, not SELECT ... INTO a variable.
  bool into_table(size_t i) const {
    if (!word(i, {"INTO"})) return false;
    for (size_t k = i; k-- > 0 && k + 4 > i;) {
      if (word(k, {"INSERT", "REPLACE"})) return true;
    }
    return false;
  }
};

}  // namespace

const char *name_kind_name(Name_kind kind) {
  switch (kind) {
    case Name_kind::none: return "none";
    case Name_kind::schema: return "schema";
    case Name_kind::table: return "table";
    case Name_kind::column: return "column";
    case Name_kind::qualifier: return "qualifier";
    case Name_kind::routine: return "routine";
    case Name_kind::function: return "function";
    case Name_kind::alias: return "alias";
    case Name_kind::variable: return "variable";
    case Name_kind::index: return "index";
    case Name_kind::other: return "other";
  }
  return "none";
}

std::string unquote_name(std::string_view s) { return unquote_identifier(s); }

const Table_ref *table_for_qualifier(const std::vector<Table_ref> &tables,
                                     const std::vector<Table_ref> &ctes, std::string_view q) {
  for (const auto &t : tables) {
    if (iequals(t.visible_name(), q)) return &t;
  }
  for (const auto &t : tables) {
    if (iequals(t.name, q)) return &t;
  }
  for (const auto &c : ctes) {
    if (iequals(c.name, q)) return &c;
  }
  return nullptr;
}

Statement_names::Statement_names(mdbp_parser *parser, std::string_view sql, uint32_t base)
    : m_sql(sql), m_base(base) {
  if (parser == nullptr) return;
  mdbp_result r;
  const mdbp_status st = mdbp_parse_sql(parser, sql.data(), sql.size(), &r);
  if (st == MDBP_INTERNAL || r.tokens == nullptr) return;
  m_parsed = true;
  m_accepted = st == MDBP_OK && r.accepted;
  m_tree = Syntax_tree::build(parser, r, base);
  m_leaves.assign(m_tree.tokens().size(), npos);
  for (uint32_t n = 0; n < m_tree.size(); ++n) {
    if (m_tree.is_token(n)) m_leaves[m_tree.node(n).first_token] = n;
  }

  const auto &s = syms();
  // Common table expressions and variables, statement-wide.
  for (const uint32_t root : m_tree.roots()) {
    for (const uint32_t w : m_tree.find_all(root, s.with_list_element)) {
      Table_ref cte;
      const uint32_t head = m_tree.child(w, s.with_element_head);
      const uint32_t id = head == npos ? npos : m_tree.find(head, s.ident);
      if (id == npos) continue;
      cte.name = name_of(id);
      cte.name_range = {m_tree.begin(id), m_tree.end(id)};
      cte.derived = true;
      if (const uint32_t cols = m_tree.child(w, s.with_column_list); cols != npos) {
        for (const uint32_t c : m_tree.find_all(cols, s.ident_sys_alloc)) {
          cte.columns.push_back(name_of(c));
        }
      }
      if (cte.columns.empty()) derived_columns(w, &cte.columns);
      m_ctes.push_back(std::move(cte));
    }
    for (const uint32_t p : m_tree.find_all(root, s.sp_param_name_and_mode)) {
      const uint32_t n = m_tree.find(p, s.sp_param_name);
      if (n == npos) continue;
      Variable v;
      const uint32_t id = m_tree.find(n, s.ident);
      v.name = name_of(id != npos ? id : n);
      v.range = {m_tree.begin(n), m_tree.end(n)};
      // The type follows the name in the parameter's text.
      const uint32_t b = m_tree.end(n), e = m_tree.end(p);
      v.type = collapse(m_sql.substr(b - m_base, e - b));
      m_variables.push_back(std::move(v));
    }
    for (const uint32_t d : m_tree.find_all(root, s.sp_decl_idents_init_vars)) {
      // DECLARE a, b INT: the type is the text after the names.
      const uint32_t names = m_tree.child(d, s.sp_decl_idents);
      std::string type;
      if (const uint32_t parent = m_tree.node(d).parent; parent != npos && names != npos) {
        const uint32_t b = m_tree.end(names), e = m_tree.end(parent);
        type = collapse(m_sql.substr(b - m_base, e - b));
      }
      for (const uint32_t id : m_tree.find_all(d, s.sp_decl_ident)) {
        m_variables.push_back({name_of(id), type, {m_tree.begin(id), m_tree.end(id)}});
      }
    }
  }
}

std::string_view Statement_names::text(uint32_t n) const {
  const uint32_t b = m_tree.begin(n), e = m_tree.end(n);
  if (b < m_base || e < b || e - m_base > m_sql.size()) return {};
  return m_sql.substr(b - m_base, e - b);
}

std::string Statement_names::name_of(uint32_t n) const {
  return n == npos ? std::string() : unquote_name(text(n));
}

uint32_t Statement_names::ancestor(uint32_t n, int symbol) const {
  for (; n != npos; n = m_tree.node(n).parent) {
    if (m_tree.node(n).symbol == symbol) return n;
  }
  return npos;
}

uint32_t Statement_names::token_at(uint32_t offset) const {
  const auto &tokens = m_tree.tokens();
  // Tokens are in text order: the first that ends after the offset.
  const auto it = std::partition_point(tokens.begin(), tokens.end(),
                                       [&](const Syntax_tree::Token &t) { return t.end <= offset; });
  const auto at = static_cast<uint32_t>(it - tokens.begin());
  const uint32_t covering =
      it != tokens.end() && it->begin <= offset && it->begin < it->end ? at : npos;
  const uint32_t ending = at > 0 && tokens[at - 1].end == offset ? at - 1 : npos;
  // A cursor between a name and punctuation (a|.b, f|() belongs to the name.
  auto name = [&](uint32_t i) { return is_name_symbol(tokens[i].symbol); };
  if (covering != npos && (name(covering) || ending == npos || !name(ending))) return covering;
  return ending != npos ? ending : covering;
}

Name_ref Statement_names::name_at(uint32_t offset) const {
  const uint32_t tok = token_at(offset);
  if (tok == npos) return {};
  return classify(tok);
}

Table_ref Statement_names::table_ident_ref(uint32_t ti) const {
  const auto &s = syms();
  Table_ref ref;
  std::vector<uint32_t> ids;
  for (const uint32_t c : m_tree.children(ti)) {
    if (m_tree.node(c).symbol == s.ident) ids.push_back(c);
  }
  if (ids.empty()) return ref;
  if (ids.size() >= 2) ref.schema = name_of(ids[0]);
  const uint32_t n = ids[std::min<size_t>(ids.size(), 2) - 1];
  ref.name = name_of(n);
  ref.name_range = {m_tree.begin(n), m_tree.end(n)};
  return ref;
}

void Statement_names::set_owner(uint32_t table_ident, Name_ref *ref) const {
  if (table_ident == npos) return;
  const Table_ref t = table_ident_ref(table_ident);
  if (t.name.empty()) return;
  ref->has_owner = true;
  ref->owner_schema = t.schema;
  ref->owner = t.name;
}

void Statement_names::apply_cte(Table_ref *ref) const {
  if (!ref->schema.empty()) return;
  for (const auto &c : m_ctes) {
    if (iequals(c.name, ref->name)) {
      ref->derived = true;
      ref->columns = c.columns;
    }
  }
}

Name_ref Statement_names::classify(uint32_t token) const {
  const auto &s = syms();
  const auto &tokens = m_tree.tokens();
  Name_ref ref;
  if (token >= tokens.size()) return ref;
  const auto &tk = tokens[token];
  ref.range = {tk.begin, tk.end};
  ref.name = unquote_name(m_sql.substr(tk.begin - m_base, tk.end - tk.begin));

  const uint32_t leaf_node = leaf(token);
  ref.node = leaf_node;
  // A name token is an identifier or a keyword standing for one; anything
  // else (a reserved keyword, a literal, punctuation) names nothing, unless
  // the tree says otherwise below.
  const bool name_like = is_name_symbol(tk.symbol);
  if (leaf_node == npos) return name_like ? classify_by_tokens(token) : ref;

  // Up through the wrappers of the one token.
  uint32_t child = leaf_node;
  uint32_t a = m_tree.node(leaf_node).parent;
  while (a != npos && is_wrapper(m_tree.node(a).symbol)) {
    child = a;
    a = m_tree.node(a).parent;
  }
  // The stack of a rejected statement ends here: ask the tokens.
  if (a == npos) return name_like ? classify_by_tokens(token) : ref;
  const int sym = m_tree.node(a).symbol;

  // The parts of a dotted name a.b.c: the children that are names, and the
  // position of ours among them.
  std::vector<uint32_t> ids;
  size_t at = 0;
  for (const uint32_t c : m_tree.children(a)) {
    const int cs = m_tree.node(c).symbol;
    const bool part = grammar::is_terminal(cs) ? is_name_symbol(cs) : is_wrapper(cs);
    if (!part || m_tree.node(c).token_count == 0) continue;
    if (c == child) at = ids.size();
    ids.push_back(c);
  }
  auto qualify = [&] {
    for (size_t k = 0; k < at; ++k) {
      ref.qualifiers.push_back(name_of(ids[k]));
      ref.qualifier_ranges.push_back({m_tree.begin(ids[k]), m_tree.end(ids[k])});
    }
  };
  // A dotted column name: column, or the table/schema parts before it.
  auto dotted_column = [&] {
    const size_t last = ids.empty() ? 0 : ids.size() - 1;
    if (at == last) {
      ref.kind = Name_kind::column;
      qualify();
    } else if (at + 1 == last) {
      ref.kind = last == 1 ? Name_kind::qualifier : Name_kind::table;
      qualify();
    } else {
      ref.kind = Name_kind::schema;
    }
  };
  const bool schema_part = ids.size() >= 2 && at == 0;
  // The table a DDL statement (ALTER TABLE t, CREATE ... t) is about.
  auto ddl_table = [&] {
    uint32_t owner = ancestor(a, s.alter);
    if (owner == npos) owner = ancestor(a, s.create);
    return owner == npos ? npos : m_tree.child(owner, s.table_ident);
  };
  // In a trigger body: the trigger's table.
  auto trigger_table = [&] {
    const uint32_t tt = ancestor(a, s.trigger_tail);
    return tt == npos ? npos : m_tree.child(tt, s.table_ident);
  };

  if (sym == s.table_ident) {
    if (schema_part) {
      ref.kind = Name_kind::schema;
      return ref;
    }
    ref.kind = Name_kind::table;
    qualify();
    const uint32_t p = m_tree.node(a).parent;
    const int ps = p == npos ? -1 : m_tree.node(p).symbol;
    if (ps == s.create) {
      // CREATE TABLE/VIEW/SEQUENCE t defines t; CREATE INDEX ... ON t,
      // CREATE TRIGGER ... ON t do not.
      const bool defines = (m_tree.child(p, s.TABLE_SYM) != npos ||
                            m_tree.child(p, s.VIEW_SYM) != npos ||
                            m_tree.child(p, s.SEQUENCE_SYM) != npos) &&
                           m_tree.child(p, s.INDEX_SYM) == npos;
      ref.defining = defines && m_tree.child(p, s.table_ident) == a;
    } else if (ps == s.table_to_table) {
      ref.defining = m_tree.child(p, s.table_ident) != a;  // RENAME a TO b: b
    }
    return ref;
  }
  if (sym == s.simple_ident || sym == s.simple_ident_nospvar ||
      sym == s.optionally_qualified_column_ident) {
    dotted_column();
    const uint32_t p = m_tree.node(a).parent;
    if (ref.kind == Name_kind::column && p != npos && m_tree.node(p).symbol == s.insert_ident) {
      if (const uint32_t ins = ancestor(p, s.insert); ins != npos) {
        set_owner(m_tree.find(ins, s.table_ident), &ref);
      }
    }
    // NEW.a and OLD.a in a trigger body are columns of the trigger's table.
    const bool new_old = ref.qualifiers.size() == 1 &&
                         (iequals(ref.qualifiers[0], "NEW") || iequals(ref.qualifiers[0], "OLD"));
    if (ref.kind == Name_kind::column && new_old) set_owner(trigger_table(), &ref);
    if (ref.kind == Name_kind::qualifier && ancestor(a, s.trigger_tail) != npos &&
        (iequals(ref.name, "NEW") || iequals(ref.name, "OLD"))) {
      ref.kind = Name_kind::other;
    }
    return ref;
  }
  if (sym == s.field_ident) {
    if (ids.size() > 1) {
      dotted_column();
      return ref;
    }
    const uint32_t p = m_tree.node(a).parent;
    const int ps = p == npos ? -1 : m_tree.node(p).symbol;
    ref.kind = ps == s.key_def ? Name_kind::index : Name_kind::column;
    ref.defining = ps == s.key_def || ps == s.field_spec;
    set_owner(ddl_table(), &ref);
    return ref;
  }
  if (sym == s.key_part_simple || sym == s.ref_list || sym == s.alter_list_item) {
    ref.kind = Name_kind::column;
    const uint32_t r = ancestor(a, s.references);
    set_owner(r != npos ? m_tree.child(r, s.table_ident) : ddl_table(), &ref);
    return ref;
  }
  if (sym == s.using_list || sym == s.view_list) {
    ref.kind = Name_kind::column;
    ref.defining = sym == s.view_list;
    return ref;
  }
  if (sym == s.ident_sys_alloc && ancestor(a, s.with_column_list) != npos) {
    ref.kind = Name_kind::column;
    ref.defining = true;
    if (const uint32_t w = ancestor(a, s.with_list_element); w != npos) {
      const uint32_t head = m_tree.child(w, s.with_element_head);
      ref.has_owner = head != npos;
      ref.owner = name_of(head == npos ? npos : m_tree.find(head, s.ident));
    }
    return ref;
  }
  if (sym == s.select_alias || sym == s.ident_table_alias || sym == s.table_alias_clause) {
    ref.kind = Name_kind::alias;
    ref.defining = true;
    return ref;
  }
  if (sym == s.table_ident_opt_wild || sym == s.table_alias_ref) {
    ref.kind = schema_part ? Name_kind::schema : Name_kind::qualifier;
    if (!schema_part) qualify();
    return ref;
  }
  if (sym == s.select_sublist_qualified_asterisk) {
    ref.kind = schema_part         ? Name_kind::schema
               : ids.size() >= 2   ? Name_kind::table
                                   : Name_kind::qualifier;
    qualify();
    return ref;
  }
  if (sym == s.function_call_generic) {
    // ident '(' ...: the parts before the '(' are the function's name.
    size_t before_paren = 0;
    for (const uint32_t c : m_tree.children(a)) {
      if (is_left_paren(m_tree.node(c).symbol)) break;
      if (std::find(ids.begin(), ids.end(), c) != ids.end()) ++before_paren;
    }
    if (at + 1 == before_paren) {
      ref.kind = Name_kind::function;
      qualify();
    } else if (at + 1 < before_paren) {
      ref.kind = Name_kind::schema;
    }
    return ref;
  }
  if (sym == s.sp_name || sym == s.call) {
    if (schema_part) {
      ref.kind = Name_kind::schema;
      return ref;
    }
    qualify();
    if (sym == s.call) {
      ref.kind = Name_kind::routine;
      return ref;
    }
    // A trigger's or event's name is not a routine's.
    const bool other = ancestor(a, s.trigger_tail) != npos || ancestor(a, s.event_tail) != npos;
    ref.kind = other ? Name_kind::other : Name_kind::routine;
    ref.defining = ancestor(a, s.create) != npos;
    return ref;
  }
  if (sym == s.use) {
    ref.kind = Name_kind::schema;
    return ref;
  }
  if (sym == s.create || sym == s.drop || sym == s.alter) {
    if (m_tree.child(a, s.DATABASE) != npos) {
      ref.kind = Name_kind::schema;
    } else if (sym == s.create && m_tree.child(a, s.INDEX_SYM) != npos) {
      ref.kind = Name_kind::index;
    } else {
      ref.kind = Name_kind::other;
    }
    ref.defining = sym == s.create;
    return ref;
  }
  if (sym == s.sp_param_name || sym == s.sp_decl_ident) {
    ref.kind = Name_kind::variable;
    ref.defining = true;
    return ref;
  }
  if (sym == s.with_element_head) {
    ref.kind = Name_kind::table;
    ref.defining = true;
    return ref;
  }
  if (sym == s.option_value_no_option_type) {
    // SET v = ..., SET NEW.a = ...
    if (ids.size() == 1) {
      ref.kind = Name_kind::variable;
    } else if (ids.size() == 2 && at == 1 && ancestor(a, s.trigger_tail) != npos) {
      ref.kind = Name_kind::column;
      qualify();
      set_owner(trigger_table(), &ref);
    } else if (ids.size() == 2 && at == 0) {
      ref.kind = Name_kind::other;
    }
    return ref;
  }
  // A name in a place the rules above do not know (an index hint, a label,
  // a REST path): something, but nothing we resolve.
  if (name_like) ref.kind = Name_kind::other;
  return ref;
}

Name_ref Statement_names::classify_by_tokens(uint32_t token) const {
  const auto &s = syms();
  const Token_view tv{m_tree.tokens(), m_sql, m_base};
  Name_ref ref;
  ref.range = {m_tree.tokens()[token].begin, m_tree.tokens()[token].end};
  ref.name = unquote_name(tv.text(token));

  // The dotted name this token ends: qualifiers before it.
  uint32_t first = token;
  while (first >= 2 && tv.symbol(first - 1) == s.dot && tv.name(first - 2)) first -= 2;
  for (uint32_t i = first; i < token; i += 2) {
    ref.qualifiers.push_back(unquote_name(tv.text(i)));
    ref.qualifier_ranges.push_back({m_tree.tokens()[i].begin, m_tree.tokens()[i].end});
  }
  // A dotted name continues after the token: then it is a qualifier.
  const bool continues = token + 2 < tv.size() && tv.symbol(token + 1) == s.dot && tv.name(token + 2);

  // The context: the keyword before the name, skipping a FROM list.
  enum class Ctx { expression, table, schema, routine, insert_columns, statement } ctx =
      Ctx::expression;
  uint32_t insert_table = npos;
  if (first > 0) {
    const uint32_t p = first - 1;
    if (tv.symbol(p) == s.semicolon || tv.word(p, {"BEGIN", "THEN", "ELSE", "DO", "LOOP", "REPEAT"})) {
      // A statement starts here (inside a compound statement): a keyword,
      // or a label being defined.
      ctx = Ctx::statement;
    } else if (tv.word(p, k_from_words) || tv.word(p, k_table_words) || tv.into_table(p) ||
               (tv.word(p, {"LIKE"}) && p >= 2 && tv.word(p - 2, {"TABLE"}))) {
      ctx = Ctx::table;  // LIKE only after CREATE TABLE x
    } else if (tv.word(p, {"USE", "DATABASE", "SCHEMA"})) {
      ctx = Ctx::schema;
    } else if (tv.word(p, {"CALL"})) {
      ctx = Ctx::routine;
    } else if (tv.symbol(p) == s.comma || is_left_paren(tv.symbol(p))) {
      // In a list: FROM a, b | INSERT INTO t (a, b
      int depth = 0;
      for (uint32_t i = p + 1; i-- > 0;) {
        const int sy = tv.symbol(i);
        if (sy == s.rparen) {
          ++depth;
        } else if (is_left_paren(sy)) {
          if (depth > 0) {
            --depth;
            continue;
          }
          // INTO [schema.]name (
          const bool qualified = i >= 4 && tv.symbol(i - 2) == s.dot && tv.name(i - 3);
          const uint32_t into = qualified ? i - 4 : i - 2;
          if (i >= 2 && tv.name(i - 1) && tv.word(into, {"INTO"})) {
            ctx = Ctx::insert_columns;
            insert_table = i - 1;
          }
          break;  // else a function's arguments, a subquery: an expression
        } else if (depth == 0 && tv.word(i, {"FROM"})) {
          ctx = Ctx::table;
          break;
        } else if (depth == 0 && (tv.word(i, k_clause_words) || tv.word(i, {"BY", "JOIN"}))) {
          break;
        }
      }
    }
  }

  switch (ctx) {
    case Ctx::statement:
      ref.kind = Name_kind::other;
      ref.defining = true;
      break;
    case Ctx::table:
      ref.kind = continues                   ? Name_kind::schema
                 : ref.qualifiers.size() <= 1 ? Name_kind::table
                                              : Name_kind::other;
      break;
    case Ctx::schema:
      ref.kind = Name_kind::schema;
      break;
    case Ctx::routine:
      ref.kind = continues ? Name_kind::schema : Name_kind::routine;
      break;
    case Ctx::insert_columns:
      ref.kind = Name_kind::column;
      ref.has_owner = true;
      ref.owner = unquote_name(tv.text(insert_table));
      if (insert_table >= 2 && tv.symbol(insert_table - 1) == s.dot) {
        ref.owner_schema = unquote_name(tv.text(insert_table - 2));
      }
      break;
    case Ctx::expression:
      if (token + 1 < tv.size() && is_left_paren(tv.symbol(token + 1)) &&
          ref.qualifiers.size() <= 1) {
        ref.kind = Name_kind::function;
      } else if (continues) {
        ref.kind = ref.qualifiers.empty() ? Name_kind::qualifier : Name_kind::table;
      } else {
        ref.kind = ref.qualifiers.size() <= 2 ? Name_kind::column : Name_kind::other;
      }
      break;
  }
  return ref;
}

std::vector<Name_ref> Statement_names::names(std::string_view spelled) const {
  std::vector<Name_ref> out;
  const Token_view tv{m_tree.tokens(), m_sql, m_base};
  for (uint32_t i = 0; i < tv.size(); ++i) {
    if (!tv.name(i)) continue;
    if (!spelled.empty() && !iequals(unquote_name(tv.text(i)), spelled)) continue;
    Name_ref r = classify(i);
    if (r.kind != Name_kind::none) out.push_back(std::move(r));
  }
  return out;
}

std::string Statement_names::alias_of(uint32_t item) const {
  const auto &s = syms();
  const uint32_t al = m_tree.child(item, s.select_alias);
  if (al == npos || m_tree.node(al).token_count == 0) return {};
  const uint32_t id = m_tree.find(al, s.ident);
  return id != npos ? name_of(id) : unquote_name(text(al));  // AS 'string' too
}

void Statement_names::derived_columns(uint32_t node, std::vector<std::string> *out) const {
  const auto &s = syms();
  const uint32_t q = m_tree.find(node, s.query_specification);
  const uint32_t list = q == npos ? npos : m_tree.find(q, s.select_item_list);
  if (list == npos) return;
  for (const uint32_t item : m_tree.find_all(list, s.select_item)) {
    if (auto alias = alias_of(item); !alias.empty()) {
      out->push_back(std::move(alias));
      continue;
    }
    const uint32_t e = m_tree.child(item, s.expr);
    const uint32_t si = e == npos ? npos : m_tree.find(e, s.simple_ident);
    if (si != npos && m_tree.node(si).token_count == m_tree.node(e).token_count) {
      const auto ch = m_tree.children(si);
      if (!ch.empty()) out->push_back(name_of(ch.back()));
    }
  }
}

void Statement_names::collect_tables(uint32_t scope, std::vector<Table_ref> *out) const {
  const auto &s = syms();
  // Depth first, in text order, not into nested scopes.
  std::vector<uint32_t> stack{scope};
  while (!stack.empty()) {
    const uint32_t n = stack.back();
    stack.pop_back();
    const int sym = m_tree.node(n).symbol;
    if (n != scope && is_scope(sym)) continue;
    const bool named = sym == s.table_primary_ident || sym == s.table_name_with_opt_use_partition ||
                       sym == s.delete_single_table;
    if (named || sym == s.table_primary_derived) {
      Table_ref r;
      if (named) {
        const uint32_t ti = m_tree.child(n, s.table_ident);
        if (ti == npos) continue;
        r = table_ident_ref(ti);
        if (r.name.empty()) continue;
        apply_cte(&r);
      } else {
        r.derived = true;
        derived_columns(n, &r.columns);
      }
      if (const uint32_t al = m_tree.find(n, s.ident_table_alias); al != npos) {
        r.alias = name_of(al);
        r.alias_range = {m_tree.begin(al), m_tree.end(al)};
      }
      out->push_back(std::move(r));
      continue;
    }
    const auto ch = m_tree.children(n);
    for (size_t k = ch.size(); k-- > 0;) stack.push_back(ch[k]);
  }
}

uint32_t Statement_names::node_at(uint32_t offset) const {
  const uint32_t tok = token_at(offset);
  uint32_t node = tok == npos ? npos : leaf(tok);
  if (node == npos) {
    // An offset between tokens: the nearest shifted token before it.
    for (uint32_t i = 0; i < m_tree.tokens().size() && m_tree.tokens()[i].begin <= offset; ++i) {
      if (leaf(i) != npos) node = leaf(i);
    }
  }
  return node;
}

Query_shape Statement_names::query_at(uint32_t offset) const {
  const auto &s = syms();
  Query_shape out;
  const uint32_t q = ancestor(node_at(offset), s.query_specification);
  if (q == npos) return out;
  const uint32_t list = m_tree.find(q, s.select_item_list);
  if (list == npos) return out;
  out.found = true;
  out.select_list = {m_tree.begin(list), m_tree.end(list)};
  // The query's own FROM, not one of a subquery in its select list.
  for (const uint32_t c : m_tree.children(q)) {
    const uint32_t from = m_tree.node(c).symbol == s.from_clause ? c : m_tree.find(c, s.from_clause);
    if (from == npos || ancestor(from, s.query_specification) != q) continue;
    const uint32_t tables = m_tree.find(from, s.table_reference_list);
    if (tables == npos) continue;
    out.has_from = true;
    out.from_tables = {m_tree.begin(tables), m_tree.end(tables)};
    out.dual = m_tree.find(tables, s.DUAL_SYM) != npos;
    break;
  }
  return out;
}

std::vector<Table_ref> Statement_names::tables_at(uint32_t offset) const {
  const auto &s = syms();
  std::vector<Table_ref> out;
  const uint32_t node = node_at(offset);
  bool found_scope = false;
  for (uint32_t n = node; n != npos; n = m_tree.node(n).parent) {
    const int sym = m_tree.node(n).symbol;
    if (is_scope(sym)) {
      collect_tables(n, &out);
      found_scope = true;
    } else if (sym == s.query_expression_body_ext && !found_scope) {
      // ORDER BY / LIMIT after a query: its first query's tables.
      if (const uint32_t q = m_tree.find(n, s.query_specification); q != npos) {
        collect_tables(q, &out);
        found_scope = true;
      }
    }
  }
  if (!found_scope) out = scanned_tables();
  return out;
}

std::vector<std::string> Statement_names::select_aliases_at(uint32_t offset) const {
  const auto &s = syms();
  std::vector<std::string> out;
  const uint32_t tok = token_at(offset);
  for (uint32_t n = tok == npos ? npos : leaf(tok); n != npos; n = m_tree.node(n).parent) {
    const int sym = m_tree.node(n).symbol;
    const uint32_t q = sym == s.query_specification        ? n
                       : sym == s.query_expression_body_ext ? m_tree.find(n, s.query_specification)
                                                            : npos;
    if (q == npos) continue;
    if (const uint32_t list = m_tree.find(q, s.select_item_list); list != npos) {
      for (const uint32_t item : m_tree.find_all(list, s.select_item)) {
        if (auto alias = alias_of(item); !alias.empty()) out.push_back(std::move(alias));
      }
    }
    break;
  }
  return out;
}

std::vector<Table_ref> Statement_names::scanned_tables() const {
  const auto &s = syms();
  const Token_view tv{m_tree.tokens(), m_sql, m_base};
  std::vector<Table_ref> out;
  bool in_from = false;
  int depth = 0;
  int from_depth = -1;
  for (size_t i = 0; i < tv.size(); ++i) {
    const int sy = tv.symbol(i);
    if (is_left_paren(sy)) ++depth;
    if (sy == s.rparen) {
      --depth;
      if (depth < from_depth) in_from = false;
    }
    const bool from = tv.word(i, k_from_words) || (in_from && depth == from_depth && sy == s.comma);
    if (tv.word(i, k_clause_words)) in_from = false;
    if (!from && !tv.word(i, {"UPDATE"}) && !tv.into_table(i)) continue;
    if (from) {
      in_from = true;
      from_depth = depth;
    }
    size_t j = i + 1;
    Table_ref r;
    if (j < tv.size() && is_left_paren(tv.symbol(j))) {
      // (SELECT ...) alias
      for (int d = 0; j < tv.size(); ++j) {
        if (is_left_paren(tv.symbol(j))) ++d;
        if (tv.symbol(j) == s.rparen && --d == 0) break;
      }
      ++j;
      r.derived = true;
    } else if (tv.name(j)) {
      if (j + 2 < tv.size() && tv.symbol(j + 1) == s.dot && tv.name(j + 2)) {
        r.schema = unquote_name(tv.text(j));
        j += 2;
      }
      r.name = unquote_name(tv.text(j));
      r.name_range = {m_tree.tokens()[j].begin, m_tree.tokens()[j].end};
      apply_cte(&r);
      ++j;
    } else {
      continue;
    }
    if (tv.word(j, {"AS"})) ++j;
    if (tv.name(j) && !tv.word(j, k_clause_words) && !tv.word(j, k_not_alias)) {
      r.alias = unquote_name(tv.text(j));
      r.alias_range = {m_tree.tokens()[j].begin, m_tree.tokens()[j].end};
    }
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace mdbls
