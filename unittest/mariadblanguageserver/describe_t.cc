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
#include <rapidjson/document.h>

#include <set>
#include <string>
#include <vector>

#include "mdbls/describe.h"
#include "mdbls/line_map.h"
#include "mdbparse.h"

namespace mdbls {

namespace {

std::vector<Statement_description> describe(const std::string &text) {
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  return describe_statements(text, index, pool);
}

// "schema.name" or "name".
std::string str(const Object_name &n) { return n.schema ? *n.schema + "." + n.name : n.name; }

std::vector<std::string> names(const Statement_description &d) {
  std::vector<std::string> out;
  for (const auto &o : d.objects) out.push_back(str(o.name));
  return out;
}

}  // namespace

// The three statements msm cannot route through the outline: a DROP of an
// object the text never created gives no outline item.
TEST(Describe, drops_msm_could_not_route) {
  const auto d = describe(
      "DROP VIEW v;\n"
      "DROP TABLE IF EXISTS t, s.u;\n"
      "DROP REST VIEW /n FROM SERVICE /app SCHEMA /main;\n");
  ASSERT_EQ(3u, d.size());
  EXPECT_EQ("drop", d[0].verb);
  EXPECT_EQ("view", d[0].kind);
  EXPECT_EQ((std::vector<std::string>{"v"}), names(d[0]));
  EXPECT_FALSE(d[0].if_exists);
  EXPECT_FALSE(d[0].rest);

  EXPECT_EQ("table", d[1].kind);
  EXPECT_TRUE(d[1].if_exists);
  EXPECT_EQ((std::vector<std::string>{"t", "s.u"}), names(d[1]));
  EXPECT_FALSE(d[1].objects[0].name.schema);

  EXPECT_EQ("drop", d[2].verb);
  EXPECT_EQ("rest_view", d[2].kind);
  ASSERT_TRUE(d[2].rest);
  ASSERT_TRUE(d[2].rest->service);
  EXPECT_EQ("/app", d[2].rest->service->path);
  EXPECT_TRUE(d[2].rest->service->written);
  ASSERT_TRUE(d[2].rest->schema);
  EXPECT_EQ("/main", d[2].rest->schema->path);
  EXPECT_EQ("/n", d[2].rest->path.value_or(""));
}

TEST(Describe, kinds_and_flags) {
  struct Case {
    const char *sql;
    const char *verb;
    const char *kind;
    const char *flags;  // R: or_replace, E: if_exists, N: if_not_exists, T: temporary
    const char *names;  // comma-separated
  };
  const Case cases[] = {
      {"CREATE OR REPLACE TABLE s.t (a INT)", "create", "table", "R", "s.t"},
      {"CREATE TEMPORARY TABLE IF NOT EXISTS t2 LIKE t", "create", "table", "NT", "t2"},
      {"DROP TEMPORARY TABLE IF EXISTS t, s.u", "drop", "table", "ET", "t,s.u"},
      {"DROP TABLES a", "drop", "table", "", "a"},
      {"CREATE VIEW v AS SELECT * FROM other_table", "create", "view", "", "v"},
      {"CREATE DEFINER=event@localhost VIEW v AS SELECT 1", "create", "view", "", "v"},
      {"CREATE SEQUENCE IF NOT EXISTS seq", "create", "sequence", "N", "seq"},
      {"DROP SEQUENCE seq, s.seq2", "drop", "sequence", "", "seq,s.seq2"},
      {"CREATE OR REPLACE PROCEDURE s.p() CREATE TABLE IF NOT EXISTS inner_t (a INT)", "create", "procedure",
       "R", "s.p"},
      {"CREATE FUNCTION IF NOT EXISTS f() RETURNS INT RETURN 1", "create", "function", "N", "f"},
      {"CREATE FUNCTION udf RETURNS STRING SONAME 'x.so'", "create", "function", "", "udf"},
      {"DROP PROCEDURE IF EXISTS s.p", "drop", "procedure", "E", "s.p"},
      {"DROP FUNCTION f", "drop", "function", "", "f"},
      {"ALTER PROCEDURE p COMMENT 'x'", "alter", "procedure", "", "p"},
      {"DROP TRIGGER IF EXISTS s.tr", "drop", "trigger", "E", "s.tr"},
      {"CREATE EVENT IF NOT EXISTS e ON SCHEDULE EVERY 1 DAY DO SET @a = 1", "create", "event", "N", "e"},
      {"DROP EVENT e", "drop", "event", "", "e"},
      {"CREATE DATABASE IF NOT EXISTS d", "create", "schema", "N", "d"},
      {"DROP SCHEMA `my db`", "drop", "schema", "", "my db"},
      {"ALTER DATABASE d CHARACTER SET utf8mb4", "alter", "schema", "", "d"},
      {"ALTER SEQUENCE IF EXISTS q RESTART", "alter", "sequence", "E", "q"},
      {"ALTER TABLE `s`.`t` ADD COLUMN b INT", "alter", "table", "", "s.t"},
      {"CREATE OR REPLACE USER u", "create", "other", "R", ""},
      {"SELECT 1", "other", "other", "", ""},
      {"INSERT INTO t VALUES (1)", "other", "other", "", ""},
  };
  for (const Case &c : cases) {
    const auto d = describe(std::string(c.sql) + ";\n");
    ASSERT_EQ(1u, d.size()) << c.sql;
    EXPECT_EQ(Describe_status::ok, d[0].status) << c.sql << ": " << d[0].message;
    EXPECT_EQ(c.verb, d[0].verb) << c.sql;
    EXPECT_EQ(c.kind, d[0].kind) << c.sql;
    std::string flags;
    if (d[0].or_replace) flags += 'R';
    if (d[0].if_exists) flags += 'E';
    if (d[0].if_not_exists) flags += 'N';
    if (d[0].temporary) flags += 'T';
    EXPECT_EQ(c.flags, flags) << c.sql;
    std::string joined;
    for (const auto &n : names(d[0])) joined += (joined.empty() ? "" : ",") + n;
    EXPECT_EQ(c.names, joined) << c.sql;
  }
}

TEST(Describe, tables_of_triggers_and_indexes_and_renames) {
  const auto d = describe(
      "CREATE OR REPLACE TRIGGER s.tr BEFORE INSERT ON s.t FOR EACH ROW SET @a = 1;\n"
      "CREATE UNIQUE INDEX i ON t (a);\n"
      "DROP INDEX IF EXISTS i ON s.t;\n"
      "RENAME TABLE a TO b, s.c TO s2.d;\n"
      "ALTER TABLE s.t ADD COLUMN b INT, RENAME TO s2.t2;\n"
      "ALTER EVENT e RENAME TO e2;\n");
  ASSERT_EQ(6u, d.size());
  ASSERT_EQ(1u, d[0].objects.size());
  ASSERT_TRUE(d[0].objects[0].table);
  EXPECT_EQ("s.t", str(*d[0].objects[0].table));
  EXPECT_TRUE(d[0].or_replace);

  EXPECT_EQ("index", d[1].kind);
  ASSERT_EQ(1u, d[1].objects.size());
  EXPECT_EQ("i", str(d[1].objects[0].name));
  EXPECT_EQ("t", str(*d[1].objects[0].table));

  EXPECT_TRUE(d[2].if_exists);
  EXPECT_EQ("s.t", str(*d[2].objects[0].table));

  EXPECT_EQ("rename", d[3].verb);
  ASSERT_EQ(2u, d[3].objects.size());
  EXPECT_EQ("a", str(d[3].objects[0].name));
  EXPECT_EQ("b", str(*d[3].objects[0].new_name));
  EXPECT_EQ("s.c", str(d[3].objects[1].name));
  EXPECT_EQ("s2.d", str(*d[3].objects[1].new_name));

  ASSERT_EQ(1u, d[4].objects.size());
  EXPECT_EQ("s2.t2", str(*d[4].objects[0].new_name));

  ASSERT_EQ(1u, d[5].objects.size());
  EXPECT_EQ("e2", str(*d[5].objects[0].new_name));
}

TEST(Describe, current_schema_follows_use) {
  const auto d = describe(
      "CREATE TABLE a (x INT);\n"
      "USE shop;\n"
      "DROP TABLE b;\n");
  ASSERT_EQ(3u, d.size());
  EXPECT_FALSE(d[0].current_schema);
  EXPECT_EQ("use", d[1].verb);
  EXPECT_EQ("schema", d[1].kind);
  EXPECT_EQ((std::vector<std::string>{"shop"}), names(d[1]));
  EXPECT_FALSE(d[1].current_schema);  // the USE in force before it
  EXPECT_EQ("shop", d[2].current_schema.value_or(""));
  EXPECT_FALSE(d[2].objects[0].name.schema);  // never filled in from current_schema
}

// The service and schema a REST statement does not name are those the
// outline's fold has in use before it; null when none is.
TEST(Describe, rest_resolution) {
  const auto d = describe(
      "CREATE OR REPLACE REST SERVICE mike, anna@/svc;\n"
      "CREATE REST VIEW /early AS db.t;\n"
      "USE REST SERVICE anna,mike@/svc;\n"
      "CREATE REST SCHEMA IF NOT EXISTS FROM sakila;\n"
      "CREATE REST VIEW /v AS sakila.actor;\n"
      "ALTER REST VIEW /v NEW REQUEST PATH /v2;\n"
      "ALTER REST SERVICE /x NEW REQUEST PATH /y;\n"
      "DROP REST SCHEMA /main FROM SERVICE /app FOR SCHEMA db;\n"
      "CREATE REST AUTH APP a VENDOR MRS;\n"
      "CREATE REST USER 'u'@'a' IDENTIFIED BY 'x';\n");
  ASSERT_EQ(10u, d.size());
  for (const auto &x : d) EXPECT_EQ(Describe_status::ok, x.status) << x.message;

  EXPECT_EQ("rest_service", d[0].kind);
  EXPECT_TRUE(d[0].or_replace);
  ASSERT_TRUE(d[0].rest->service);
  EXPECT_EQ("/svc", d[0].rest->service->path);
  EXPECT_EQ((std::vector<std::string>{"anna", "mike"}), d[0].rest->service->developers);

  EXPECT_FALSE(d[1].rest->service);  // creating a service does not make it current
  EXPECT_FALSE(d[1].rest->schema);
  EXPECT_EQ("/early", d[1].rest->path.value_or(""));

  EXPECT_EQ("use", d[2].verb);
  EXPECT_EQ("rest_service", d[2].kind);

  EXPECT_EQ("rest_schema", d[3].kind);
  EXPECT_TRUE(d[3].if_not_exists);
  ASSERT_TRUE(d[3].rest->service);
  EXPECT_FALSE(d[3].rest->service->written);
  EXPECT_EQ((std::vector<std::string>{"anna", "mike"}), d[3].rest->service->developers);
  EXPECT_EQ("/sakila", d[3].rest->schema->path);  // no path: /<database schema>
  EXPECT_EQ("sakila", d[3].rest->database_schema.value_or(""));

  // The service's first schema became current.
  ASSERT_TRUE(d[4].rest->schema);
  EXPECT_EQ("/sakila", d[4].rest->schema->path);
  EXPECT_FALSE(d[4].rest->schema->written);

  EXPECT_EQ("alter", d[5].verb);
  EXPECT_EQ("/v2", d[5].rest->new_path.value_or(""));

  EXPECT_EQ("/x", d[6].rest->service->path);
  EXPECT_EQ("/y", d[6].rest->new_path.value_or(""));

  EXPECT_EQ("rest_schema", d[7].kind);
  EXPECT_EQ("/app", d[7].rest->service->path);
  EXPECT_EQ("/main", d[7].rest->schema->path);
  EXPECT_EQ("db", d[7].rest->database_schema.value_or(""));

  EXPECT_EQ("rest_auth_app", d[8].kind);
  EXPECT_EQ((std::vector<std::string>{"a"}), names(d[8]));
  EXPECT_EQ("rest_user", d[9].kind);
  EXPECT_EQ((std::vector<std::string>{"u@a"}), names(d[9]));
}

TEST(Describe, statuses) {
  const auto d = describe(
      "-- a comment of its own\n"
      "SELEC 1;\n"
      "SOURCE 'other.sql';\n"
      "DROP VIEW v;\n");
  ASSERT_EQ(3u, d.size());  // the comment is left out
  EXPECT_EQ(Describe_status::syntax_error, d[0].status);
  EXPECT_NE(std::string::npos, d[0].message.find("SELEC")) << d[0].message;
  EXPECT_TRUE(d[0].verb.empty());
  EXPECT_TRUE(d[0].objects.empty());
  EXPECT_EQ(Describe_status::source_directive, d[1].status);
  EXPECT_EQ(Describe_status::ok, d[2].status);
}

TEST(Describe, json) {
  const std::string text = "DROP TABLE IF EXISTS s.t;\nSELEC 1;\n";
  static Parser_pool pool(1);
  const auto index = Statement_index::build(text);
  const Line_map lines(text);
  const std::string json = describe_json(describe_statements(text, index, pool), text, index, lines);
  rapidjson::Document doc;
  ASSERT_FALSE(doc.Parse(json.c_str()).HasParseError()) << json;
  EXPECT_EQ(1, doc["format"].GetInt());
  const auto &st = doc["statements"];
  ASSERT_EQ(2u, st.Size());
  EXPECT_STREQ("ok", st[0]["status"].GetString());
  EXPECT_STREQ("drop", st[0]["verb"].GetString());
  EXPECT_TRUE(st[0]["if_exists"].GetBool());
  EXPECT_STREQ("s", st[0]["objects"][0]["schema"].GetString());
  EXPECT_TRUE(st[0]["rest"].IsNull());
  EXPECT_FALSE(st[0].HasMember("message"));
  EXPECT_STREQ("syntax-error", st[1]["status"].GetString());
  EXPECT_TRUE(st[1]["verb"].IsNull());
  EXPECT_TRUE(st[1].HasMember("error_range"));
}

// List input: each item one statement, described in order as one text.
TEST(Describe, list_items) {
  static Parser_pool pool(2);
  const std::vector<std::string> items = {
      "DROP VIEW v;",
      "CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END;",  // its `;` need no DELIMITER
      "  -- only a comment\n",
      "DROP VIEW a; DROP VIEW b;",
      "SELECT 'open",
      "DELIMITER //\nCREATE PROCEDURE q() BEGIN SELECT 1; END//",
      "SET sql_mode = 'ANSI_QUOTES'",
      "CREATE TABLE \"x y\" (a INT)",  // the sql_mode of the item before
      "USE REST SERVICE /app",
      "DROP REST VIEW /n",  // the service in use, from the item before
      "SELEC 1",
      "",
  };
  const auto d = describe_list(items, pool);
  ASSERT_EQ(items.size(), d.size());
  auto status = [&](size_t i) { return d[i].description.status; };

  EXPECT_EQ(Describe_status::ok, status(0));
  EXPECT_EQ("drop", d[0].description.verb);
  EXPECT_EQ(0u, d[0].begin);
  EXPECT_EQ(11u, d[0].body_end);  // before the trailing `;`, as in a text
  EXPECT_EQ(12u, d[0].end);

  EXPECT_EQ(Describe_status::ok, status(1));
  EXPECT_EQ("procedure", d[1].description.kind);
  EXPECT_EQ("p", str(d[1].description.objects[0].name));

  EXPECT_EQ(Describe_status::empty, status(2));
  EXPECT_EQ(Describe_status::several_statements, status(3));
  EXPECT_EQ("the item holds 2 statements", d[3].description.message);
  EXPECT_TRUE(d[3].description.verb.empty());
  EXPECT_EQ(Describe_status::syntax_error, status(4));
  EXPECT_EQ(Describe_status::unsupported, status(5));

  EXPECT_EQ(Describe_status::ok, status(7));
  EXPECT_EQ("x y", str(d[7].description.objects[0].name));

  ASSERT_TRUE(d[9].description.rest);
  ASSERT_TRUE(d[9].description.rest->service);
  EXPECT_EQ("/app", d[9].description.rest->service->path);
  EXPECT_FALSE(d[9].description.rest->service->written);

  EXPECT_EQ(Describe_status::syntax_error, status(10));
  EXPECT_EQ(0u, d[10].description.error_begin);  // within the item
  EXPECT_EQ(5u, d[10].description.error_end);
  EXPECT_EQ(Describe_status::empty, status(11));

  const std::string json = describe_list_json(d);
  rapidjson::Document doc;
  ASSERT_FALSE(doc.Parse(json.c_str()).HasParseError()) << json;
  ASSERT_EQ(items.size(), doc["statements"].Size());
  EXPECT_EQ(3, doc["statements"][3]["index"].GetInt());
  EXPECT_STREQ("several-statements", doc["statements"][3]["status"].GetString());
}

// An item may hold the delimiter the items are joined with.
TEST(Describe, list_item_holding_the_join_delimiter) {
  static Parser_pool pool(1);
  const auto d = describe_list({"SELECT '$$'", "DROP VIEW v"}, pool);
  ASSERT_EQ(2u, d.size());
  EXPECT_EQ(Describe_status::ok, d[0].description.status) << d[0].description.message;
  EXPECT_EQ(Describe_status::ok, d[1].description.status) << d[1].description.message;
  EXPECT_EQ("drop", d[1].description.verb);
}

// Found by review: body_end, sql_mode across items, several-statements
// under a mode.
TEST(Describe, list_item_edges) {
  static Parser_pool pool(1);
  {
    // A `;` in a trailing comment is no delimiter.
    const auto d = describe_list({"DROP VIEW v; -- done;", "DROP VIEW w -- no delimiter;"}, pool);
    EXPECT_EQ(11u, d[0].body_end);
    EXPECT_EQ(28u, d[1].body_end);
  }
  {
    // The item after a SET sql_mode is split under that mode.
    const auto d = describe_list({"SET sql_mode = 'NO_BACKSLASH_ESCAPES'", "CREATE TABLE t (a VARCHAR(5) DEFAULT 'a\\')"},
                                 pool);
    EXPECT_EQ(Describe_status::ok, d[1].description.status) << d[1].description.message;
  }
  {
    Describe_options o;
    o.sql_mode = "ANSI_QUOTES";
    const auto d = describe_list({"CREATE TABLE \"t\" (a INT); CREATE TABLE \"u\" (b INT)"}, pool, o);
    EXPECT_EQ(Describe_status::several_statements, d[0].description.status) << d[0].description.message;
  }
}

TEST(Describe, rest_only_for_rest_kinds) {
  const auto d = describe(
      "ALTER REST CONTENT SET /c ON SERVICE /s NEW REQUEST PATH /c2;\n"
      "USE REST METADATA SCHEMA mysql_rest_service_metadata;\n"
      "GRANT REST READ ON SERVICE /s TO r;\n");
  ASSERT_EQ(3u, d.size());
  ASSERT_TRUE(d[0].rest);
  EXPECT_EQ("rest_content_set", d[0].kind);
  EXPECT_EQ("/c2", d[0].rest->new_path.value_or(""));
  EXPECT_EQ("other", d[1].kind);
  EXPECT_FALSE(d[1].rest);
  EXPECT_EQ("other", d[2].kind);
  EXPECT_FALSE(d[2].rest);
}

// Every REST CREATE, ALTER and DROP rule of the grammar is one of the kinds
// describe names, or listed here as one it leaves `other` on purpose: a new
// REST statement fails this test rather than coming out as `other`.
TEST(Describe, every_rest_statement_has_a_kind) {
  const std::set<std::string> kinds = {"service",     "schema",       "view",     "procedure", "function",
                                       "content_set", "content_file", "auth_app", "user",      "role"};
  const std::set<std::string> other = {"daemon"};
  for (int s = 0; s < mdbp_grammar_symbol_count(); ++s) {
    const std::string name = mdbp_grammar_symbol_name(s);
    for (const char *verb : {"create", "alter", "drop"}) {
      const std::string prefix = std::string("mrs_") + verb + "_rest_";
      const std::string suffix = "_statement";
      if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + suffix.size() ||
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
        continue;
      const std::string kind = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
      EXPECT_TRUE(kinds.count(kind) || other.count(kind)) << name;
    }
  }
}

}  // namespace mdbls
