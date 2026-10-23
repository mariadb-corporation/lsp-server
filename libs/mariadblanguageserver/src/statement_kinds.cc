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
};

const Kind_syms &syms() {
  static const Kind_syms s;
  return s;
}

}  // namespace

bool has_clauses(const Syntax_tree &tree, uint32_t n) {
  const auto &s = syms();
  const int sym = tree.node(n).symbol;
  if (sym == s.create || sym == s.alter) return tree.child(n, s.view) != Syntax_tree::npos;
  return sym == s.select || sym == s.select_into || sym == s.insert || sym == s.replace || sym == s.update ||
         sym == s.delete_;
}

}  // namespace mdbls
