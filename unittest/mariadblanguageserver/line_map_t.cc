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

#include "mdbls/line_map.h"

namespace mdbls {

TEST(Line_map, line_breaks_lf_crlf_cr) {
  const std::string text = "a\nbc\r\nd\re";
  Line_map map(text);
  ASSERT_EQ(4u, map.line_count());
  EXPECT_EQ(0u, map.line_start(0));
  EXPECT_EQ(2u, map.line_start(1));
  EXPECT_EQ(6u, map.line_start(2));
  EXPECT_EQ(8u, map.line_start(3));
  EXPECT_EQ(text.size(), map.line_start(4));

  EXPECT_EQ((Position{1, 2}), map.position(text, 4));  // the '\r' of "\r\n"
  EXPECT_EQ(1u, map.line_of(5));                       // the '\n' of "\r\n"
  EXPECT_EQ((Position{3, 0}), map.position(text, 8));
  EXPECT_EQ((Position{3, 1}), map.position(text, 9));  // end of text
}

TEST(Line_map, empty_text_and_trailing_newline) {
  Line_map empty("");
  EXPECT_EQ(1u, empty.line_count());
  EXPECT_EQ((Position{0, 0}), empty.position("", 0));

  const std::string text = "x\n";
  Line_map map(text);
  EXPECT_EQ(2u, map.line_count());
  EXPECT_EQ((Position{1, 0}), map.position(text, 2));
  EXPECT_EQ(2u, map.offset(text, {1, 0}));
}

TEST(Line_map, utf16_columns) {
  // e-acute (2 bytes, 1 unit), euro sign (3 bytes, 1 unit), emoji (4 bytes,
  // 2 units)
  const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";
  Line_map map(text);
  EXPECT_EQ(6u, utf16_length(text));
  EXPECT_EQ((Position{0, 0}), map.position(text, 0));
  EXPECT_EQ((Position{0, 1}), map.position(text, 1));
  EXPECT_EQ((Position{0, 2}), map.position(text, 3));
  EXPECT_EQ((Position{0, 3}), map.position(text, 6));
  EXPECT_EQ((Position{0, 5}), map.position(text, 10));
  // An offset inside a character maps to the character.
  EXPECT_EQ((Position{0, 1}), map.position(text, 2));
  EXPECT_EQ((Position{0, 3}), map.position(text, 8));

  EXPECT_EQ(0u, map.offset(text, {0, 0}));
  EXPECT_EQ(1u, map.offset(text, {0, 1}));
  EXPECT_EQ(3u, map.offset(text, {0, 2}));
  EXPECT_EQ(6u, map.offset(text, {0, 3}));
  EXPECT_EQ(6u, map.offset(text, {0, 4}));  // inside the surrogate pair
  EXPECT_EQ(10u, map.offset(text, {0, 5}));
  EXPECT_EQ(11u, map.offset(text, {0, 6}));
  EXPECT_EQ(11u, map.offset(text, {0, 99}));  // past the end of the line
}

TEST(Line_map, offset_clamps_to_line_content) {
  const std::string text = "ab\r\ncd";
  Line_map map(text);
  EXPECT_EQ(2u, map.offset(text, {0, 5}));  // before "\r\n"
  EXPECT_EQ(4u, map.offset(text, {1, 0}));
  EXPECT_EQ(text.size(), map.offset(text, {7, 0}));
}

TEST(Line_map, round_trip_every_character_start) {
  const std::string text =
      "SELECT 'caf\xC3\xA9';\r\n-- \xF0\x9F\x98\x80 note\nINSERT INTO t VALUES (1);\rEND";
  Line_map map(text);
  for (uint32_t off = 0; off <= text.size(); ++off) {
    if (off < text.size() && (static_cast<unsigned char>(text[off]) & 0xC0) == 0x80) continue;
    if (off > 0 && text[off - 1] == '\r' && off < text.size() && text[off] == '\n') continue;
    const Position p = map.position(text, off);
    EXPECT_EQ(off, map.offset(text, p)) << "offset " << off;
  }
}

}  // namespace mdbls
