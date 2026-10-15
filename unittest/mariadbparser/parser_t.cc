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

// Tests of libmariadbparser's C API: verdicts, error positions, the expected
// set, the derivation, lexing, version-gated comments, refusals, and the
// one-parser-per-thread contract.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "mdbparse.h"

namespace {

struct Parser_deleter {
  void operator()(mdbp_parser *p) const { mdbp_parser_free(p); }
};
using Parser = std::unique_ptr<mdbp_parser, Parser_deleter>;

Parser make_parser(const char *sql_mode = nullptr, int prepare_mode = 1) {
  mdbp_options opts;
  mdbp_options_init(&opts);
  opts.sql_mode = sql_mode;
  opts.prepare_mode = prepare_mode;
  return Parser{mdbp_parser_new(&opts)};
}

std::vector<std::string> token_names(const mdbp_result &r) {
  std::vector<std::string> names;
  for (size_t i = 0; i < r.ntokens; ++i) names.emplace_back(r.tokens[i].name);
  return names;
}

std::vector<std::string> expected_set(const mdbp_result &r) {
  std::vector<std::string> names;
  for (size_t i = 0; i < r.nexpected; ++i) names.emplace_back(r.expected[i]);
  return names;
}

mdbp_result parse(mdbp_parser *p, std::string_view sql) {
  mdbp_result r;
  EXPECT_EQ(MDBP_OK, mdbp_parse_sql(p, sql.data(), sql.size(), &r));
  return r;
}

bool contains(const std::vector<std::string> &v, std::string_view s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST(Mariadbparser, version_matches_header) {
  EXPECT_STREQ(std::to_string(MDBP_VERSION_MAJOR)
                   .append(".")
                   .append(std::to_string(MDBP_VERSION_MINOR))
                   .append(".")
                   .append(std::to_string(MDBP_VERSION_PATCH))
                   .c_str(),
               mdbp_version_string());
}

TEST(Mariadbparser, accepts_valid_statements) {
  auto p = make_parser();
  ASSERT_TRUE(p);
  for (const char *sql : {
           "SELECT a, b FROM t WHERE x = 1",
           "CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR(20) NOT NULL)",
           "INSERT INTO t VALUES (1, 'a'), (2, 'b')",
           "ALTER TABLE t ADD COLUMN c DATETIME DEFAULT CURRENT_TIMESTAMP",
           "WITH x AS (SELECT 1) SELECT * FROM x",
           "SELECT JSON_VALUE(doc, '$.a') FROM t",
       }) {
    auto r = parse(p.get(), sql);
    EXPECT_TRUE(r.accepted) << sql;
    EXPECT_EQ(0, r.error_seen) << sql;
  }
}

TEST(Mariadbparser, accepts_rest_sql) {
  auto p = make_parser();
  for (const char *sql : {
           "CONFIGURE REST METADATA ENABLED UPDATE IF AVAILABLE",
           "CREATE OR REPLACE REST SERVICE /myService COMMENT 'x'",
           "CREATE OR REPLACE REST SERVICE mike,'a@b.c'@/myService",
           "CREATE REST SCHEMA /sakila ON SERVICE /myService FROM sakila",
           "SHOW REST SERVICES",
           "SHOW CREATE REST SERVICE /myService",
           "USE REST SERVICE /myService",
           "GRANT REST ROLE 'admin' TO 'jane'@'MRS'",
           "REVOKE REST ROLE 'admin' FROM jane@MRS",
           "DROP REST SERVICE /myService",
           "CLONE REST SERVICE /a NEW REQUEST PATH /b",
       }) {
    auto r = parse(p.get(), sql);
    EXPECT_TRUE(r.accepted) << sql << " rejected at " << (r.error_token ? r.error_token : "?");
  }
}

// The words REST SQL made keywords stay usable as names in server SQL.
TEST(Mariadbparser, rest_keywords_are_still_names) {
  auto p = make_parser();
  for (const char *sql : {
           "CREATE TABLE service (rest INT, content TEXT, app INT, metadata JSON,"
           " files INT, users INT, roles INT, views INT, url TEXT, clone INT,"
           " configure INT, enabled INT, published INT)",
           "SELECT rest, service.content FROM service AS content WHERE app = 1",
           "USE rest",
           "GRANT rest TO jane",
           "REVOKE rest FROM jane",
           "SHOW CREATE TABLE service",
           "CREATE PROCEDURE p() rest: LOOP LEAVE rest; END LOOP rest",
           "SET @rest = 1",
       }) {
    auto r = parse(p.get(), sql);
    EXPECT_TRUE(r.accepted) << sql << " rejected at " << (r.error_token ? r.error_token : "?");
  }
}

TEST(Mariadbparser, rejection_reports_token_and_byte_position) {
  auto p = make_parser();
  auto r = parse(p.get(), "SELECT FROM WHERE");
  EXPECT_FALSE(r.accepted);
  ASSERT_EQ(1, r.error_seen);
  EXPECT_STREQ("FROM", r.error_token);
  EXPECT_EQ(7, r.error_byte_pos);
  EXPECT_EQ(1u, r.error_token_index);
}

TEST(Mariadbparser, expected_set_at_end_of_input) {
  auto p = make_parser();
  auto r = parse(p.get(), "SELECT * FROM t WHERE");
  EXPECT_FALSE(r.accepted);
  ASSERT_NE(nullptr, r.expected);
  const auto expected = expected_set(r);
  // An expression may start here: an identifier, a literal, NOT, '(' ...
  EXPECT_TRUE(contains(expected, "IDENT"));
  EXPECT_TRUE(contains(expected, "NUM"));
  EXPECT_TRUE(contains(expected, "NOT_SYM"));
  EXPECT_TRUE(contains(expected, "'('"));
  // ... but no statement keyword.
  EXPECT_FALSE(contains(expected, "SELECT_SYM") && contains(expected, "UPDATE_SYM"));
  EXPECT_TRUE(std::is_sorted(expected.begin(), expected.end()));
}

TEST(Mariadbparser, derivation_is_bison_rule_numbers) {
  auto p = make_parser();
  auto r = parse(p.get(), "SELECT 1");
  ASSERT_TRUE(r.accepted);
  ASSERT_NE(nullptr, r.derivation);
  ASSERT_GT(r.derivation_len, 0u);
  for (size_t i = 0; i < r.derivation_len; ++i) {
    EXPECT_GE(r.derivation[i], 1);
  }
  // Same statement, same derivation.
  std::vector<int> first(r.derivation, r.derivation + r.derivation_len);
  auto again = parse(p.get(), "SELECT 1");
  EXPECT_EQ(first, std::vector<int>(again.derivation,
                                    again.derivation + again.derivation_len));
}

TEST(Mariadbparser, parse_tokens_round_trip) {
  auto p = make_parser();
  auto lexed = parse(p.get(), "SELECT a FROM t");
  ASSERT_TRUE(lexed.accepted);
  const auto names = token_names(lexed);
  std::vector<int> derivation(lexed.derivation,
                              lexed.derivation + lexed.derivation_len);

  std::vector<mdbp_token> tokens;
  for (const auto &n : names) tokens.push_back({n.c_str(), nullptr, 0, -1, -1});
  mdbp_result r;
  ASSERT_EQ(MDBP_OK, mdbp_parse_tokens(p.get(), tokens.data(), tokens.size(), &r));
  EXPECT_TRUE(r.accepted);
  EXPECT_EQ(derivation, std::vector<int>(r.derivation, r.derivation + r.derivation_len));
}

TEST(Mariadbparser, unknown_terminal_is_unsupported) {
  auto p = make_parser();
  mdbp_token tokens[] = {{"SELECT_SYM", nullptr, 0, -1, -1},
                         {"NO_SUCH_TERMINAL", nullptr, 0, -1, -1}};
  mdbp_result r;
  EXPECT_EQ(MDBP_UNSUPPORTED, mdbp_parse_tokens(p.get(), tokens, 2, &r));
  EXPECT_EQ(1u, r.error_token_index);
}

TEST(Mariadbparser, lex_without_parsing) {
  auto p = make_parser();
  mdbp_result r;
  const std::string sql = "SELECT 1; SELECT `a b`";
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(p.get(), sql.data(), sql.size(), &r));
  EXPECT_EQ((std::vector<std::string>{"SELECT_SYM", "NUM", "';'", "SELECT_SYM",
                                      "IDENT_QUOTED", "END_OF_INPUT"}),
            token_names(r));
  EXPECT_EQ(10, r.tokens[3].byte_start);
  EXPECT_EQ(16, r.tokens[3].byte_end);
}

TEST(Mariadbparser, lex_prefix_stops_after_max_tokens) {
  auto p = make_parser();
  mdbp_result r;
  int more = -1;
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql_prefix(p.get(), "SELECT 1; SELECT 2", 18, 2, &r, &more));
  EXPECT_EQ((std::vector<std::string>{"SELECT_SYM", "NUM"}), token_names(r));
  EXPECT_EQ(1, more);
}

