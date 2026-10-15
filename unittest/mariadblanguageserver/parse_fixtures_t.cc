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

// Every SQL statement of these fixtures must parse: the files are valid SQL
// (the sakila schema) and valid REST SQL (MariaDB Shell's grammar test file).

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "mdbls/statement_index.h"
#include "mdbparse.h"

namespace mdbls {

class Parse_fixture : public ::testing::TestWithParam<std::string> {};

TEST_P(Parse_fixture, every_statement_parses) {
  std::ifstream in(std::string(MDBLS_SQL_FIXTURE_DIR) + "/" + GetParam(), std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), {}};
  ASSERT_FALSE(text.empty());
  const auto index = Statement_index::build(text);

  std::unique_ptr<mdbp_parser, decltype(&mdbp_parser_free)> p{mdbp_parser_new(nullptr),
                                                              &mdbp_parser_free};
  ASSERT_TRUE(p);
  size_t n = 0;
  for (const auto &s : index.statements()) {
    if (s.kind != Statement_kind::sql) continue;
    ++n;
    const auto sql = index.text_of(text, s);
    mdbp_result r;
    ASSERT_EQ(MDBP_OK, mdbp_parse_sql(p.get(), sql.data(), sql.size(), &r)) << sql;
    EXPECT_TRUE(r.accepted) << GetParam() << ": rejected at "
                            << (r.error_token ? r.error_token : "?") << " (byte "
                            << r.error_byte_pos << "): " << sql.substr(0, 200);
  }
  EXPECT_GT(n, 0u);
}

INSTANTIATE_TEST_SUITE_P(Fixtures, Parse_fixture,
                         ::testing::Values("sakila-schema.sql", "mrs_grammar_test.sql"),
                         [](const auto &info) {
                           std::string name = info.param;
                           for (auto &c : name)
                             if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                           return name;
                         });

}  // namespace mdbls
