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

// Helpers shared by the fuzz targets.

#ifndef MDBLS_FUZZ_FUZZ_UTIL_H
#define MDBLS_FUZZ_FUZZ_UTIL_H

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "mdbls/statement_index.h"

namespace mdbls {
namespace fuzz {

// A failed expectation: reported, and the input kept as a crash.
#define MDBLS_FUZZ_CHECK(cond)                                                     \
  do {                                                                             \
    if (!(cond)) {                                                                 \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      std::abort();                                                                \
    }                                                                              \
  } while (0)

// The options a text is split with: the sql_modes that change how it
// splits, and a delimiter other than ";".
inline Index_options consume_options(FuzzedDataProvider &data) {
  static const char *const modes[] = {"", "ANSI_QUOTES", "NO_BACKSLASH_ESCAPES",
                                      "ANSI_QUOTES,NO_BACKSLASH_ESCAPES"};
  static const char *const delimiters[] = {";", ";", "$$", "//"};
  Index_options options;
  const auto bits = data.ConsumeIntegral<uint8_t>();
  options.sql_mode = modes[bits & 3];
  options.delimiter = delimiters[(bits >> 2) & 3];
  if (bits & 0x10) options.version_id = 50700;
  return options;
}

// A statement with its delimiters and sql_mode spelled out, comparable
// between indexes.
using Flat_statement = std::tuple<uint32_t, uint32_t, uint32_t, std::string, std::string, std::string,
                                  bool, Statement_kind, bool, bool>;

inline std::vector<Flat_statement> flatten(const Statement_index &index) {
  std::vector<Flat_statement> out;
  for (const auto &s : index.statements()) {
    const auto &m = index.modes()[s.mode];
    out.emplace_back(s.begin, s.body_end, s.end, index.delimiters()[s.delimiter],
                     index.delimiters()[s.active_delimiter], m.mode, m.known, s.kind,
                     s.mentions_sql_mode, s.clean_after);
  }
  return out;
}

inline bool same_index(const Statement_index &a, const Statement_index &b) {
  return flatten(a) == flatten(b) && a.final_delimiter() == b.final_delimiter() &&
         a.open_at_end() == b.open_at_end() && a.errors() == b.errors();
}

// Pieces that make the splitter change state, for the edits of a fuzz input.
inline const std::vector<std::string> &edit_pieces() {
  static const std::vector<std::string> pieces = {
      ";", ";\n", "\n", " ", "x", "SELECT 1", "'", "\"", "`", "/*", "*/", "-- ", "#",
      "DELIMITER $$\n", "\nDELIMITER ;\n", "$$", "\\G", "/*!40101 ", "/*M!100100 ",
      "SET sql_mode='ANSI_QUOTES';\n", "SET sql_mode='NO_BACKSLASH_ESCAPES';\n",
      "SET @m = @@sql_mode;\n", "SET sql_mode = @m;\n", "\\'", "BEGIN SELECT 1; END",
      "\r\n", "\xc3\xa9", "\xf0\x9f\x98\x80", "\xff"};
  return pieces;
}

}  // namespace fuzz
}  // namespace mdbls

#endif  // MDBLS_FUZZ_FUZZ_UTIL_H