TEST(Mariadbparser, versioned_comment_follows_release_id) {
  auto p = make_parser();
  const std::string sql = "SELECT 1 /*!120000 , 2 */";
  mdbp_result r;

  ASSERT_EQ(MDBP_OK, mdbp_parser_set_version_id(p.get(), 110803));
  EXPECT_EQ(110803ul, mdbp_parser_version_id(p.get()));
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(p.get(), sql.data(), sql.size(), &r));
  EXPECT_EQ((std::vector<std::string>{"SELECT_SYM", "NUM", "END_OF_INPUT"}),
            token_names(r));

  ASSERT_EQ(MDBP_OK, mdbp_parser_set_version_id(p.get(), 120000));
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(p.get(), sql.data(), sql.size(), &r));
  EXPECT_EQ((std::vector<std::string>{"SELECT_SYM", "NUM", "','", "NUM",
                                      "END_OF_INPUT"}),
            token_names(r));

  EXPECT_EQ(MDBP_UNSUPPORTED, mdbp_parser_set_version_id(p.get(), 5));
  EXPECT_EQ(120000ul, mdbp_parser_version_id(p.get()));
}

TEST(Mariadbparser, sql_mode_changes_lexing) {
  const std::string sql = "SELECT \"x\" FROM t";
  mdbp_result r;

  auto plain = make_parser();
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(plain.get(), sql.data(), sql.size(), &r));
  EXPECT_EQ("TEXT_STRING", token_names(r)[1]);

  auto ansi = make_parser("ANSI_QUOTES");
  ASSERT_TRUE(ansi);
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(ansi.get(), sql.data(), sql.size(), &r));
  EXPECT_EQ("IDENT_QUOTED", token_names(r)[1]);
}

