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

#include "mdbls/charset.h"
#include "mdbls/line_map.h"

namespace mdbls {

TEST(Charset, names) {
  EXPECT_EQ(File_charset::utf8mb4, parse_charset_name("UTF8MB4"));
  EXPECT_EQ(File_charset::utf8mb4, parse_charset_name("utf8"));
  EXPECT_EQ(File_charset::latin1, parse_charset_name("Latin1"));
  EXPECT_EQ(File_charset::latin1, parse_charset_name("cp1252"));
  EXPECT_FALSE(parse_charset_name("latin2"));
}

TEST(Charset, first_invalid_utf8) {
  EXPECT_FALSE(first_invalid_utf8(""));
  EXPECT_FALSE(first_invalid_utf8("caf\xC3\xA9 \xF0\x9F\x98\x80"));
  EXPECT_EQ(3u, first_invalid_utf8("abc\xB4"));           // stray continuation byte
  EXPECT_EQ(1u, first_invalid_utf8("a\xC3"));             // truncated sequence
  EXPECT_EQ(0u, first_invalid_utf8("\xC0\x80"));          // overlong
  EXPECT_EQ(0u, first_invalid_utf8("\xED\xA0\x80"));      // surrogate
  EXPECT_EQ(0u, first_invalid_utf8("\xF4\x90\x80\x80"));  // above U+10FFFF
}

TEST(Charset, latin1_is_mariadbs_cp1252) {
  EXPECT_EQ("a\xC2\xB4z", latin1_to_utf8("a\xB4z"));   // acute accent
  EXPECT_EQ("\xC3\xA9", latin1_to_utf8("\xE9"));       // e-acute
  EXPECT_EQ("\xE2\x82\xAC", latin1_to_utf8("\x80"));   // euro sign
  EXPECT_EQ("\xC2\x81", latin1_to_utf8("\x81"));       // undefined in cp1252: U+0081
  EXPECT_EQ("\xC5\xB8", latin1_to_utf8("\x9F"));       // Y-diaeresis
  EXPECT_EQ("plain", latin1_to_utf8("plain"));
  std::string all;
  for (int b = 0; b < 256; ++b) all.push_back(static_cast<char>(b));
  EXPECT_FALSE(first_invalid_utf8(latin1_to_utf8(all)));
}

TEST(Charset, latin1_comes_back_byte_for_byte) {
  std::string all;
  for (int b = 0; b < 256; ++b) all.push_back(static_cast<char>(b));
  EXPECT_EQ(all, utf8_to_latin1(latin1_to_utf8(all)));
  EXPECT_EQ("a?b", utf8_to_latin1("a\xE4\xB8\xAD" "b"));  // not in latin1
}

TEST(Charset, columns_survive_the_conversion) {
  const std::string latin1 = "SELECT '\xE9t\xE9', x";
  const std::string utf8 = latin1_to_utf8(latin1);
  const Line_map map(utf8);
  // 'x' is at 0-based character 14 in the Latin-1 file, and at column 14.
  EXPECT_EQ(14u, map.position(utf8, static_cast<uint32_t>(utf8.find('x'))).character);
}

TEST(Charset, declarations) {
  const std::string text =
      "/*!40101 SET NAMES latin1 */;\n"
      "SET character_set_client = utf8mb4;\n"
      "SELECT 'set names x';\n"
      "SET CHARACTER SET 'cp1252';\n"
      "SET NAMES DEFAULT;\n"
      "set charset utf8mb4;\n";
  const auto all = charset_declarations(text);
  ASSERT_EQ(3u, all.size());
  EXPECT_EQ("latin1", all[0].name);
  EXPECT_EQ(0u, all[0].offset);
  EXPECT_EQ("cp1252", all[1].name);
  EXPECT_EQ("utf8mb4", all[2].name);
  EXPECT_EQ("cp1252", declared_charset(text, text.find("SET NAMES DEFAULT")));
  EXPECT_FALSE(declared_charset(text, 5));
}

}  // namespace mdbls
