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

// The base protocol's framing: what Message_reader makes of an input.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "mdbls/lsp/transport.h"

namespace mdbls::lsp {

namespace {

std::string framed(const std::string &body) {
  return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

TEST(Transport, messages_come_one_by_one) {
  std::istringstream in(framed("{\"a\":1}") + "\r\n" + framed("{}"));
  Stream_input input(in);
  Message_reader reader(input);
  std::string body;
  ASSERT_TRUE(reader.next(&body));
  EXPECT_EQ(body, "{\"a\":1}");
  ASSERT_TRUE(reader.next(&body));
  EXPECT_EQ(body, "{}");
  EXPECT_FALSE(reader.next(&body));
}

TEST(Transport, a_body_longer_than_the_buffer_is_read_whole) {
  const std::string big(3u << 20, 'x');
  std::istringstream in(framed(big) + framed("{}"));
  Stream_input input(in);
  Message_reader reader(input);
  std::string body;
  ASSERT_TRUE(reader.next(&body));
  EXPECT_EQ(body, big);
  ASSERT_TRUE(reader.next(&body));
  EXPECT_EQ(body, "{}");
}

TEST(Transport, a_length_no_data_follows_ends_the_input) {
  // Neither is allocated: the body grows with what arrives.
  for (const char *length : {"100000000000", "18446744073709551615"}) {
    std::istringstream in(std::string("Content-Length: ") + length + "\r\n\r\n{}");
    Stream_input input(in);
    Message_reader reader(input);
    std::string body;
    EXPECT_FALSE(reader.next(&body)) << length;
    EXPECT_LT(body.capacity(), size_t{1} << 30) << length;
  }
}

TEST(Transport, a_malformed_header_ends_the_input) {
  std::istringstream in("Content-Length: many\r\n\r\n{}");
  Stream_input input(in);
  Message_reader reader(input);
  std::string body;
  EXPECT_FALSE(reader.next(&body));
}

}  // namespace mdbls::lsp
