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

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "mdbls/statement_index.h"
#include "mysqlshdk/libs/utils/utils_mysql_parsing.h"

namespace mdbls {

namespace {

struct Seen {
  std::string text;
  std::string delimiter;
  Statement_kind kind;
  bool operator==(const Seen &) const = default;
};

std::vector<Seen> split(const std::string &text, const Index_options &o = {}) {
  const auto index = Statement_index::build(text, o);
  std::vector<Seen> out;
  for (const auto &s : index.statements()) {
    out.push_back({std::string(index.text_of(text, s)), index.delimiters()[s.delimiter], s.kind});
  }
  return out;
}

std::string read_file(const std::string &name) {
  std::ifstream in(std::string(MDBLS_SQL_FIXTURE_DIR) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

TEST(Statement_index, simple_statements_and_offsets) {
  const std::string text = "SELECT 1;  SELECT 2;\nSELECT 3";
  const auto index = Statement_index::build(text);
  const auto &s = index.statements();
  ASSERT_EQ(3u, s.size());
  EXPECT_EQ(0u, s[0].begin);
  EXPECT_EQ(8u, s[0].body_end);
  EXPECT_EQ(9u, s[0].end);
  EXPECT_EQ(11u, s[1].begin);  // leading blanks are not part of a statement
  EXPECT_EQ("SELECT 3", index.text_of(text, s[2]));
  EXPECT_EQ(s[2].body_end, s[2].end);  // no delimiter at the end
  EXPECT_EQ("", index.delimiters()[s[2].delimiter]);
  EXPECT_EQ(Open_context::none, index.open_at_end());
}

TEST(Statement_index, delimiter_command) {
  const std::string text =
      "DELIMITER $$\n"
      "CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END$$\n"
      "DELIMITER ;\n"
      "SELECT 3;";
  const auto seen = split(text);
  ASSERT_EQ(2u, seen.size());
  EXPECT_EQ((Seen{"CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END", "$$",
                  Statement_kind::sql}),
            seen[0]);
  EXPECT_EQ((Seen{"SELECT 3", ";", Statement_kind::sql}), seen[1]);
}

TEST(Statement_index, comment_lines_are_their_own_entries) {
  const auto seen = split("-- header\n# another\nSELECT 1; /* c */ SELECT 2;");
  ASSERT_EQ(4u, seen.size());
  EXPECT_EQ(Statement_kind::comment, seen[0].kind);
  EXPECT_EQ(Statement_kind::comment, seen[1].kind);
  EXPECT_EQ((Seen{"SELECT 1", ";", Statement_kind::sql}), seen[2]);
  EXPECT_EQ((Seen{"/* c */ SELECT 2", ";", Statement_kind::sql}), seen[3]);
}

TEST(Statement_index, versioned_comment_is_sql) {
  const auto seen = split("/*!40101 SET NAMES utf8 */;");
  ASSERT_EQ(1u, seen.size());
  EXPECT_EQ(Statement_kind::sql, seen[0].kind);
}

TEST(Statement_index, backslash_g_ends_a_statement_other_backslashes_do_not) {
  const auto seen = split("SELECT 1\\G SELECT a \\x b;");
  ASSERT_EQ(2u, seen.size());
  EXPECT_EQ((Seen{"SELECT 1", "\\G", Statement_kind::sql}), seen[0]);
  EXPECT_EQ((Seen{"SELECT a \\x b", ";", Statement_kind::sql}), seen[1]);
}

TEST(Statement_index, dollar_signs_inside_a_versioned_comment) {
  // Regression: "$$ */" used to leave the comment open in the splitter, so
  // the delimiter after the next statement was missed.
  const auto seen = split("/*!90200 X $$ $$ */;\nset sql_mode='ansi';\nSELECT 2;");
  ASSERT_EQ(3u, seen.size());
  EXPECT_EQ("/*!90200 X $$ $$ */", seen[0].text);
  EXPECT_EQ("set sql_mode='ansi'", seen[1].text);
  EXPECT_EQ("SELECT 2", seen[2].text);
}

TEST(Statement_index, sql_mode_tracking_and_mode_ids) {
  const std::string text =
      "SELECT 1;\n"
      "SET sql_mode = 'NO_BACKSLASH_ESCAPES';\n"
      "SELECT 'a\\'; SELECT 2;\n"
      "SET sql_mode = DEFAULT;\n"
      "SELECT 3;";
  Index_options o;
  o.sql_mode = "STRICT_TRANS_TABLES";
  const auto index = Statement_index::build(text, o);
  const auto &s = index.statements();
  ASSERT_EQ(6u, s.size());
  EXPECT_EQ("SELECT 'a\\'", index.text_of(text, s[2]));
  EXPECT_EQ("STRICT_TRANS_TABLES", index.modes()[s[0].mode].mode);
  EXPECT_EQ("STRICT_TRANS_TABLES", index.modes()[s[1].mode].mode);  // the SET itself
  EXPECT_EQ("NO_BACKSLASH_ESCAPES", index.modes()[s[2].mode].mode);
  EXPECT_EQ("NO_BACKSLASH_ESCAPES", index.modes()[s[3].mode].mode);
  EXPECT_EQ("STRICT_TRANS_TABLES", index.modes()[s[5].mode].mode);
  EXPECT_EQ(s[0].mode, s[5].mode);  // interned
  EXPECT_EQ(2u, index.modes().size());
}

TEST(Statement_index, unterminated_string_at_end) {
  const std::string text = "SELECT 1; SELECT 'abc";
  const auto index = Statement_index::build(text);
  ASSERT_EQ(2u, index.statements().size());
  EXPECT_EQ("SELECT 'abc", index.text_of(text, index.statements()[1]));
  EXPECT_EQ(Open_context::single_quote_string, index.open_at_end());
}

TEST(Statement_index, find) {
  const std::string text = "SELECT 1;  SELECT 2;";
  const auto index = Statement_index::build(text);
  EXPECT_EQ(0u, index.find(0));
  EXPECT_EQ(0u, index.find(8));   // the delimiter belongs to its statement
  EXPECT_EQ(1u, index.find(9));   // blanks before a statement: the next one
  EXPECT_EQ(1u, index.find(15));
  EXPECT_EQ(2u, index.find(20));  // past the end
}

TEST(Statement_index, empty_text) {
  const auto index = Statement_index::build("");
  EXPECT_TRUE(index.statements().empty());
  EXPECT_EQ(";", index.final_delimiter());
}

// The index's single-pass driver against the shell's own chunked driver,
// iterate_sql_stream(), on real files and several chunk sizes.
class Statement_index_fixture : public ::testing::TestWithParam<std::string> {};

TEST_P(Statement_index_fixture, matches_the_shell_driver) {
  const std::string text = read_file(GetParam());
  ASSERT_FALSE(text.empty()) << GetParam();
  const auto index = Statement_index::build(text);

  for (size_t chunk : {size_t{64}, size_t{4096}, text.size()}) {
    std::istringstream in(text);
    std::vector<std::tuple<size_t, std::string, std::string>> shell;
    mysqlshdk::utils::iterate_sql_stream(
        &in, chunk,
        [&](std::string_view s, std::string_view delim, size_t, size_t offset) {
          shell.emplace_back(offset, std::string(s), std::string(delim));
          return true;
        },
        [](std::string_view) {}, false, false, false);

    ASSERT_EQ(shell.size(), index.statements().size()) << GetParam() << " chunk " << chunk;
    for (size_t i = 0; i < shell.size(); ++i) {
      const auto &s = index.statements()[i];
      EXPECT_EQ(std::get<0>(shell[i]), s.begin) << GetParam() << " #" << i;
      EXPECT_EQ(std::get<1>(shell[i]), index.text_of(text, s)) << GetParam() << " #" << i;
      EXPECT_EQ(std::get<2>(shell[i]), index.delimiters()[s.delimiter])
          << GetParam() << " #" << i;
    }
  }
  EXPECT_TRUE(index.errors().empty());
  EXPECT_EQ(Open_context::none, index.open_at_end());
}

INSTANTIATE_TEST_SUITE_P(Fixtures, Statement_index_fixture,
                         ::testing::Values("sakila-schema.sql", "world.sql",
                                           "mysqldump_t.sql"),
                         [](const auto &info) {
                           std::string n = info.param;
                           for (auto &c : n) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                           return n;
                         });

TEST(Statement_index, mysqldump_modes) {
  const std::string text = read_file("mysqldump_t.sql");
  Index_options o;
  o.sql_mode = "STRICT_TRANS_TABLES";
  const auto index = Statement_index::build(text, o);
  // The dump saves the mode and restores it: the last statement runs with
  // the connection's mode again.
  ASSERT_FALSE(index.statements().empty());
  for (const auto &m : index.modes()) EXPECT_TRUE(m.known) << m.mode;
  EXPECT_EQ("STRICT_TRANS_TABLES", index.modes()[index.statements().back().mode].mode);
}

}  // namespace mdbls
