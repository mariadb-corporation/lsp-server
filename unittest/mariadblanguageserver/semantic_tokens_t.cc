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
#include <vector>

#include "mdbls/parser_pool.h"
#include "mdbls/semantic_tokens.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"

namespace mdbls {

namespace {

struct Seen {
  std::string text;
  std::string type;
  bool library;
  bool operator==(const Seen &) const = default;
};

void PrintTo(const Seen &s, std::ostream *os) {
  *os << s.type << (s.library ? "+lib" : "") << " '" << s.text << "'";
}

std::vector<Seen> tokens(const std::string &text, const Index_options &o = {}) {
  const auto index = Statement_index::build(text, o);
  Parser_cache cache;
  std::vector<Semantic_token> toks;
  for (size_t i = 0; i < index.statements().size(); ++i) {
    statement_semantic_tokens(text, index, i, cache, 130100, &toks);
  }
  std::vector<Seen> out;
  for (const auto &t : toks) {
    out.push_back({text.substr(t.begin, t.end - t.begin),
                   semantic_token_types()[static_cast<size_t>(t.type)],
                   (t.modifiers & k_modifier_default_library) != 0});
  }
  return out;
}

}  // namespace

TEST(Semantic_tokens, classes) {
  const auto got = tokens(
      "SELECT a, 'x', 12, 1.5e3, 0x1F, @v, @@session.sql_mode, ?, COUNT(*), f(1), "
      "x >= 1 /* c */ FROM t -- end\n;");
  const std::vector<Seen> want = {
      {"SELECT", "keyword", false},     {"'x'", "string", false},
      {"12", "number", false},          {"1.5e3", "number", false},
      {"0x1F", "number", false},        {"@v", "variable", false},
      {"@@session.sql_mode", "variable", true},
      {"?", "parameter", false},        {"COUNT", "function", true},
      {"*", "operator", false},         {"f", "function", false},
      {"1", "number", false},           {">=", "operator", false},
      {"1", "number", false},           {"/* c */", "comment", false},
      {"FROM", "keyword", false},       {"-- end", "comment", false},
  };
  EXPECT_EQ(got, want);
}

TEST(Semantic_tokens, user_host_is_not_a_variable_and_tables_are_not_functions) {
  const auto got = tokens("CREATE USER 'a'@'h'; CREATE TABLE t(a INT);");
  const std::vector<Seen> want = {
      {"CREATE", "keyword", false}, {"USER", "keyword", false}, {"'a'", "string", false},
      {"@", "operator", false},     {"'h'", "string", false},   {"CREATE", "keyword", false},
      {"TABLE", "keyword", false},  {"INT", "keyword", false},
  };
  EXPECT_EQ(got, want);
}

TEST(Semantic_tokens, versioned_comments_and_comment_statements) {
  const auto got = tokens("# head\n/*!40101 SET x=1 */;\n");
  const std::vector<Seen> want = {
      {"# head", "comment", false}, {"SET", "keyword", false}, {"=", "operator", false},
      {"1", "number", false},
  };
  EXPECT_EQ(got, want);
}

TEST(Semantic_tokens, encoding_splits_tokens_at_line_breaks) {
  const std::string text = "SELECT 'a\nbc' ,\n  1";
  const auto index = Statement_index::build(text);
  Parser_cache cache;
  std::vector<Semantic_token> toks;
  statement_semantic_tokens(text, index, 0, cache, 130100, &toks);
  std::vector<uint32_t> data;
  Position last;
  encode_semantic_tokens(text, Line_map(text), toks, &data, &last);
  const std::vector<uint32_t> want = {
      0, 0, 6, 0, 0,  // SELECT
      0, 7, 2, 1, 0,  // 'a
      1, 0, 3, 1, 0,  // bc'
      1, 2, 1, 2, 0,  // 1
  };
  EXPECT_EQ(data, want);
}

TEST(Semantic_tokens, blocks) {
  const std::string text =
      "CREATE PROCEDURE p() BEGIN\n IF 1 THEN SELECT 1;\n END IF;\n"
      " WHILE 1 DO SELECT 2; END WHILE;\n"
      " SELECT CASE WHEN 1 THEN 2 END;\n"
      " DROP TABLE IF EXISTS t;\nEND";
  Index_options o;
  o.delimiter = "$$";
  const auto index = Statement_index::build(text, o);
  Parser_cache cache;
  auto blocks = statement_blocks(text, index, 0, cache, 130100);
  std::vector<std::string> got;
  for (auto [b, e] : blocks) got.push_back(text.substr(b, 5) + ".." + text.substr(e, 3));
  EXPECT_EQ(got, (std::vector<std::string>{"IF 1 ..END", "WHILE..END", "CASE ..END",
                                           "BEGIN..END"}));
}

TEST(Syntax_check, messages) {
  const std::string text = "SELECT FROM t";
  const auto index = Statement_index::build(text);
  Parser_cache cache;
  const auto c = check_statement(text, index, 0, cache, 130100);
  ASSERT_EQ(c.status, Check_status::syntax_error);
  EXPECT_EQ(syntax_error_near(text, c), "syntax error near 'FROM'");
  EXPECT_EQ(syntax_error_message(text, c, 0).rfind("syntax error near 'FROM'; expected ", 0), 0u);
  EXPECT_STREQ(open_context_name(Open_context::comment), "comment");
}

}  // namespace mdbls
