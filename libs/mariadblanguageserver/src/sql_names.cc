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
#include <cstring>

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
  S(sp_name) S(call) S(use) S(create) S(alter) S(drop) S(rename) S(table_to_table)
  S(create_like) S(references) S(ref_list) S(using_list) S(view_list) S(alter_list_item)
  S(field_spec) S(key_def) S(sp_param_name) S(sp_decl_ident) S(sp_decl_idents)
  S(sp_decl_idents_init_vars) S(with_element_head) S(with_list_element) S(with_column_list)
  S(ident_sys_alloc) S(option_value_no_option_type) S(trigger_tail) S(create_routine)
  S(drop_routine) S(event_tail) S(optionally_qualified_column_ident) S(order_ident)
  S(update_elem)
  // scopes and tables
  S(query_specification) S(query_expression_body_ext) S(update) S(insert) S(replace)
  S(table_primary_ident) S(table_primary_derived) S(table_name_with_opt_use_partition)
  S(delete_single_table) S(insert_table) S(select_item) S(select_item_list) S(expr)
  S(sp_param_name_and_mode) S(type_with_opt_collate) S(field_type) S(sp_decl_variable_list)
  // tokens
  S(TABLE_SYM) S(VIEW_SYM) S(SEQUENCE_SYM) S(INDEX_SYM) S(DATABASE) S(TRIGGER_SYM) S(EVENT_SYM)
  S(IDENT) S(IDENT_QUOTED) S(END_OF_INPUT)
#undef S
  int delete_ = grammar::symbol("delete");
  int dot = grammar::symbol("'.'");
  int comma = grammar::symbol("','");
  int lparen = grammar::symbol("'('");
  int rparen = grammar::symbol("')'");
};

const Name_syms &syms() {
  static const Name_syms s;
  return s;
}

bool starts_with(const char *s, const char *prefix) {
  return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

bool is_left_paren(int symbol) {
  return symbol >= 0 && (symbol == grammar::symbol("'('") ||
                         starts_with(grammar::name(symbol), "LEFT_PAREN"));
}

// `s` without blanks at either end, and with runs of blanks as one space.
std::string trimmed(std::string_view s) {
  std::string out;
  bool space = false;
  for (const char c : s) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      space = !out.empty();
    } else {
      if (space) out.push_back(' ');
      space = false;
      out.push_back(c);
    }
  }
  return out;
}

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