TEST(Mariadbparser, refuses_what_it_cannot_honour) {
  EXPECT_FALSE(make_parser("ORACLE"));
  EXPECT_FALSE(make_parser("MSSQL"));

  mdbp_options opts;
  mdbp_options_init(&opts);
  opts.charset = "latin1";
  EXPECT_EQ(nullptr, mdbp_parser_new(&opts));

  auto p = make_parser();
  mdbp_result r;
  const char bad_utf8[] = "SELECT '\xff'";
  EXPECT_EQ(MDBP_UNSUPPORTED, mdbp_parse_sql(p.get(), bad_utf8, sizeof bad_utf8 - 1, &r));
}

// One parser per thread, many threads at once: every thread must get the
// single-threaded answer.  The first calls also race the once-initialised
// process-wide tables.
TEST(Mariadbparser, parsers_on_many_threads_agree) {
  const std::vector<std::string> statements = {
      "SELECT a, b FROM t WHERE x = 1",
      "SELECT FROM WHERE",
      "CREATE TABLE t (id INT PRIMARY KEY)",
      "SELECT * FROM t WHERE",
      "UPDATE t SET a = a + 1 WHERE id IN (1, 2, 3)",
      "DROP TABLE IF EXISTS t1, t2",
  };
  struct Answer {
    int accepted;
    std::vector<int> derivation;
    long error_byte_pos;
    std::vector<std::string> expected;
    bool operator==(const Answer &) const = default;
  };
  auto answer_all = [&](mdbp_parser *p) {
    std::vector<Answer> answers;
    for (const auto &s : statements) {
      mdbp_result r;
      mdbp_parse_sql(p, s.data(), s.size(), &r);
      answers.push_back({r.accepted,
                         std::vector<int>(r.derivation, r.derivation + r.derivation_len),
                         r.error_seen ? r.error_byte_pos : -1, expected_set(r)});
    }
    return answers;
  };

  const unsigned n_threads = std::max(4u, std::thread::hardware_concurrency());
  std::vector<std::vector<Answer>> results(n_threads);
  {
    std::vector<std::jthread> threads;
    for (unsigned i = 0; i < n_threads; ++i) {
      threads.emplace_back([&, i] {
        auto p = make_parser();
        for (int round = 0; round < 50; ++round) results[i] = answer_all(p.get());
      });
    }
  }
  auto p = make_parser();
  const auto baseline = answer_all(p.get());
  for (unsigned i = 0; i < n_threads; ++i) EXPECT_EQ(baseline, results[i]) << "thread " << i;
}
