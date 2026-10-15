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

// The MariaDB client library links and initialises.  No server is needed:
// connecting to a port nobody listens on must fail cleanly, not crash.

#include <gtest/gtest.h>

#include <mysql.h>

#include <string>

TEST(Mariadb_client, links_and_reports_its_version) {
  const std::string info = mysql_get_client_info();
  EXPECT_FALSE(info.empty());
  EXPECT_GE(mysql_get_client_version(), 30000ul);
}

TEST(Mariadb_client, connect_to_nothing_fails_cleanly) {
  ASSERT_EQ(0, mysql_library_init(0, nullptr, nullptr));
  MYSQL *m = mysql_init(nullptr);
  ASSERT_NE(nullptr, m);
  unsigned int timeout = 2;
  mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
  EXPECT_EQ(nullptr, mysql_real_connect(m, "127.0.0.1", "nobody", "", nullptr, 1,
                                        nullptr, 0));
  EXPECT_NE(0u, mysql_errno(m));
  mysql_close(m);
  mysql_library_end();
}