std::string unquote_name(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '`' || s.front() == '"') && s.back() == s.front()) {
    const char q = s.front();
    std::string out;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
      out.push_back(s[i]);
      if (s[i] == q && i + 2 < s.size() && s[i + 1] == q) ++i;
    }
    return out;
  }
  return std::string(s);
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

  const auto &s = syms();
  if (m_tree.empty()) return;
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
      v.type = trimmed(m_sql.substr(b - m_base, e - b));
      m_variables.push_back(std::move(v));
    }
    for (const uint32_t d : m_tree.find_all(root, s.sp_decl_idents_init_vars)) {
      // DECLARE a, b INT: the type is the text after the names.
      const uint32_t names = m_tree.child(d, s.sp_decl_idents);
      std::string type;
      if (const uint32_t parent = m_tree.node(d).parent; parent != npos && names != npos) {
        const uint32_t b = m_tree.end(names), e = m_tree.end(parent);
        type = trimmed(m_sql.substr(b - m_base, e - b));
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

bool Statement_names::is_wrapper(int symbol) const {
  const auto &s = syms();
  if (symbol == s.ident || symbol == s.ident_cli || symbol == s.IDENT_sys ||
      symbol == s.IDENT_cli || symbol == s.ident_cli_func ||
      symbol == s.ident_cli_set_usual_case || symbol == s.label_ident) {
    return true;
  }
  return !grammar::is_terminal(symbol) && starts_with(grammar::name(symbol), "keyword");
}

bool Statement_names::is_scope(int symbol) const {
  const auto &s = syms();
  return symbol == s.query_specification || symbol == s.update || symbol == s.delete_ ||
         symbol == s.insert || symbol == s.replace;
}

uint32_t Statement_names::ancestor(uint32_t n, int symbol) const {
  for (; n != npos; n = m_tree.node(n).parent) {
    if (m_tree.node(n).symbol == symbol) return n;
  }
  return npos;
}

uint32_t Statement_names::token_at(uint32_t offset) const {
  const auto &s = syms();
  const auto &tokens = m_tree.tokens();
  uint32_t covering = npos, ending = npos;
  for (uint32_t i = 0; i < tokens.size(); ++i) {
    const auto &t = tokens[i];
    if (t.end <= t.begin) continue;  // END_OF_INPUT
    if (t.begin <= offset && offset < t.end) {
      covering = i;
      break;
    }
    if (t.end == offset) ending = i;
    if (t.begin > offset) break;
  }
  // A cursor between a name and punctuation (a|.b, f|() belongs to the name.
  auto is_name = [&](uint32_t i) {
    const int sy = tokens[i].symbol;
    return sy == s.IDENT || sy == s.IDENT_QUOTED || is_nonreserved_keyword(sy);
  };
  if (covering != npos && (is_name(covering) || ending == npos || !is_name(ending))) {
    return covering;
  }
  return ending != npos ? ending : covering;
}

namespace {

// The leaf node of token `token`, npos when the parser never shifted it.
uint32_t leaf_of(const Syntax_tree &t, uint32_t token) {
  // Leaves are created in token order; a binary search over nodes is not
  // possible (inner nodes interleave), so scan the leaves.
  for (uint32_t n = 0; n < t.size(); ++n) {
    if (t.is_token(n) && t.node(n).first_token == token) return n;
  }
  return npos;
}

}  // namespace

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
  if (ids.size() >= 2) {
    ref.schema = name_of(ids[0]);
    ref.name = name_of(ids[1]);
    ref.name_range = {m_tree.begin(ids[1]), m_tree.end(ids[1])};
  } else if (ids.size() == 1) {
    ref.name = name_of(ids[0]);
    ref.name_range = {m_tree.begin(ids[0]), m_tree.end(ids[0])};
  }
  return ref;
}

void Statement_names::set_owner_from(uint32_t node, int table_child_symbol, Name_ref *ref) const {
  const uint32_t ti = node == npos ? npos : m_tree.find(node, table_child_symbol);
  if (ti == npos) return;
  // find() goes depth first: make sure the table_ident is the node's own
  // (a direct child, or under the insert table wrapper).
  const Table_ref t = table_ident_ref(ti);
  if (t.name.empty()) return;
  ref->has_owner = true;
  ref->owner_schema = t.schema;
  ref->owner = t.name;
}

Name_ref Statement_names::classify(uint32_t token) const {
  const auto &s = syms();
  const auto &tokens = m_tree.tokens();
  Name_ref ref;
  if (token >= tokens.size()) return ref;
  const auto &tk = tokens[token];
  ref.range = {tk.begin, tk.end};
  ref.name = unquote_name(m_sql.substr(tk.begin - m_base, tk.end - tk.begin));

  const uint32_t leaf = leaf_of(m_tree, token);
  ref.node = leaf;
  // A name token is an identifier or a keyword standing for one; anything
  // else (a reserved keyword, a literal, punctuation) names nothing, unless
  // the tree says otherwise below.
  const bool name_like = tk.symbol == s.IDENT || tk.symbol == s.IDENT_QUOTED ||
                         is_nonreserved_keyword(tk.symbol);
  if (leaf == npos) {
    return name_like ? classify_by_tokens(token) : ref;
  }

  // Up through the wrappers of the one token.
  uint32_t child = leaf;
  uint32_t a = m_tree.node(leaf).parent;
  while (a != npos && is_wrapper(m_tree.node(a).symbol)) {
    child = a;
    a = m_tree.node(a).parent;
  }
  if (a == npos) {
    // The stack of a rejected statement ends here: ask the tokens.
    return name_like ? classify_by_tokens(token) : ref;
  }
  const int sym = m_tree.node(a).symbol;
  const auto kids = m_tree.children(a);

  // The parts of a dotted name a.b.c: the children that are names, and the
  // position of ours among them.
  auto parts = [&](uint32_t node, std::vector<uint32_t> *ids) -> size_t {
    size_t at = 0;
    for (const uint32_t c : m_tree.children(node)) {
      const int cs = m_tree.node(c).symbol;
      const bool part = grammar::is_terminal(cs)
                            ? cs == s.IDENT || cs == s.IDENT_QUOTED || is_nonreserved_keyword(cs)
                            : is_wrapper(cs);
      if (!part || m_tree.node(c).token_count == 0) continue;
      if (c == child) at = ids->size();
      ids->push_back(c);
    }
    return at;
  };
  auto qualify = [&](const std::vector<uint32_t> &ids, size_t upto) {
    for (size_t k = 0; k < upto; ++k) {
      ref.qualifiers.push_back(name_of(ids[k]));
      ref.qualifier_ranges.push_back({m_tree.begin(ids[k]), m_tree.end(ids[k])});
    }
  };
  // A dotted column name: column, or the table/schema parts before it.
  auto dotted_column = [&](uint32_t node) {
    std::vector<uint32_t> ids;
    const size_t at = parts(node, &ids);
    const size_t last = ids.empty() ? 0 : ids.size() - 1;
    if (at == last) {
      ref.kind = Name_kind::column;
      qualify(ids, at);
    } else if (at + 1 == last) {
      ref.kind = last == 1 ? Name_kind::qualifier : Name_kind::table;
      qualify(ids, at);
    } else {
      ref.kind = Name_kind::schema;
    }
  };

  if (sym == s.table_ident) {
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    if (ids.size() >= 2 && at == 0) {
      ref.kind = Name_kind::schema;
      return ref;
    }
    ref.kind = Name_kind::table;
    qualify(ids, at);
    const uint32_t p = m_tree.node(a).parent;
    if (p != npos) {
      const int ps = m_tree.node(p).symbol;
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
    }
    return ref;
  }
  if (sym == s.simple_ident || sym == s.simple_ident_nospvar ||
      sym == s.optionally_qualified_column_ident) {
    dotted_column(a);
    const uint32_t p = m_tree.node(a).parent;
    if (ref.kind == Name_kind::column && p != npos && m_tree.node(p).symbol == s.insert_ident) {
      set_owner_from(ancestor(p, s.insert), s.table_ident, &ref);
    }
    // NEW.a and OLD.a in a trigger body are columns of the trigger's table.
    if (ref.kind == Name_kind::column && ref.qualifiers.size() == 1 &&
        (iequals(ref.qualifiers[0], "NEW") || iequals(ref.qualifiers[0], "OLD"))) {
      if (const uint32_t tt = ancestor(a, s.trigger_tail); tt != npos) {
        set_owner_from(m_tree.child(tt, s.table_ident) != npos ? tt : npos, s.table_ident, &ref);
      }
    }
    if (ref.kind == Name_kind::qualifier && ancestor(a, s.trigger_tail) != npos &&
        (iequals(ref.name, "NEW") || iequals(ref.name, "OLD"))) {
      ref.kind = Name_kind::other;
    }
    return ref;
  }
  if (sym == s.field_ident) {
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    if (ids.size() > 1) {
      dotted_column(a);
      return ref;
    }
    (void)at;
    const uint32_t p = m_tree.node(a).parent;
    const int ps = p == npos ? -1 : m_tree.node(p).symbol;
    if (ps == s.key_def) {
      ref.kind = Name_kind::index;
      ref.defining = true;
    } else {
      ref.kind = Name_kind::column;
      ref.defining = ps == s.field_spec;
    }
    uint32_t owner = ancestor(a, s.alter);
    if (owner == npos) owner = ancestor(a, s.create);
    if (owner != npos) {
      if (const uint32_t ti = m_tree.child(owner, s.table_ident); ti != npos) {
        const Table_ref t = table_ident_ref(ti);
        ref.has_owner = !t.name.empty();
        ref.owner_schema = t.schema;
        ref.owner = t.name;
      }
    }
    return ref;
  }
  if (sym == s.key_part_simple || sym == s.ref_list || sym == s.alter_list_item) {
    ref.kind = Name_kind::column;
    if (const uint32_t r = ancestor(a, s.references); r != npos) {
      if (const uint32_t ti = m_tree.child(r, s.table_ident); ti != npos) {
        const Table_ref t = table_ident_ref(ti);
        ref.has_owner = true;
        ref.owner_schema = t.schema;
        ref.owner = t.name;
      }
      return ref;
    }
    uint32_t owner = ancestor(a, s.alter);
    if (owner == npos) owner = ancestor(a, s.create);
    if (owner != npos) {
      if (const uint32_t ti = m_tree.child(owner, s.table_ident); ti != npos) {
        const Table_ref t = table_ident_ref(ti);
        ref.has_owner = !t.name.empty();
        ref.owner_schema = t.schema;
        ref.owner = t.name;
      }
    }
    return ref;
  }
  if (sym == s.using_list) {
    ref.kind = Name_kind::column;
    return ref;
  }
  if (sym == s.view_list) {
    ref.kind = Name_kind::column;
    ref.defining = true;
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
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    if (ids.size() >= 2 && at == 0) {
      ref.kind = Name_kind::schema;
    } else {
      ref.kind = Name_kind::qualifier;
      qualify(ids, at);
    }
    return ref;
  }
  if (sym == s.select_sublist_qualified_asterisk) {
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    ref.kind = ids.size() >= 2 && at == 0 ? Name_kind::schema
               : ids.size() >= 2          ? Name_kind::table
                                          : Name_kind::qualifier;
    qualify(ids, at);
    return ref;
  }
  if (sym == s.function_call_generic) {
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    // ident '(' ...: the parts before the '(' are the function's name.
    size_t before_paren = 0;
    for (const uint32_t c : kids) {
      if (m_tree.node(c).symbol == s.lparen) break;
      if (std::find(ids.begin(), ids.end(), c) != ids.end()) ++before_paren;
    }
    if (at + 1 == before_paren) {
      ref.kind = Name_kind::function;
      qualify(ids, at);
    } else if (at + 1 < before_paren) {
      ref.kind = Name_kind::schema;
    }
    return ref;
  }
  if (sym == s.sp_name || sym == s.call) {
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    if (ids.size() >= 2 && at == 0) {
      ref.kind = Name_kind::schema;
      return ref;
    }
    qualify(ids, at);
    if (sym == s.call) {
      ref.kind = Name_kind::routine;
      return ref;
    }
    const uint32_t p = m_tree.node(a).parent;
    const int ps = p == npos ? -1 : m_tree.node(p).symbol;
    if (ps == s.trigger_tail || ps == s.event_tail || ancestor(a, s.trigger_tail) != npos ||
        ancestor(a, s.event_tail) != npos) {
      ref.kind = Name_kind::other;
      ref.defining = ancestor(a, s.create) != npos;
    } else {
      ref.kind = Name_kind::routine;
      ref.defining = ancestor(a, s.create) != npos;
    }
    return ref;
  }
  if (sym == s.use) {
    ref.kind = Name_kind::schema;
    return ref;
  }
  if (sym == s.create || sym == s.drop || sym == s.alter) {
    if (m_tree.child(a, s.DATABASE) != npos) {
      ref.kind = Name_kind::schema;
      ref.defining = sym == s.create;
    } else if (sym == s.create && m_tree.child(a, s.INDEX_SYM) != npos) {
      ref.kind = Name_kind::index;
      ref.defining = true;
    } else {
      ref.kind = Name_kind::other;
      ref.defining = sym == s.create;
    }
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
    std::vector<uint32_t> ids;
    const size_t at = parts(a, &ids);
    if (ids.size() == 1) {
      ref.kind = Name_kind::variable;
    } else if (ids.size() == 2 && at == 1 && ancestor(a, s.trigger_tail) != npos) {
      ref.kind = Name_kind::column;
      qualify(ids, at);
      if (const uint32_t tt = ancestor(a, s.trigger_tail); tt != npos) {
        if (const uint32_t ti = m_tree.child(tt, s.table_ident); ti != npos) {
          const Table_ref t = table_ident_ref(ti);
          ref.has_owner = true;
          ref.owner_schema = t.schema;
          ref.owner = t.name;
        }
      }
    } else if (ids.size() == 2 && at == 0) {
      ref.kind = Name_kind::other;
    }
    return ref;
  }
  if (name_like) {
    // A name in a place the rules above do not know (an index hint, a
    // label, a REST path): something, but nothing we resolve.
    ref.kind = Name_kind::other;
  }
  return ref;
}

Name_ref Statement_names::classify_by_tokens(uint32_t token) const {
  const auto &s = syms();
  const auto &tokens = m_tree.tokens();
  Name_ref ref;
  const auto &tk = tokens[token];
  ref.range = {tk.begin, tk.end};
  ref.name = unquote_name(m_sql.substr(tk.begin - m_base, tk.end - tk.begin));
  auto tok_text = [&](uint32_t i) {
    return m_sql.substr(tokens[i].begin - m_base, tokens[i].end - tokens[i].begin);
  };
  auto is_name = [&](uint32_t i) {
    const int sy = tokens[i].symbol;
    return sy == s.IDENT || sy == s.IDENT_QUOTED || is_nonreserved_keyword(sy);
  };
  // Token i is one of `words`, a keyword (reserved or not), not a name.
  auto word_is = [&](uint32_t i, std::initializer_list<const char *> words) {
    if (tokens[i].symbol == s.IDENT || tokens[i].symbol == s.IDENT_QUOTED) return false;
    const auto t = tok_text(i);
    for (const char *w : words) {
      if (iequals(t, w)) return true;
    }
    return false;
  };

  // The dotted name this token ends: qualifiers before it.
  uint32_t first = token;
  while (first >= 2 && tokens[first - 1].symbol == s.dot && is_name(first - 2)) first -= 2;
  for (uint32_t i = first; i < token; i += 2) {
    ref.qualifiers.push_back(unquote_name(tok_text(i)));
    ref.qualifier_ranges.push_back({tokens[i].begin, tokens[i].end});
  }
  // A dotted name continues after the token: then it is a qualifier.
  const bool continues = token + 2 < tokens.size() && tokens[token + 1].symbol == s.dot &&
                         is_name(token + 2);

  // The context: the keyword before the name, skipping a FROM list.
  enum class Ctx { expression, table, schema, routine, insert_columns, statement } ctx =
      Ctx::expression;
  uint32_t insert_table = npos;
  static const int semicolon = grammar::symbol("';'");
  if (first > 0) {
    uint32_t p = first - 1;
    if (tokens[p].symbol == semicolon ||
        word_is(p, {"BEGIN", "THEN", "ELSE", "DO", "LOOP", "REPEAT"})) {
      // A statement starts here (inside a compound statement): a keyword,
      // or a label being defined.
      ctx = Ctx::statement;
    } else if (word_is(p, {"FROM", "JOIN", "STRAIGHT_JOIN", "INTO", "UPDATE", "TABLE", "TABLES",
                    "REFERENCES", "DESCRIBE", "DESC", "TRUNCATE", "LIKE"})) {
      // LIKE only after CREATE TABLE x
      if (!word_is(p, {"LIKE"}) || (p >= 2 && word_is(p - 2, {"TABLE"}))) ctx = Ctx::table;
    } else if (word_is(p, {"USE", "DATABASE", "SCHEMA"})) {
      ctx = Ctx::schema;
    } else if (word_is(p, {"CALL"})) {
      ctx = Ctx::routine;
    } else if (tokens[p].symbol == s.comma || is_left_paren(tokens[p].symbol)) {
      // In a list: FROM a, b | INSERT INTO t (a, b
      int depth = 0;
      for (uint32_t i = p + 1; i-- > 0;) {
        const int sy = tokens[i].symbol;
        if (sy == s.rparen) {
          ++depth;
        } else if (is_left_paren(sy)) {
          if (depth == 0) {
            // INTO name [(]
            if (i >= 2 && is_name(i - 1) && word_is(i - 2, {"INTO"})) {
              ctx = Ctx::insert_columns;
              insert_table = i - 1;
              break;
            }
            if (i >= 4 && is_name(i - 1) && tokens[i - 2].symbol == s.dot && is_name(i - 3) &&
                word_is(i - 4, {"INTO"})) {
              ctx = Ctx::insert_columns;
              insert_table = i - 1;
              break;
            }
            break;  // a function's arguments, a subquery: an expression
          }
          --depth;
        } else if (depth == 0 && word_is(i, {"FROM"})) {
          ctx = Ctx::table;
          break;
        } else if (depth == 0 && word_is(i, {"WHERE", "ON", "SELECT", "SET", "BY", "HAVING",
                                             "USING", "VALUES", "JOIN"})) {
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
      if (continues) {
        ref.kind = Name_kind::schema;
      } else {
        ref.kind = ref.qualifiers.empty() || ref.qualifiers.size() == 1 ? Name_kind::table
                                                                          : Name_kind::other;
      }
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
      ref.owner = unquote_name(tok_text(insert_table));
      if (insert_table >= 2 && tokens[insert_table - 1].symbol == s.dot) {
        ref.owner_schema = unquote_name(tok_text(insert_table - 2));
      }
      break;
    case Ctx::expression:
      if (token + 1 < tokens.size() && tokens[token + 1].symbol == s.lparen &&
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

std::vector<Name_ref> Statement_names::names() const {
  const auto &s = syms();
  std::vector<Name_ref> out;
  const auto &tokens = m_tree.tokens();
  for (uint32_t i = 0; i < tokens.size(); ++i) {
    const int sy = tokens[i].symbol;
    if (sy != s.IDENT && sy != s.IDENT_QUOTED && !is_nonreserved_keyword(sy)) continue;
    Name_ref r = classify(i);
    if (r.kind != Name_kind::none) out.push_back(std::move(r));
  }
  return out;
}

void Statement_names::derived_columns(uint32_t node, std::vector<std::string> *out) const {
  const auto &s = syms();
  const uint32_t q = m_tree.find(node, s.query_specification);
  if (q == npos) return;
  const uint32_t list = m_tree.find(q, s.select_item_list);
  if (list == npos) return;
  for (const uint32_t item : m_tree.find_all(list, s.select_item)) {
    if (const uint32_t al = m_tree.child(item, s.select_alias);
        al != npos && m_tree.node(al).token_count > 0) {
      const uint32_t id = m_tree.find(al, s.ident);
      // AS 'string' is an alias too
      out->push_back(id != npos ? name_of(id) : unquote_name(text(al)));
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
    if (sym == s.table_primary_ident || sym == s.table_name_with_opt_use_partition ||
        sym == s.delete_single_table) {
      const uint32_t ti = m_tree.child(n, s.table_ident);
      if (ti == npos) continue;
      Table_ref r = table_ident_ref(ti);
      if (const uint32_t al = m_tree.find(n, s.ident_table_alias); al != npos) {
        r.alias = name_of(al);
        r.alias_range = {m_tree.begin(al), m_tree.end(al)};
      }
      if (r.schema.empty()) {
        for (const auto &c : m_ctes) {
          if (iequals(c.name, r.name)) {
            r.derived = true;
            r.columns = c.columns;
          }
        }
      }
      if (!r.name.empty()) out->push_back(std::move(r));
      continue;
    }
    if (sym == s.table_primary_derived) {
      Table_ref r;
      r.derived = true;
      if (const uint32_t al = m_tree.find(n, s.ident_table_alias); al != npos) {
        r.alias = name_of(al);
        r.alias_range = {m_tree.begin(al), m_tree.end(al)};
      }
      derived_columns(n, &r.columns);
      out->push_back(std::move(r));
      continue;
    }
    const auto ch = m_tree.children(n);
    for (size_t k = ch.size(); k-- > 0;) stack.push_back(ch[k]);
  }
}

std::vector<Table_ref> Statement_names::tables_at(uint32_t offset) const {
  const auto &s = syms();
  std::vector<Table_ref> out;
  const uint32_t tok = token_at(offset);
  uint32_t leaf = tok == npos ? npos : leaf_of(m_tree, tok);
  if (leaf == npos && !m_tree.empty()) {
    // An offset between tokens: the nearest token before it.
    for (uint32_t i = 0; i < m_tree.tokens().size(); ++i) {
      if (m_tree.tokens()[i].begin > offset) break;
      if (const uint32_t l = leaf_of(m_tree, i); l != npos) leaf = l;
    }
  }
  bool found_scope = false;
  for (uint32_t n = leaf; n != npos; n = m_tree.node(n).parent) {
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
  const uint32_t leaf = tok == npos ? npos : leaf_of(m_tree, tok);
  for (uint32_t n = leaf; n != npos; n = m_tree.node(n).parent) {
    const int sym = m_tree.node(n).symbol;
    uint32_t q = npos;
    if (sym == s.query_specification) q = n;
    if (sym == s.query_expression_body_ext) q = m_tree.find(n, s.query_specification);
    if (q == npos) continue;
    const uint32_t list = m_tree.find(q, s.select_item_list);
    if (list == npos) break;
    for (const uint32_t item : m_tree.find_all(list, s.select_item)) {
      if (const uint32_t al = m_tree.child(item, s.select_alias);
          al != npos && m_tree.node(al).token_count > 0) {
        const uint32_t id = m_tree.find(al, s.ident);
        if (id != npos) out.push_back(name_of(id));
      }
    }
    break;
  }
  return out;
}

std::vector<Table_ref> Statement_names::scanned_tables() const {
  const auto &s = syms();
  std::vector<Table_ref> out;
  const auto &tokens = m_tree.tokens();
  auto tok_text = [&](size_t i) {
    return m_sql.substr(tokens[i].begin - m_base, tokens[i].end - tokens[i].begin);
  };
  auto is_name = [&](size_t i) {
    if (i >= tokens.size()) return false;
    const int sy = tokens[i].symbol;
    return sy == s.IDENT || sy == s.IDENT_QUOTED || is_nonreserved_keyword(sy);
  };
  auto is_word = [&](size_t i, std::initializer_list<const char *> words) {
    if (i >= tokens.size() || tokens[i].symbol == s.IDENT || tokens[i].symbol == s.IDENT_QUOTED)
      return false;
    for (const char *w : words) {
      if (iequals(tok_text(i), w)) return true;
    }
    return false;
  };
  bool in_from = false;
  int depth = 0;
  int from_depth = -1;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const int sy = tokens[i].symbol;
    if (is_left_paren(sy)) ++depth;
    if (sy == s.rparen) {
      --depth;
      if (depth < from_depth) in_from = false;
    }
    // INTO names a table after INSERT or REPLACE (and their options), not
    // in SELECT ... INTO a variable.
    bool into_table = false;
    if (is_word(i, {"INTO"})) {
      for (size_t k = i; k-- > 0 && k + 4 > i;) {
        if (is_word(k, {"INSERT", "REPLACE"})) into_table = true;
      }
    }
    const bool starts = is_word(i, {"FROM", "JOIN", "STRAIGHT_JOIN", "UPDATE"}) || into_table ||
                        (in_from && depth == from_depth && sy == s.comma);
    if (is_word(i, {"WHERE", "ON", "USING", "GROUP", "ORDER", "HAVING", "LIMIT", "SET",
                    "VALUES", "SELECT", "WINDOW", "UNION", "EXCEPT", "INTERSECT"})) {
      in_from = false;
    }
    if (!starts) continue;
    if (is_word(i, {"FROM", "JOIN", "STRAIGHT_JOIN"}) || sy == s.comma) {
      in_from = true;
      from_depth = depth;
    }
    size_t j = i + 1;
    Table_ref r;
    if (j < tokens.size() && is_left_paren(tokens[j].symbol)) {
      // (SELECT ...) alias
      int d = 0;
      for (; j < tokens.size(); ++j) {
        if (is_left_paren(tokens[j].symbol)) ++d;
        if (tokens[j].symbol == s.rparen && --d == 0) break;
      }
      ++j;
      r.derived = true;
    } else if (is_name(j)) {
      r.name = unquote_name(tok_text(j));
      r.name_range = {tokens[j].begin, tokens[j].end};
      if (j + 2 < tokens.size() && tokens[j + 1].symbol == s.dot && is_name(j + 2)) {
        r.schema = r.name;
        r.name = unquote_name(tok_text(j + 2));
        r.name_range = {tokens[j + 2].begin, tokens[j + 2].end};
        j += 2;
      }
      ++j;
    } else {
      continue;
    }
    if (is_word(j, {"AS"})) ++j;
    if (is_name(j) && !is_word(j, {"WHERE", "ON", "USING", "JOIN", "LEFT", "RIGHT", "INNER",
                                   "CROSS", "NATURAL", "SET", "VALUES", "GROUP", "ORDER",
                                   "LIMIT", "STRAIGHT_JOIN", "HAVING", "WINDOW", "USE",
                                   "IGNORE", "FORCE", "PARTITION", "FOR", "LOCK"})) {
      r.alias = unquote_name(tok_text(j));
      r.alias_range = {tokens[j].begin, tokens[j].end};
    }
    if (!r.name.empty() || r.derived) {
      for (const auto &c : m_ctes) {
        if (r.schema.empty() && iequals(c.name, r.name)) {
          r.derived = true;
          r.columns = c.columns;
        }
      }
      out.push_back(std::move(r));
    }
  }
  return out;
}

}  // namespace mdbls
