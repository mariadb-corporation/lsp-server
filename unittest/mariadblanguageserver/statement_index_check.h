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

// Statement indexes compared as values, and the edits that exercise
// Statement_index::update(): shared by the unit tests and the fuzz targets,
// so without gtest.

#ifndef MDBLS_UNITTEST_STATEMENT_INDEX_CHECK_H
#define MDBLS_UNITTEST_STATEMENT_INDEX_CHECK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mdbls/statement_index.h"

namespace mdbls {
namespace test {

// A statement with its delimiters and sql_mode spelled out, so that two
// indexes compare regardless of the order their modes and delimiters were
// interned in.
struct Flat_statement {
  uint32_t begin, body_end, end;
  std::string delimiter, active_delimiter, mode;
  bool known;
  Statement_kind kind;
  bool mentions;
  bool clean_after;
  bool operator==(const Flat_statement &) const = default;
};

inline std::vector<Flat_statement> flatten(const Statement_index &index) {
  std::vector<Flat_statement> out;
  for (const auto &s : index.statements()) {
    const auto &m = index.modes()[s.mode];
    out.push_back({s.begin, s.body_end, s.end, index.delimiters()[s.delimiter],
                   index.delimiters()[s.active_delimiter], m.mode, m.known, s.kind,
                   s.mentions_sql_mode, s.clean_after});
  }
  return out;
}

// What an index holds besides its statements.
inline bool same_ends(const Statement_index &a, const Statement_index &b) {
  return a.final_delimiter() == b.final_delimiter() && a.open_at_end() == b.open_at_end() &&
         a.errors() == b.errors();
}

inline bool same_index(const Statement_index &a, const Statement_index &b) {
  return flatten(a) == flatten(b) && same_ends(a, b);
}

// "" when `after` is `before` changed as `splice` says: an unchanged
// prefix, a replaced middle, a shifted suffix; else what differs.
inline std::string splice_mismatch(const std::vector<Flat_statement> &before,
                                   const std::vector<Flat_statement> &after,
                                   const Index_splice &splice) {
  if (splice.first + splice.removed > before.size()) return "the splice removes past the end";
  if (before.size() - splice.removed + splice.added != after.size()) return "the sizes do not add up";
  for (size_t i = 0; i < splice.first; ++i) {
    if (after[i] != before[i]) return "statement " + std::to_string(i) + " of the prefix changed";
  }
  for (size_t i = splice.first + splice.removed; i < before.size(); ++i) {
    Flat_statement f = before[i];
    f.begin = static_cast<uint32_t>(f.begin + splice.delta);
    f.body_end = static_cast<uint32_t>(f.body_end + splice.delta);
    f.end = static_cast<uint32_t>(f.end + splice.delta);
    if (after[i - splice.removed + splice.added] != f) {
      return "statement " + std::to_string(i) + " of the suffix is not shifted";
    }
  }
  return {};
}

// Pieces that make the splitter change state.
inline const std::vector<std::string> &edit_pieces() {
  static const std::vector<std::string> pieces = {
      ";", ";\n", "\n", " ", "x", "SELECT 1", "'", "\"", "`", "/*", "*/", "-- ", "#",
      "DELIMITER $$\n", "\nDELIMITER ;\n", "$$", "\\G", "/*!40101 ", "/*M!100100 ",
      "SET sql_mode='ANSI_QUOTES';\n", "SET sql_mode='NO_BACKSLASH_ESCAPES';\n",
      "SET @m = @@sql_mode;\n", "SET sql_mode = @m;\n", "\\'", "BEGIN SELECT 1; END"};
  return pieces;
}

}  // namespace test
}  // namespace mdbls

#endif  // MDBLS_UNITTEST_STATEMENT_INDEX_CHECK_H
