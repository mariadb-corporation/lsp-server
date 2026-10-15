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

#include "mdbls/sql_mode.h"

namespace mdbls {

TEST(Sql_mode, normalize) {
  EXPECT_EQ("ANSI_QUOTES,TRADITIONAL", normalize_sql_mode(" ansi_quotes, ,traditional "));
  EXPECT_EQ("", normalize_sql_mode(""));
  EXPECT_EQ("", normalize_sql_mode(" , "));
}

TEST(Sql_mode, bits_and_composites) {
  EXPECT_EQ(0u, sql_mode_bits("STRICT_TRANS_TABLES,NO_ZERO_DATE"));
  EXPECT_EQ(k_ansi_quotes, sql_mode_bits("ansi_quotes"));
  EXPECT_EQ(k_ansi_quotes | k_pipes_as_concat | k_ignore_space, sql_mode_bits("ANSI"));
  EXPECT_EQ(k_ansi_quotes | k_pipes_as_concat | k_ignore_space, sql_mode_bits("POSTGRESQL"));
  EXPECT_EQ(k_high_not_precedence, sql_mode_bits("MYSQL40"));
  EXPECT_EQ(k_no_backslash_escapes | k_high_not_precedence,
            sql_mode_bits("NO_BACKSLASH_ESCAPES,MYSQL323"));
  EXPECT_NE(0u, sql_mode_bits("ORACLE") & k_oracle);
  EXPECT_NE(0u, sql_mode_bits("ORACLE") & k_ansi_quotes);
  EXPECT_NE(0u, sql_mode_bits("MSSQL") & k_mssql);
}

TEST(Sql_mode_tracker, plain_assignments) {
  Sql_mode_tracker t("STRICT_TRANS_TABLES");
  EXPECT_FALSE(t.observe("SELECT 1"));
  EXPECT_TRUE(t.observe("SET sql_mode = 'ansi_quotes'"));
  EXPECT_EQ("ANSI_QUOTES", t.mode());
  EXPECT_EQ(k_ansi_quotes, t.bits());
  EXPECT_TRUE(t.observe("SET SESSION sql_mode=\"NO_BACKSLASH_ESCAPES\""));
  EXPECT_EQ("NO_BACKSLASH_ESCAPES", t.mode());
  EXPECT_TRUE(t.observe("set @@session.sql_mode := 'ANSI'"));
  EXPECT_EQ("ANSI", t.mode());
  EXPECT_TRUE(t.observe("set sql_mode=oracle"));  // an unquoted name
  EXPECT_EQ("ORACLE", t.mode());
  EXPECT_TRUE(t.observe("SET LOCAL sql_mode = DEFAULT"));
  EXPECT_EQ("STRICT_TRANS_TABLES", t.mode());
  EXPECT_TRUE(t.known());
}

TEST(Sql_mode_tracker, ignores_global_and_other_sets) {
  Sql_mode_tracker t("");
  EXPECT_FALSE(t.observe("SET GLOBAL sql_mode = 'ANSI'"));
  EXPECT_FALSE(t.observe("SET @@global.sql_mode = 'ANSI'"));
  EXPECT_FALSE(t.observe("SET NAMES utf8mb4"));
  EXPECT_FALSE(t.observe("SELECT @@sql_mode"));
  EXPECT_FALSE(t.observe("SET sql_mode = @@sql_mode"));
  EXPECT_EQ("", t.mode());
}

TEST(Sql_mode_tracker, mysqldump_save_and_restore) {
  Sql_mode_tracker t("STRICT_TRANS_TABLES,ERROR_FOR_DIVISION_BY_ZERO");
  EXPECT_TRUE(t.observe(
      "/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */"));
  EXPECT_EQ("NO_AUTO_VALUE_ON_ZERO", t.mode());
  EXPECT_TRUE(t.observe("/*!40101 SET SQL_MODE=@OLD_SQL_MODE */"));
  EXPECT_EQ("STRICT_TRANS_TABLES,ERROR_FOR_DIVISION_BY_ZERO", t.mode());
  EXPECT_TRUE(t.known());
}

TEST(Sql_mode_tracker, versioned_comments_follow_the_release_id) {
  Sql_mode_tracker t("", 110803);
  EXPECT_FALSE(t.observe("/*!120000 SET sql_mode='ANSI' */"));   // newer release
  EXPECT_FALSE(t.observe("/*!50700 SET sql_mode='ANSI' */"));    // MySQL-only range
  EXPECT_FALSE(t.observe("/* SET sql_mode='ANSI' */"));          // plain comment
  EXPECT_TRUE(t.observe("/*M!100100 SET sql_mode='ANSI' */"));
  EXPECT_EQ("ANSI", t.mode());
}

TEST(Sql_mode_tracker, unknown_values) {
  Sql_mode_tracker t("ANSI_QUOTES");
  EXPECT_TRUE(t.observe("SET sql_mode = CONCAT(@@sql_mode, ',NO_BACKSLASH_ESCAPES')"));
  EXPECT_FALSE(t.known());
  EXPECT_EQ("ANSI_QUOTES", t.mode());  // the last known mode is kept
  EXPECT_TRUE(t.observe("SET sql_mode = ''"));
  EXPECT_TRUE(t.known());
  EXPECT_TRUE(t.observe("SET sql_mode = @never_assigned"));
  EXPECT_FALSE(t.known());
  EXPECT_TRUE(t.observe("SET sql_mode = ''"));
  EXPECT_TRUE(t.observe("SET sql_mode = 1"));
  EXPECT_FALSE(t.known());
}

}  // namespace mdbls
