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

#include "statement_kinds.h"

namespace mdbls {

namespace {

struct Kind_syms {
  int select = grammar::symbol("select");
  int select_into = grammar::symbol("select_into");
  int insert = grammar::symbol("insert");
  int replace = grammar::symbol("replace");
  int update = grammar::symbol("update");
  int delete_ = grammar::symbol("delete");
  int create = grammar::symbol("create");
  int alter = grammar::symbol("alter");
  int view = grammar::symbol("VIEW_SYM");
  int user = grammar::symbol("USER_SYM");
  int create_body = grammar::symbol("create_body");
  int create_field_list_parens = grammar::symbol("create_field_list_parens");
  int opt_create_select = grammar::symbol("opt_create_select");
  int describe = grammar::symbol("describe");
  int analyze_stmt_command = grammar::symbol("analyze_stmt_command");
  int explainable_command = grammar::symbol("explainable_command");
  int grant = grammar::symbol("grant");
  int revoke = grammar::symbol("revoke");
};

const Kind_syms &syms() {
  static const Kind_syms s;
  return s;
}

bool is_query_or_dml(int sym) {
  const auto &s = syms();
  return sym == s.select || sym == s.select_into || sym == s.insert || sym == s.replace || sym == s.update ||
         sym == s.delete_;
}

}  // namespace

bool has_clauses(const Syntax_tree &tree, uint32_t n) {
  const auto &s = syms();
  const int sym = tree.node(n).symbol;
  constexpr uint32_t npos = Syntax_tree::npos;
  // EXPLAIN or ANALYZE of a query or DML statement; not DESCRIBE t or
  // EXPLAIN FOR CONNECTION.
  if (sym == s.describe || sym == s.analyze_stmt_command) {
    const uint32_t cmd = tree.child(n, s.explainable_command);
    return cmd != npos && tree.children(cmd).size() == 1 && is_query_or_dml(tree.node(tree.children(cmd)[0]).symbol);
  }
  if (sym == s.create || sym == s.alter) {
    if (tree.child(n, s.view) != npos || tree.child(n, s.user) != npos) return true;
    // CREATE TABLE ... AS SELECT without columns (theirs are lines of their
    // own already).
    const uint32_t body = tree.child(n, s.create_body);
    const uint32_t select = body == npos ? npos : tree.child(body, s.opt_create_select);
    return select != npos && tree.child(body, s.create_field_list_parens) == npos &&
           tree.begin(select) != tree.end(select);
  }
  return is_query_or_dml(sym) || sym == s.grant || sym == s.revoke;
}

}  // namespace mdbls
