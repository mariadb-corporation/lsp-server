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

#include <gtest/gtest.h>

#include <string>

#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "test_util.h"

namespace mdbls {

namespace {

// The tree's leaves in document order.
std::vector<uint32_t> leaves(const Syntax_tree &t) {
  std::vector<uint32_t> out;
  std::vector<uint32_t> todo(t.roots().rbegin(), t.roots().rend());
  while (!todo.empty()) {
    const uint32_t n = todo.back();
    todo.pop_back();
    if (t.is_token(n)) {
      out.push_back(n);
      continue;
    }
    const auto ch = t.children(n);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(*it);
  }
  return out;
}

void check_invariants(const Syntax_tree &t, const std::string &what) {
  ASSERT_EQ(1u, t.roots().size()) << what;
  EXPECT_STREQ("query", t.name(t.roots()[0])) << what;
  const auto ls = leaves(t);
  ASSERT_EQ(t.tokens().size(), ls.size()) << what;
  for (uint32_t i = 0; i < ls.size(); ++i) {
    EXPECT_EQ(i, t.node(ls[i]).first_token) << what;
  }
  for (uint32_t n = 0; n < t.size(); ++n) {
    if (t.is_token(n) || t.node(n).child_count == 0) continue;
    const auto ch = t.children(n);
    uint32_t covered = 0;
    for (const uint32_t c : ch) {
      EXPECT_EQ(n, t.node(c).parent) << what;
      covered += t.node(c).token_count;
    }
    EXPECT_EQ(t.node(n).token_count, covered) << what << " node " << t.name(n);
    EXPECT_EQ(t.node(n).first_token, t.node(ch[0]).first_token) << what;
  }
}

}  // namespace

TEST(Syntax_tree, simple_statement) {
  Parser_cache cache;
  mdbp_parser *p = cache.get("", 130100, true);
  const std::string sql = "SELECT a FROM t";
  mdbp_result r;
  ASSERT_EQ(MDBP_OK, mdbp_parse_sql(p, sql.data(), sql.size(), &r));
  const auto t = Syntax_tree::build(p, r, 100);
  check_invariants(t, sql);
  const uint32_t ident = t.find(t.roots()[0], grammar::symbol("table_ident"));
  ASSERT_NE(Syntax_tree::npos, ident);
  EXPECT_EQ(114u, t.begin(ident));  // offsets are absolute
  EXPECT_EQ(115u, t.end(ident));
  EXPECT_EQ(-1, grammar::symbol("no_such_rule"));
}

TEST(Syntax_tree, rejected_statement_keeps_partial_trees) {
  Parser_cache cache;
  mdbp_parser *p = cache.get("", 130100, true);
  const std::string sql = "SELECT a FROM t WHERE";
  mdbp_result r;
  ASSERT_EQ(MDBP_OK, mdbp_parse_sql(p, sql.data(), sql.size(), &r));
  ASSERT_FALSE(r.accepted);
  const auto t = Syntax_tree::build(p, r, 0);
  // Several partial trees: the select list, the FROM clause, ...
  EXPECT_GT(t.roots().size(), 1u);
  bool found = false;
  for (const uint32_t root : t.roots()) {
    found = found || t.find(root, grammar::symbol("table_ident")) != Syntax_tree::npos;
  }
  EXPECT_TRUE(found);
}

TEST(Syntax_tree, no_derivation_no_tree) {
  Parser_cache cache;
  mdbp_parser *p = cache.get("", 130100, false);
  const std::string sql = "SELECT 1";
  mdbp_result r;
  ASSERT_EQ(MDBP_OK, mdbp_parse_sql(p, sql.data(), sql.size(), &r));
  EXPECT_TRUE(Syntax_tree::build(p, r, 0).empty());
  EXPECT_EQ(nullptr, mdbp_derivation_depths(p));
}

// Every statement of the fixtures: a well-formed tree.
TEST(Syntax_tree, fixtures_are_well_formed) {
  Parser_cache cache;
  size_t trees = 0;
  for (const char *name : {"sakila-schema.sql", "mrs_grammar_test.sql"}) {
    const std::string text = test::read_fixture(name);
    const auto index = Statement_index::build(text);
    for (const auto &s : index.statements()) {
      if (s.kind != Statement_kind::sql) continue;
      mdbp_parser *p = cache.get(index.modes()[s.mode].mode, 130100, true);
      const auto sql = index.text_of(text, s);
      mdbp_result r;
      ASSERT_EQ(MDBP_OK, mdbp_parse_sql(p, sql.data(), sql.size(), &r));
      ASSERT_TRUE(r.accepted) << sql;
      check_invariants(Syntax_tree::build(p, r, s.begin), std::string(sql.substr(0, 60)));
      ++trees;
    }
  }
  EXPECT_GT(trees, 250u);
}

}  // namespace mdbls
