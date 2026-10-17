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

// Helpers shared by the language server's tests.

#ifndef MDBLS_UNITTEST_TEST_UTIL_H
#define MDBLS_UNITTEST_TEST_UTIL_H

#include <gtest/gtest.h>

#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

namespace mdbls {
namespace test {

// The whole of tests/fixtures/sql/<name>, byte for byte; empty when the file
// cannot be read.  `name` may be a relative path ("../outline/x.json").
inline std::string read_fixture(const std::string &name) {
  std::ifstream in(std::string(MDBLS_SQL_FIXTURE_DIR) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

// A fixture file name as a gtest parameter name: every character that is not
// alphanumeric becomes '_'.
inline std::string fixture_param_name(const ::testing::TestParamInfo<std::string> &info) {
  std::string name = info.param;
  for (auto &c : name)
    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  return name;
}

}  // namespace test
}  // namespace mdbls

#endif  // MDBLS_UNITTEST_TEST_UTIL_H
