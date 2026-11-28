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

#include <optional>
#include <string>

#include "mdbls/outline.h"
#include "test_util.h"

namespace mdbls {

namespace {

Outline outline_of(const std::string &text) {
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  return build_outline(text, index, pool);
}

const Schema *schema(const Outline &o, std::string_view name) {
  for (const auto &s : o.catalog.schemas()) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

const Table *table(const Outline &o, std::string_view s, std::string_view t) {
  const Schema *sc = schema(o, s);
  if (sc == nullptr) return nullptr;
  for (const auto &tb : sc->tables) {
    if (tb.name == t) return &tb;
  }
  return nullptr;
}

std::vector<std::string> column_names(const Table &t) {
  std::vector<std::string> out;
  for (const auto &c : t.columns) out.push_back(c.name);
  return out;
}

}  // namespace

TEST(Catalog, use_and_qualified_names) {
  const auto o = outline_of(
      "CREATE TABLE loose (a INT);\n"
      "CREATE DATABASE shop CHARACTER SET latin1;\n"
      "USE shop;\n"
      "CREATE TABLE `order` (id INT PRIMARY KEY, `note``s` TEXT COMMENT 'it''s');\n"
      "CREATE TABLE other.t (x INT);\n");
  ASSERT_NE(nullptr, table(o, "", "loose"));
  const Schema *shop = schema(o, "shop");
  ASSERT_NE(nullptr, shop);
  EXPECT_TRUE(shop->declared);
  ASSERT_EQ(1u, shop->options.size());
  EXPECT_EQ("latin1", shop->options[0].second);
  const Table *order = table(o, "shop", "order");
  ASSERT_NE(nullptr, order);
  EXPECT_EQ((std::vector<std::string>{"id", "note`s"}), column_names(*order));
  EXPECT_EQ("it's", order->columns[1].comment);
  EXPECT_FALSE(order->columns[0].nullable);  // PRIMARY KEY implies NOT NULL
  EXPECT_NE(nullptr, table(o, "other", "t"));
  EXPECT_EQ(o.ddl_statements, o.applied);
}

TEST(Catalog, alter_table) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, c INT, KEY kb (b));\n"
      "ALTER TABLE t ADD COLUMN d DATE AFTER a, DROP COLUMN c;\n"
      "ALTER TABLE t MODIFY b BIGINT NOT NULL DEFAULT 0;\n"
      "ALTER TABLE t CHANGE a a2 VARCHAR(10);\n"
      "ALTER TABLE t RENAME COLUMN d TO d2;\n"
      "ALTER TABLE t ADD PRIMARY KEY (a2), ADD UNIQUE KEY ub (b), DROP INDEX kb;\n"
      "ALTER TABLE t ADD CONSTRAINT fk FOREIGN KEY (b) REFERENCES p (id) ON DELETE CASCADE;\n"
      "ALTER TABLE t ALTER COLUMN d2 SET DEFAULT '2026-01-01';\n"
      "ALTER TABLE t ENGINE=MyISAM, COMMENT='x';\n"
      "ALTER TABLE t RENAME TO t2;\n");
  EXPECT_EQ(nullptr, table(o, "", "t"));
  const Table *t = table(o, "", "t2");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ((std::vector<std::string>{"a2", "d2", "b"}), column_names(*t));
  EXPECT_EQ("BIGINT", t->columns[2].type);
  EXPECT_FALSE(t->columns[2].nullable);
  EXPECT_EQ("0", t->columns[2].default_value);
  EXPECT_EQ("VARCHAR(10)", t->columns[0].type);
  EXPECT_EQ("'2026-01-01'", t->columns[1].default_value);
  ASSERT_EQ(2u, t->indexes.size());
  EXPECT_EQ("primary", t->indexes[0].kind);
  EXPECT_EQ("ub", t->indexes[1].name);
  ASSERT_EQ(1u, t->foreign_keys.size());
  EXPECT_EQ("fk", t->foreign_keys[0].name);
  EXPECT_EQ("p", t->foreign_keys[0].ref_table);
  EXPECT_EQ("CASCADE", t->foreign_keys[0].on_delete);
  bool engine = false;
  for (const auto &[k, v] : t->options) engine = engine || (k == "engine" && v == "MyISAM");
  EXPECT_TRUE(engine);
}

TEST(Catalog, drop_rename_like_and_if_not_exists) {
  const auto o = outline_of(
      "CREATE TABLE a (x INT);\n"
      "CREATE TABLE IF NOT EXISTS a (y INT);\n"
      "CREATE TABLE b LIKE a;\n"
      "CREATE TABLE c (z INT);\n"
      "RENAME TABLE c TO c2, b TO s2.b;\n"
      "DROP TABLE IF EXISTS a, nope;\n"
      "CREATE INDEX ix ON c2 (z);\n"
      "CREATE SEQUENCE seq;\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "DROP VIEW v;\n");
  EXPECT_EQ(nullptr, table(o, "", "a"));
  const Table *b = table(o, "s2", "b");
  ASSERT_NE(nullptr, b);
  EXPECT_EQ((std::vector<std::string>{"x"}), column_names(*b));  // LIKE copied a
  const Table *c2 = table(o, "", "c2");
  ASSERT_NE(nullptr, c2);
  ASSERT_EQ(1u, c2->indexes.size());
  EXPECT_EQ("ix", c2->indexes[0].name);
  const Schema *dflt = schema(o, "");
  ASSERT_NE(nullptr, dflt);
  EXPECT_EQ(1u, dflt->sequences.size());
  EXPECT_TRUE(dflt->views.empty());
}

TEST(Catalog, rename_table_renames_views_and_sequences) {
  // RENAME TABLE renames whatever has the name: a table, view or sequence.
  const auto o = outline_of(
      "CREATE SEQUENCE s;\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "RENAME TABLE s TO s2, v TO w;\n");
  const Schema *dflt = schema(o, "");
  ASSERT_NE(nullptr, dflt);
  ASSERT_EQ(1u, dflt->sequences.size());
  EXPECT_EQ("s2", dflt->sequences[0].name);
  ASSERT_EQ(1u, dflt->views.size());
  EXPECT_EQ("w", dflt->views[0].name);
  // A view does not move to another schema (MariaDB error 1450), and then
  // nothing of the statement is renamed.
  const auto x = outline_of(
      "CREATE SEQUENCE s;\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "RENAME TABLE s TO s2, v TO db.w;\n");
  const Schema *d = schema(x, "");
  ASSERT_NE(nullptr, d);
  ASSERT_EQ(1u, d->sequences.size());
  EXPECT_EQ("s", d->sequences[0].name);
  ASSERT_EQ(1u, d->views.size());
  EXPECT_EQ("v", d->views[0].name);
  // A chain through a schema the statement itself creates.
  const auto y = outline_of(
      "CREATE TABLE a (x INT);\n"
      "RENAME TABLE a TO db.b, db.b TO db.c;\n");
  const Schema *db = schema(y, "db");
  ASSERT_NE(nullptr, db);
  ASSERT_EQ(1u, db->tables.size());
  EXPECT_EQ("c", db->tables[0].name);
  // A view renamed in the statement and then moved: nothing is renamed.
  const auto z = outline_of(
      "CREATE VIEW v AS SELECT 1;\n"
      "RENAME TABLE v TO w, w TO db.x;\n");
  const Schema *dz = schema(z, "");
  ASSERT_NE(nullptr, dz);
  ASSERT_EQ(1u, dz->views.size());
  EXPECT_EQ("v", dz->views[0].name);
}

TEST(Catalog, routines_triggers_events) {
  const auto o = outline_of(
      "DELIMITER //\n"
      "CREATE PROCEDURE db.p(IN a INT, OUT b VARCHAR(10), c DATE) BEGIN SELECT 1; END//\n"
      "CREATE FUNCTION f(x INT) RETURNS DECIMAL(10,2) DETERMINISTIC RETURN x//\n"
      "CREATE TRIGGER trg BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1//\n"
      "CREATE EVENT ev ON SCHEDULE EVERY 1 DAY DO SELECT 1//\n"
      "DELIMITER ;\n"
      "DROP FUNCTION f;\n");
  const Schema *db = schema(o, "db");
  ASSERT_NE(nullptr, db);
  ASSERT_EQ(1u, db->routines.size());
  const Routine &p = db->routines[0];
  EXPECT_EQ("procedure", p.kind);
  ASSERT_EQ(3u, p.parameters.size());
  EXPECT_EQ("OUT", p.parameters[1].mode);
  EXPECT_EQ("b", p.parameters[1].name);
  EXPECT_EQ("VARCHAR(10)", p.parameters[1].type);
  EXPECT_EQ("", p.parameters[2].mode);
  const Schema *dflt = schema(o, "");
  ASSERT_NE(nullptr, dflt);
  EXPECT_TRUE(dflt->routines.empty());  // f was dropped
  ASSERT_EQ(1u, dflt->triggers.size());
  EXPECT_EQ("BEFORE", dflt->triggers[0].timing);
  EXPECT_EQ("INSERT", dflt->triggers[0].events);
  EXPECT_EQ("t", dflt->triggers[0].table);
  EXPECT_EQ(1u, dflt->events.size());
}

TEST(Catalog, rest_objects) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "USE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /sch FROM sakila;\n"
      "CREATE REST VIEW /actor ON SCHEMA /sch AS sakila.actor;\n"
      "CREATE REST PROCEDURE /p ON SERVICE /svc SCHEMA /sch AS sakila.film_in_stock;\n"
      "CREATE REST CONTENT SET /cs;\n"
      "CREATE REST AUTH APP 'app' VENDOR MariaDB;\n"
      "CREATE REST ROLE 'admin';\n"
      "CREATE REST USER 'jane'@'app';\n"
      "DROP REST VIEW /actor;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  EXPECT_EQ("/svc", svcs[0].name.path);
  ASSERT_EQ(1u, svcs[0].schemas.size());
  EXPECT_EQ("/sch", svcs[0].schemas[0].path);
  EXPECT_EQ("sakila", svcs[0].schemas[0].schema);
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());
  EXPECT_EQ("procedure", svcs[0].schemas[0].objects[0].kind);
  EXPECT_EQ("sakila.film_in_stock", svcs[0].schemas[0].objects[0].object);
  EXPECT_EQ(1u, svcs[0].content_sets.size());
  ASSERT_EQ(1u, o.catalog.rest_auth_apps().size());
  EXPECT_EQ("app", o.catalog.rest_auth_apps()[0].name);
  ASSERT_EQ(1u, o.catalog.rest_users().size());
  EXPECT_EQ("jane@app", o.catalog.rest_users()[0].name);
}

// DROP REST takes the object's kind, its FROM selector and the current
// service and schema into account; it never creates a service.
TEST(Catalog, rest_drop_is_scoped) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "CREATE REST SERVICE /b;\n"
      "CREATE REST SCHEMA /s ON SERVICE /a FROM sakila;\n"
      "CREATE REST SCHEMA /s ON SERVICE /b FROM sakila;\n"
      "CREATE REST VIEW /x ON SERVICE /a SCHEMA /s AS sakila.actor;\n"
      "CREATE REST PROCEDURE /y ON SERVICE /a SCHEMA /s AS sakila.film_in_stock;\n"
      "CREATE REST VIEW /x ON SERVICE /b SCHEMA /s AS sakila.actor;\n"
      "CREATE REST CONTENT SET /cs ON SERVICE /a;\n"
      "CREATE REST CONTENT SET /cs ON SERVICE /b;\n"
      "USE REST SERVICE /a SCHEMA /S;\n"
      "DROP REST VIEW /x;\n"
      "DROP REST CONTENT SET /cs;\n"
      "DROP REST PROCEDURE /x FROM SERVICE /nowhere DATABASE /s;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(2u, svcs.size());  // /nowhere was not created by the DROP
  const auto &a = svcs[0], &b = svcs[1];
  ASSERT_EQ(1u, a.schemas[0].objects.size());
  EXPECT_EQ("procedure", a.schemas[0].objects[0].kind);  // the view went, not the procedure
  EXPECT_TRUE(a.content_sets.empty());
  ASSERT_EQ(1u, b.schemas[0].objects.size());  // service /b was not searched
  EXPECT_EQ("view", b.schemas[0].objects[0].kind);
  EXPECT_EQ(1u, b.content_sets.size());
}

// A service keeps the auth apps linked to it: ADD AUTH APP links, REMOVE
// AUTH APP unlinks (names compare without regard to case), CREATE OR
// REPLACE keeps the links it does not name, as MariaDB Shell does.
TEST(Catalog, rest_auth_app_links) {
  const auto o = outline_of(
      "CREATE REST AUTH APP 'MRS' VENDOR MRS;\n"
      "CREATE REST AUTH APP `Other` VENDOR MRS;\n"
      "CREATE REST AUTH APP gone VENDOR MRS;\n"
      "CREATE REST SERVICE mike@/notes ADD AUTH APP 'MRS' ADD AUTH APP `Other` ADD AUTH APP gone;\n"
      "CREATE REST SERVICE /b ADD AUTH APP 'gone';\n"
      "DROP REST AUTH APP gone;\n"
      "ALTER REST SERVICE mike@/notes REMOVE AUTH APP 'mrs' IF EXISTS;\n"
      "CREATE OR REPLACE REST SERVICE mike@/NOTES ADD AUTH APP 'Third';\n"
      "CREATE REST SERVICE IF NOT EXISTS mike@/notes REMOVE AUTH APP 'Third';\n"
      "ALTER REST SERVICE mike@/notes NEW REQUEST PATH /memo ADD AUTH APP 'MRS';\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(2u, svcs.size());
  EXPECT_EQ("/memo", svcs[0].name.path);
  std::vector<std::string> names;
  for (const auto &a : svcs[0].auth_apps) names.push_back(a.name);
  EXPECT_EQ((std::vector<std::string>{"Other", "Third", "MRS"}), names);
  EXPECT_TRUE(svcs[1].auth_apps.empty());  // DROP REST AUTH APP unlinks it
  EXPECT_EQ(10u, o.applied);
  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos, json.find("\"content_sets\":[],\"auth_apps\":[{\"name\":\"Other\",")) << json;
}

// A REST statement that names no service or schema takes the ones in use,
// as MariaDB Shell does: USE REST SERVICE sets the service and clears the
// schema, USE REST SCHEMA sets the schema, the first schema created on the
// current service becomes current.  Where the shell fails, a note.
TEST(Catalog, rest_current_service_and_schema) {
  const auto o = outline_of(
      "CREATE REST SERVICE /notes;\n"
      "CREATE REST VIEW /v1 AS sakila.actor;\n"                    // no service in use
      "CREATE REST SCHEMA /early FROM sakila;\n"                   // no service in use
      "USE REST SCHEMA /app;\n"                                    // no service in use
      "ALTER REST SERVICE /elsewhere ADD AUTH APP 'MRS';\n"        // not created
      "USE REST SERVICE /notes;\n"
      "CREATE REST SCHEMA FROM sakila;\n"                          // /sakila, now current
      "CREATE REST VIEW /v2 AS sakila.actor;\n"
      "CREATE REST SCHEMA /app FROM world;\n"
      "USE REST SCHEMA /APP;\n"
      "CREATE REST FUNCTION /f AS world.f;\n"
      "USE REST SERVICE /notes;\n"                                 // clears the schema
      "CREATE REST PROCEDURE /p AS world.p;\n"                     // no schema in use
      "CREATE REST PROCEDURE /p ON SCHEMA /app AS world.p;\n"
      "DROP REST SCHEMA /x;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  EXPECT_EQ("/notes", svcs[0].name.path);
  ASSERT_EQ(2u, svcs[0].schemas.size());
  EXPECT_EQ("/sakila", svcs[0].schemas[0].path);
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());
  EXPECT_EQ("/v2", svcs[0].schemas[0].objects[0].path);
  EXPECT_EQ("/app", svcs[0].schemas[1].path);
  ASSERT_EQ(2u, svcs[0].schemas[1].objects.size());
  EXPECT_EQ("/f", svcs[0].schemas[1].objects[0].path);
  EXPECT_EQ("/p", svcs[0].schemas[1].objects[1].path);
  ASSERT_EQ(5u, o.notes.size());
  for (const auto &n : o.notes) EXPECT_FALSE(n.applied);
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[0].kind);
  EXPECT_EQ("CREATE REST VIEW /v1: names no REST SERVICE, and none is in use (USE REST SERVICE)",
            o.notes[0].message);
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[1].kind);
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[2].kind);
  EXPECT_EQ(Note_kind::unknown_rest_service, o.notes[3].kind);
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[4].kind);
  EXPECT_NE(std::string::npos, o.notes[4].message.find("names no REST SCHEMA")) << o.notes[4].message;
}

// Catalog::rest_context between statements (what describe reads): each
// row is a statement and the service key and schema path in use after it.
TEST(Catalog, rest_context_after_each_statement) {
  struct Step {
    const char *sql;
    const char *service;
    const char *schema;
  };
  const Step steps[] = {
      {"CREATE REST SERVICE /notes;\n", "", ""},                 // creating does not make it current
      {"USE REST SERVICE /notes;\n", "/notes", ""},
      {"CREATE REST SCHEMA /a FROM sakila;\n", "/notes", "/a"},   // the service's first schema
      {"CREATE REST SCHEMA /b FROM world;\n", "/notes", "/a"},    // not its first
      {"ALTER REST SCHEMA /a NEW REQUEST PATH /a2;\n", "/notes", "/a2"},
      {"USE REST SCHEMA /b;\n", "/notes", "/b"},
      {"ALTER REST SERVICE /notes NEW REQUEST PATH /n2;\n", "/n2", "/b"},
      {"DROP REST SCHEMA /b;\n", "/n2", ""},
      {"CREATE REST SERVICE mike, anna@/svc;\n", "/n2", ""},
      {"USE REST SERVICE anna,mike@/svc SCHEMA /s;\n", "anna,mike@/svc", "/s"},
      {"USE REST SERVICE /n2;\n", "/n2", ""},                    // clears the schema
      {"DROP REST SERVICE /n2;\n", "", ""},
  };
  std::string text;
  for (const Step &s : steps) {
    text += s.sql;
    const auto o = outline_of(text);
    EXPECT_EQ(s.service, o.catalog.rest_context().service.key) << s.sql;
    EXPECT_EQ(s.schema, o.catalog.rest_context().schema) << s.sql;
  }
}

// The current service follows an ALTER REST SERVICE ... NEW REQUEST PATH
// (its schema stays current); DROP REST SCHEMA / SERVICE of the current one
// clears it, as MariaDB Shell does.
TEST(Catalog, rest_current_follows_rename_and_drop) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "USE REST SERVICE /a;\n"
      "CREATE REST SCHEMA /s FROM sakila;\n"                       // current
      "ALTER REST SERVICE /a NEW REQUEST PATH /b;\n"
      "CREATE REST VIEW /v1 AS sakila.actor;\n"                    // /b, /s
      "DROP REST SCHEMA /s;\n"
      "CREATE REST VIEW /v2 AS sakila.actor;\n"                    // no schema in use
      "DROP REST SERVICE /b;\n"
      "CREATE REST CONTENT SET /cs;\n");                           // no service in use
  EXPECT_TRUE(o.catalog.rest_services().empty());
  ASSERT_EQ(2u, o.notes.size());
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[0].kind);
  EXPECT_NE(std::string::npos, o.notes[0].message.find("names no REST SCHEMA")) << o.notes[0].message;
  EXPECT_EQ(Note_kind::rest_none_in_use, o.notes[1].kind);
  EXPECT_NE(std::string::npos, o.notes[1].message.find("names no REST SERVICE")) << o.notes[1].message;
}

TEST(Catalog, rest_current_schema_survives_rename) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "USE REST SERVICE /a;\n"
      "CREATE REST SCHEMA /s FROM sakila;\n"
      "ALTER REST SERVICE /a NEW REQUEST PATH /b;\n"
      "CREATE REST VIEW /v AS sakila.actor;\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  EXPECT_EQ("/b", svcs[0].name.path);
  ASSERT_EQ(1u, svcs[0].schemas.size());
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());
  EXPECT_EQ("/v", svcs[0].schemas[0].objects[0].path);
}

// What `mariadb-lsp outline --features` prints: every feature, in the
// format msm reads.
TEST(Catalog, outline_features_json) {
  std::string expected = "{\"format\":1,\"features\":[";
  bool first = true;
  for (const char *f : outline_features()) {
    expected += (first ? "\"" : ",\"") + std::string(f) + "\"";
    first = false;
  }
  expected += "]}";
  EXPECT_EQ(expected, outline_features_json());
  EXPECT_NE(std::string::npos, expected.find("\"rest_declared\""));
}

// CREATE REST SCHEMA IF NOT EXISTS keeps a schema the file has; DROP REST
// CONTENT SET matches the unquoted path; a CREATE of a new service ignores
// REMOVE AUTH APP, as MariaDB Shell does.
TEST(Catalog, rest_if_not_exists_and_quoted_paths) {
  const auto o = outline_of(
      "CREATE REST SERVICE /n ADD AUTH APP a REMOVE AUTH APP a;\n"
      "CREATE REST SCHEMA /s ON SERVICE /n FROM sakila;\n"
      "CREATE REST SCHEMA IF NOT EXISTS /s ON SERVICE /n FROM world;\n"
      "CREATE REST CONTENT SET '/cs' ON SERVICE /n;\n"
      "DROP REST CONTENT SET '/cs' FROM SERVICE /n;\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  ASSERT_EQ(1u, svcs[0].schemas.size());
  EXPECT_EQ("sakila", svcs[0].schemas[0].schema);
  EXPECT_TRUE(svcs[0].content_sets.empty());
  ASSERT_EQ(1u, svcs[0].auth_apps.size());
  EXPECT_EQ("a", svcs[0].auth_apps[0].name);
}

// `declared`: the file has a CREATE REST SERVICE / SCHEMA for it, not just a
// statement naming it (msm never drops a service a release only references).
TEST(Catalog, rest_declared) {
  const auto o = outline_of(
      "CREATE REST SCHEMA /q ON SERVICE /shared FROM a;\n"
      "CREATE REST VIEW /v ON SERVICE /shared SCHEMA /implicit AS a.t;\n"
      "CREATE REST SERVICE /own;\n"
      "CREATE REST VIEW /v ON SERVICE /own SCHEMA /later AS a.t;\n"
      "CREATE REST SCHEMA IF NOT EXISTS /later ON SERVICE /own FROM a;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(2u, svcs.size());
  EXPECT_FALSE(svcs[0].declared);
  ASSERT_EQ(2u, svcs[0].schemas.size());
  EXPECT_TRUE(svcs[0].schemas[0].declared);
  EXPECT_FALSE(svcs[0].schemas[1].declared);
  EXPECT_TRUE(svcs[1].declared);
  ASSERT_EQ(1u, svcs[1].schemas.size());
  EXPECT_TRUE(svcs[1].schemas[0].declared);  // IF NOT EXISTS of a schema only named creates it
  EXPECT_EQ("a", svcs[1].schemas[0].schema);
  EXPECT_EQ(1u, svcs[1].schemas[0].objects.size());
  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos, json.find("{\"path\":\"/shared\",\"declared\":false,")) << json;
  EXPECT_NE(std::string::npos, json.find("{\"path\":\"/q\",\"schema\":\"a\",\"declared\":true,")) << json;
}

// DROP REST SCHEMA ... FOR DATABASE db drops the REST schema only when it
// exposes database schema db (msm's drop of a moved REST schema).
TEST(Catalog, rest_drop_for_database) {
  const auto o = outline_of(
      "CREATE REST SERVICE /s;\n"
      "CREATE REST SCHEMA /app ON SERVICE /s FROM b;\n"
      "CREATE REST SCHEMA /old ON SERVICE /s FROM `A`;\n"
      "DROP REST SCHEMA IF EXISTS /app FROM SERVICE /s FOR DATABASE a;\n"
      "DROP REST DATABASE IF EXISTS /old FROM SERVICE /s FOR SCHEMA a;\n"
      "DROP REST SCHEMA IF EXISTS /none FROM SERVICE /s FOR DATABASE a;\n"
      "DROP REST SCHEMA IF EXISTS /app FROM /s FOR DATABASE c;\n");  // the shell's grammar_test form
  EXPECT_TRUE(o.notes.empty());
  EXPECT_EQ(7u, o.applied);
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  ASSERT_EQ(1u, svcs[0].schemas.size());  // /app is on b: kept; /old on `A`: dropped
  EXPECT_EQ("/app", svcs[0].schemas[0].path);
  EXPECT_EQ("b", svcs[0].schemas[0].schema);
}

// A REST schema only named (no database known) is dropped as without FOR
// DATABASE; a FOR DATABASE that names another database keeps the current one.
TEST(Catalog, rest_drop_for_database_unknown_and_current) {
  const auto o = outline_of(
      "CREATE REST SERVICE /s;\n"
      "USE REST SERVICE /s;\n"
      "CREATE REST VIEW /v ON SCHEMA /p AS a.t;\n"
      "DROP REST SCHEMA /p FOR DATABASE a;\n"
      "CREATE REST SCHEMA /q FROM b;\n"
      "DROP REST SCHEMA /q FOR DATABASE c;\n"
      "CREATE REST VIEW /w AS b.t;\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  ASSERT_EQ(1u, svcs[0].schemas.size());
  EXPECT_EQ("/q", svcs[0].schemas[0].path);
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());  // /q stayed current
  EXPECT_EQ("/w", svcs[0].schemas[0].objects[0].path);
}

TEST(Catalog, named_key_replaces_its_namesake) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a), KEY k (b), UNIQUE (a), UNIQUE (b));\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(3u, t->indexes.size());  // k once, the two unnamed keys both kept
  EXPECT_EQ("k", t->indexes[0].name);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[0].columns());
}

// Every note has a kind and says whether its statement was applied
// nonetheless; msm_plugin fails a release on a note that was not.
TEST(Catalog, notes_for_what_cannot_be_applied) {
  const auto o = outline_of(
      "ALTER TABLE missing ADD COLUMN x INT;\n"
      "CREATE TABLE broken (a INT,);\n"
      "CREATE INDEX i ON missing (a);\n"
      "CREATE TABLE copy LIKE missing;\n"
      "SET sql_mode = ORACLE;\n"
      "CREATE TABLE o (a INT);\n");
  ASSERT_EQ(5u, o.notes.size());
  EXPECT_EQ(Note_kind::unknown_table, o.notes[0].kind);
  EXPECT_FALSE(o.notes[0].applied);
  EXPECT_NE(std::string::npos, o.notes[0].message.find("does not create this table"));
  EXPECT_EQ(Note_kind::syntax_error, o.notes[1].kind);
  EXPECT_FALSE(o.notes[1].applied);
  EXPECT_NE(std::string::npos, o.notes[1].message.find("syntax error"));
  EXPECT_EQ(Note_kind::unknown_table, o.notes[2].kind);
  EXPECT_FALSE(o.notes[2].applied);
  EXPECT_EQ(Note_kind::unknown_table, o.notes[3].kind);
  EXPECT_TRUE(o.notes[3].applied);  // the LIKE creates the table
  EXPECT_NE(nullptr, table(o, "", "copy"));
  EXPECT_EQ(Note_kind::unsupported_sql_mode, o.notes[4].kind);
  EXPECT_FALSE(o.notes[4].applied);
  EXPECT_EQ("not read: sql_mode ORACLE is not supported", o.notes[4].message);
  EXPECT_EQ(nullptr, table(o, "", "o"));
  for (const Note &n : o.notes) EXPECT_EQ(0u, n.source);
  EXPECT_EQ(5u, o.ddl_statements);
  EXPECT_EQ(1u, o.applied);  // the LIKE

  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos,
            json.find("{\"kind\":\"unknown-table\",\"applied\":false,\"start\":{\"line\":0,\"character\":0},"
                      "\"message\":\"ALTER TABLE missing: the script does not create this table\"}"))
      << json;
  EXPECT_NE(std::string::npos, json.find("{\"kind\":\"unknown-table\",\"applied\":true,")) << json;
}

// docs/outline-format.md lists every kind of note the server can write.
TEST(Catalog, every_note_kind_is_documented) {
  const std::string doc = test::read_fixture("../../../docs/outline-format.md");
  ASSERT_FALSE(doc.empty());
  const size_t section = doc.find("\n## Notes\n");
  ASSERT_NE(std::string::npos, section);
  const size_t end = doc.find("\n## ", section + 1);
  std::string notes = doc.substr(section, end == std::string::npos ? end : end - section);
  const size_t kinds = notes.find("\n| Kind |");  // the table of kinds, after the members' table
  ASSERT_NE(std::string::npos, kinds);
  notes = notes.substr(kinds);
  size_t rows = 0;
  for (size_t at = notes.find("\n| `"); at != std::string::npos; at = notes.find("\n| `", at + 1)) ++rows;
  EXPECT_EQ(std::size(k_note_kinds), rows);
  for (const Note_kind kind : k_note_kinds) {
    EXPECT_NE(std::string::npos, notes.find("\n| `" + std::string(to_string(kind)) + "` |")) << to_string(kind);
  }
}

// Column attributes the outline keeps (they make the schema snapshot of an
// MSM project).  CHARACTER SET and COLLATE are not part of the type.
TEST(Catalog, column_attributes) {
  const auto o = outline_of(
      "CREATE TABLE t (\n"
      "  a VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,\n"
      "  ts TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6) INVISIBLE,\n"
      "  g INT AS (a + 1) PERSISTENT, g2 INT GENERATED ALWAYS AS (2) STORED, g3 INT AS (3),\n"
      "  v INT WITHOUT SYSTEM VERSIONING, w INT WITH SYSTEM VERSIONING,\n"
      "  b CHAR(3) BINARY, x ENUM('a','b') CHARSET latin1, y TEXT ASCII, z CHAR(2) UNICODE,\n"
      "  u INT UNSIGNED ZEROFILL, e INT ENCRYPTED=YES,\n"
      "  rs TIMESTAMP(6) AS ROW START, re TIMESTAMP(6) AS ROW END\n"
      ");\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(15u, t->columns.size());
  const auto &c = t->columns;
  EXPECT_EQ("VARCHAR(255)", c[0].type);
  EXPECT_EQ("utf8mb4", c[0].charset);
  EXPECT_EQ("utf8mb4_bin", c[0].collation);
  EXPECT_EQ("CURRENT_TIMESTAMP(6)", c[1].on_update);
  EXPECT_TRUE(c[1].invisible);
  EXPECT_FALSE(c[0].invisible);
  EXPECT_EQ(std::nullopt, c[0].on_update);
  EXPECT_EQ("PERSISTENT", c[2].generated_storage);
  EXPECT_EQ("STORED", c[3].generated_storage);
  EXPECT_EQ(std::nullopt, c[4].generated_storage);  // not written
  EXPECT_EQ("(3)", c[4].generated);
  EXPECT_EQ(std::nullopt, c[0].generated_storage);
  EXPECT_EQ(false, c[5].system_versioning);
  EXPECT_EQ(true, c[6].system_versioning);
  EXPECT_EQ(std::nullopt, c[0].system_versioning);
  EXPECT_EQ("CHAR(3) BINARY", c[7].type);
  EXPECT_EQ(std::nullopt, c[7].charset);
  EXPECT_EQ("ENUM('a','b')", c[8].type);
  EXPECT_EQ("latin1", c[8].charset);
  EXPECT_EQ("TEXT", c[9].type);
  EXPECT_EQ("latin1", c[9].charset);  // ASCII
  EXPECT_EQ("CHAR(2)", c[10].type);
  EXPECT_EQ("ucs2", c[10].charset);  // UNICODE
  EXPECT_EQ("INT UNSIGNED ZEROFILL", c[11].type);
  ASSERT_EQ(1u, c[12].options.size());
  EXPECT_EQ("encrypted", c[12].options[0].first);
  EXPECT_EQ("YES", c[12].options[0].second);
  EXPECT_EQ("START", c[13].generated_row);
  EXPECT_EQ("END", c[14].generated_row);
  EXPECT_EQ(std::nullopt, c[13].generated);
  // Completion shows attributes(), hover definition(): both keep CHARACTER SET and COLLATE.
  EXPECT_EQ("VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL", c[0].attributes());
  EXPECT_EQ("TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6) INVISIBLE",
            c[1].definition());
  EXPECT_EQ("INT AS (a + 1) PERSISTENT", c[2].definition());
}

TEST(Catalog, quoted_collation_reads_alike_everywhere) {
  const auto o = outline_of("CREATE TABLE t (a CHAR(1) COLLATE 'utf8mb4_bin', b CHAR(1) NOT NULL COLLATE 'utf8mb4_bin');\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(2u, t->columns.size());
  EXPECT_EQ("utf8mb4_bin", t->columns[0].collation);
  EXPECT_EQ("utf8mb4_bin", t->columns[1].collation);
}

TEST(Catalog, column_check_is_a_table_check) {
  const auto o = outline_of(
      "CREATE TABLE t (j JSON CHECK (JSON_VALID(j)), k INT, CONSTRAINT ck CHECK (k > 0));\n"
      "ALTER TABLE t MODIFY k INT CHECK (k < 9);\n"
      "ALTER TABLE t ADD CONSTRAINT IF NOT EXISTS ck2 CHECK (k <> 5);\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(4u, t->checks.size());
  EXPECT_EQ("", t->checks[0].name);
  EXPECT_EQ("j", t->checks[0].column);
  EXPECT_EQ("JSON_VALID(j)", t->checks[0].expression);
  EXPECT_EQ("ck", t->checks[1].name);
  EXPECT_EQ("", t->checks[1].column);
  EXPECT_EQ("k", t->checks[2].column);
  EXPECT_EQ("k < 9", t->checks[2].expression);
  EXPECT_EQ("ck2", t->checks[3].name);

  // MODIFY replaces the column's own check; DROP COLUMN drops it.
  const auto o2 = outline_of(
      "CREATE TABLE t (j JSON CHECK (JSON_VALID(j)), k INT);\n"
      "ALTER TABLE t MODIFY j JSON;\n"
      "ALTER TABLE t MODIFY k INT CHECK (k > 1);\n"
      "ALTER TABLE t DROP COLUMN k;\n");
  const Table *t2 = table(o2, "", "t");
  ASSERT_NE(nullptr, t2);
  EXPECT_TRUE(t2->checks.empty());
}

// DROP COLUMN takes the column out of its indexes; an index left without
// key parts goes with it, as on the server.
TEST(Catalog, drop_column_leaves_its_indexes) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a, b), KEY ka (a), UNIQUE u (b));\n"
      "ALTER TABLE t DROP COLUMN a;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(2u, t->indexes.size());
  EXPECT_EQ("k", t->indexes[0].name);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[0].columns());
  EXPECT_EQ("u", t->indexes[1].name);
}

TEST(Catalog, index_details) {
  const auto o = outline_of(
      "CREATE TABLE t (a VARCHAR(20), b INT, g GEOMETRY NOT NULL, s DATE, e DATE, txt TEXT,\n"
      "  PERIOD FOR p (s, e),\n"
      "  KEY k1 (a(10) DESC, b ASC) USING HASH COMMENT 'it''s' IGNORED,\n"
      "  KEY k2 USING BTREE (b) KEY_BLOCK_SIZE=8 NOT IGNORED,\n"
      "  SPATIAL KEY sg (g),\n"
      "  UNIQUE KEY u (b, p WITHOUT OVERLAPS),\n"
      "  FULLTEXT KEY ft (txt) WITH PARSER ngram);\n"
      "ALTER TABLE t ADD INDEX ai TYPE RTREE (b), ALTER INDEX k2 IGNORED;\n"
      "CREATE UNIQUE INDEX ci USING HASH ON t (a(4) DESC) COMMENT 'c' IGNORED;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(7u, t->indexes.size());
  const Index &k1 = t->indexes[0];
  EXPECT_EQ((std::vector<std::string>{"a", "b"}), k1.columns());
  ASSERT_EQ(2u, k1.key_parts.size());
  EXPECT_EQ("a", k1.key_parts[0].column);
  EXPECT_EQ(10u, k1.key_parts[0].length);
  EXPECT_EQ("DESC", k1.key_parts[0].order);
  EXPECT_EQ(std::nullopt, k1.key_parts[1].length);
  EXPECT_EQ("ASC", k1.key_parts[1].order);
  EXPECT_EQ(std::nullopt, k1.key_parts[0].expression);
  EXPECT_EQ("HASH", k1.using_);
  EXPECT_EQ("it's", k1.comment);
  EXPECT_TRUE(k1.ignored);
  const Index &k2 = t->indexes[1];
  EXPECT_EQ("BTREE", k2.using_);
  EXPECT_TRUE(k2.ignored);  // ALTER INDEX k2 IGNORED
  ASSERT_EQ(1u, k2.options.size());
  EXPECT_EQ("key_block_size", k2.options[0].first);
  EXPECT_EQ("8", k2.options[0].second);
  const Index &sg = t->indexes[2];
  EXPECT_EQ("spatial", sg.kind);
  EXPECT_EQ(std::vector<std::string>{"g"}, sg.columns());
  ASSERT_EQ(1u, sg.key_parts.size());
  EXPECT_EQ("p", t->indexes[3].without_overlaps);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[3].columns());
  ASSERT_EQ(1u, t->indexes[4].options.size());
  EXPECT_EQ("parser", t->indexes[4].options[0].first);
  EXPECT_EQ("ngram", t->indexes[4].options[0].second);
  EXPECT_EQ("RTREE", t->indexes[5].using_);
  EXPECT_FALSE(t->indexes[5].ignored);
  const Index &ci = t->indexes[6];
  EXPECT_EQ("ci", ci.name);
  EXPECT_EQ("unique", ci.kind);
  EXPECT_EQ("HASH", ci.using_);
  EXPECT_EQ("c", ci.comment);
  EXPECT_TRUE(ci.ignored);
  ASSERT_EQ(1u, ci.key_parts.size());
  EXPECT_EQ(4u, ci.key_parts[0].length);
  EXPECT_EQ("DESC", ci.key_parts[0].order);
}

TEST(Catalog, foreign_key_names_and_match) {
  const auto o = outline_of(
      "CREATE TABLE c (pid INT, pid2 INT, pid3 INT REFERENCES p (id) MATCH FULL,\n"
      "  CONSTRAINT fk_c1 FOREIGN KEY (pid) REFERENCES p (id) MATCH SIMPLE,\n"
      "  CONSTRAINT fk_c2 FOREIGN KEY fkidx (pid2) REFERENCES p (id2),\n"
      "  FOREIGN KEY fk3 (pid) REFERENCES p (id));\n"
      "ALTER TABLE c ADD CONSTRAINT fk4 FOREIGN KEY ix4 (pid2) REFERENCES p (id2) MATCH PARTIAL;\n"
      "ALTER TABLE c DROP FOREIGN KEY fk_c2;\n");
  const Table *c = table(o, "", "c");
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(4u, c->foreign_keys.size());
  const auto &f = c->foreign_keys;
  EXPECT_EQ("", f[0].name);
  EXPECT_EQ("FULL", f[0].match);
  EXPECT_EQ("fk_c1", f[1].name);
  EXPECT_EQ("", f[1].index_name);
  EXPECT_EQ("SIMPLE", f[1].match);
  EXPECT_EQ("fk3", f[2].name);  // fk_c2 (index fkidx) was dropped by its constraint name
  EXPECT_EQ("fk3", f[2].index_name);
  EXPECT_EQ("", f[2].match);
  EXPECT_EQ("fk4", f[3].name);
  EXPECT_EQ("ix4", f[3].index_name);
  EXPECT_EQ("PARTIAL", f[3].match);
}

TEST(Catalog, table_partitioning_versioning_periods) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, s DATE, e DATE, rs TIMESTAMP(6) AS ROW START,\n"
      "  re TIMESTAMP(6) AS ROW END, PERIOD FOR SYSTEM_TIME (rs, re), PERIOD FOR app (s, e))\n"
      "  ENGINE=InnoDB WITH SYSTEM VERSIONING PAGE_COMPRESSED=1 UNION=(x, y)\n"
      "  PARTITION BY HASH (a)\n  PARTITIONS 4;\n"
      "CREATE TABLE u (a INT) PARTITION BY RANGE (a) (PARTITION p0 VALUES LESS THAN (10));\n"
      "ALTER TABLE u REMOVE PARTITIONING;\n"
      "CREATE TABLE w (a INT);\n"
      "ALTER TABLE w ADD SYSTEM VERSIONING, ADD COLUMN b INT PARTITION BY KEY (a) PARTITIONS 2;\n"
      "ALTER TABLE t DROP PERIOD FOR app;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ("PARTITION BY HASH (a) PARTITIONS 4", t->partitioning);
  EXPECT_TRUE(t->system_versioning);
  ASSERT_EQ(1u, t->periods.size());
  EXPECT_EQ("SYSTEM_TIME", t->periods[0].name);
  EXPECT_EQ("rs", t->periods[0].start);
  EXPECT_EQ("re", t->periods[0].end);
  auto option = [](const Table &x, const std::string &k) {
    for (const auto &[key, v] : x.options)
      if (key == k) return v;
    return std::string("<none>");
  };
  EXPECT_EQ("1", option(*t, "page_compressed"));
  EXPECT_EQ("(x, y)", option(*t, "union"));
  const Table *u = table(o, "", "u");
  ASSERT_NE(nullptr, u);
  EXPECT_EQ("", u->partitioning);
  EXPECT_FALSE(u->system_versioning);
  const Table *w = table(o, "", "w");
  ASSERT_NE(nullptr, w);
  EXPECT_TRUE(w->system_versioning);
  EXPECT_EQ("PARTITION BY KEY (a) PARTITIONS 2", w->partitioning);
}

TEST(Catalog, alter_keeps_column_members) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a), CHECK (a > 0));\n"
      "ALTER TABLE t MODIFY a VARCHAR(5) CHARACTER SET latin1 COLLATE latin1_bin NOT NULL\n"
      "  INVISIBLE COMMENT 'x';\n"
      "ALTER TABLE t CHANGE b b2 TIMESTAMP ON UPDATE CURRENT_TIMESTAMP WITHOUT SYSTEM VERSIONING;\n"
      "ALTER TABLE t ADD COLUMN g INT AS (1) VIRTUAL INVISIBLE;\n"
      "ALTER TABLE t ALTER COLUMN a SET DEFAULT 'q';\n"
      "ALTER TABLE t RENAME COLUMN a TO a2;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(3u, t->columns.size());
  const auto &a = t->columns[0];
  EXPECT_EQ("a2", a.name);
  EXPECT_EQ("VARCHAR(5)", a.type);
  EXPECT_EQ("latin1", a.charset);
  EXPECT_EQ("latin1_bin", a.collation);
  EXPECT_TRUE(a.invisible);
  EXPECT_EQ("'q'", a.default_value);
  const auto &b = t->columns[1];
  EXPECT_EQ("b2", b.name);
  EXPECT_EQ("CURRENT_TIMESTAMP", b.on_update);
  EXPECT_EQ(false, b.system_versioning);
  EXPECT_EQ(std::nullopt, b.charset);
  const auto &g = t->columns[2];
  EXPECT_EQ("VIRTUAL", g.generated_storage);
  EXPECT_TRUE(g.invisible);
  ASSERT_EQ(1u, t->indexes.size());
  EXPECT_EQ(std::vector<std::string>{"a2"}, t->indexes[0].columns());  // follows the rename
  EXPECT_EQ("a2", t->indexes[0].key_parts[0].column);
}

TEST(Catalog, view_routine_trigger_event_sequence_details) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT);\n"
      "CREATE OR REPLACE ALGORITHM=MERGE DEFINER=`root`@`localhost` SQL SECURITY INVOKER\n"
      "  VIEW v AS SELECT a FROM t WITH LOCAL CHECK OPTION;\n"
      "CREATE VIEW v2 AS SELECT a FROM t WITH CHECK OPTION;\n"
      "CREATE VIEW v3 AS SELECT 1;\n"
      "DELIMITER //\n"
      "CREATE DEFINER=root@'%' FUNCTION f(x INT) RETURNS INT DETERMINISTIC READS SQL DATA\n"
      "  SQL SECURITY INVOKER COMMENT 'it''s' RETURN x + 1//\n"
      "CREATE PROCEDURE p() NOT DETERMINISTIC MODIFIES SQL DATA\n"
      "BEGIN\n  SELECT   1;  -- kept\nEND//\n"
      "CREATE TRIGGER tr1 BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1//\n"
      "CREATE DEFINER=CURRENT_USER TRIGGER tr2 BEFORE UPDATE ON t FOR EACH ROW PRECEDES tr1\n"
      "BEGIN SET NEW.b = 2; END//\n"
      "CREATE EVENT e1 ON SCHEDULE EVERY 1 HOUR STARTS '2030-01-01' ON COMPLETION NOT PRESERVE\n"
      "  DISABLE ON SLAVE COMMENT 'ev' DO DELETE FROM t//\n"
      "CREATE EVENT e2 ON SCHEDULE AT '2030-01-01 00:00:00' DO BEGIN SELECT 1; END//\n"
      "DELIMITER ;\n"
      "ALTER PROCEDURE p COMMENT 'changed' SQL SECURITY INVOKER;\n"
      "ALTER EVENT e2 ON COMPLETION PRESERVE ENABLE;\n"
      "CREATE SEQUENCE s1 START WITH 10 INCREMENT BY -2 MINVALUE=1 MAXVALUE 100 CACHE 5 CYCLE;\n"
      "CREATE SEQUENCE s2 NOCACHE NOCYCLE;\n"
      "ALTER SEQUENCE s2 INCREMENT 3;\n");
  const Schema *sc = schema(o, "");
  ASSERT_NE(nullptr, sc);
  ASSERT_EQ(3u, sc->views.size());
  EXPECT_EQ("MERGE", sc->views[0].algorithm);
  EXPECT_EQ("INVOKER", sc->views[0].security);
  EXPECT_EQ("root@localhost", sc->views[0].definer);
  EXPECT_EQ("LOCAL", sc->views[0].check_option);
  EXPECT_EQ("CASCADED", sc->views[1].check_option);
  EXPECT_EQ("", sc->views[2].algorithm);
  EXPECT_EQ("", sc->views[2].check_option);

  ASSERT_EQ(2u, sc->routines.size());
  const Routine &f = sc->routines[0];
  EXPECT_EQ("RETURN x + 1", f.body);
  EXPECT_EQ(true, f.deterministic);
  EXPECT_EQ("READS SQL DATA", f.data_access);
  EXPECT_EQ("INVOKER", f.security);
  EXPECT_EQ("root@%", f.definer);
  EXPECT_EQ("it's", f.comment);
  const Routine &p = sc->routines[1];
  EXPECT_EQ("BEGIN\n  SELECT   1;  -- kept\nEND", p.body);  // as written
  EXPECT_EQ(false, p.deterministic);
  EXPECT_EQ("MODIFIES SQL DATA", p.data_access);
  EXPECT_EQ("INVOKER", p.security);  // ALTER PROCEDURE
  EXPECT_EQ("changed", p.comment);
  EXPECT_EQ("", p.definer);

  ASSERT_EQ(2u, sc->triggers.size());
  EXPECT_EQ("SET NEW.a = 1", sc->triggers[0].body);
  EXPECT_EQ("", sc->triggers[0].order_kind);
  EXPECT_EQ("PRECEDES", sc->triggers[1].order_kind);
  EXPECT_EQ("tr1", sc->triggers[1].order_trigger);
  EXPECT_EQ("CURRENT_USER", sc->triggers[1].definer);
  EXPECT_EQ("BEGIN SET NEW.b = 2; END", sc->triggers[1].body);

  ASSERT_EQ(2u, sc->events.size());
  const Event &e1 = sc->events[0];
  EXPECT_EQ("EVERY 1 HOUR STARTS '2030-01-01'", e1.schedule);
  EXPECT_EQ(false, e1.on_completion_preserve);
  EXPECT_EQ("DISABLE ON SLAVE", e1.status);
  EXPECT_EQ("ev", e1.comment);
  EXPECT_EQ("DELETE FROM t", e1.body);
  const Event &e2 = sc->events[1];
  EXPECT_EQ("AT '2030-01-01 00:00:00'", e2.schedule);  // kept by ALTER EVENT
  EXPECT_EQ(true, e2.on_completion_preserve);
  EXPECT_EQ("ENABLE", e2.status);
  EXPECT_EQ(std::nullopt, e2.comment);
  EXPECT_EQ("BEGIN SELECT 1; END", e2.body);

  ASSERT_EQ(2u, sc->sequences.size());
  const Sequence &s1 = sc->sequences[0];
  EXPECT_EQ("10", s1.start);
  EXPECT_EQ("-2", s1.increment);
  EXPECT_EQ("1", s1.minvalue);
  EXPECT_EQ("100", s1.maxvalue);
  EXPECT_EQ("5", s1.cache);
  EXPECT_EQ(true, s1.cycle);
  const Sequence &s2 = sc->sequences[1];
  EXPECT_EQ("0", s2.cache);
  EXPECT_EQ(false, s2.cycle);
  EXPECT_EQ("3", s2.increment);
  EXPECT_EQ("", s2.start);
}

// A view's reads: tables and views, stored functions and sequences, without
// the common table expressions in scope, aliases or built-in functions.
TEST(Catalog, view_reads) {
  const std::string text =
      "CREATE VIEW v AS WITH c AS (SELECT 1 FROM base) SELECT crm.f(x), g(1), CONCAT(a), crm.concat(a),\n"
      "  NEXTVAL(s), NEXT VALUE FOR crm.s2, LASTVAL(s3), PREVIOUS VALUE FOR s4, customer.*, shop.t.id\n"
      "  FROM c, t AS customer, crm.note JOIN (SELECT 1 FROM d) dd ON 1\n"
      "  WHERE a IN (SELECT b FROM u) AND EXISTS (SELECT 1 FROM t) AND crm.f(1);\n"
      // Without RECURSIVE an element sees only those before it: `a` reads
      // the table b.
      "CREATE VIEW w AS WITH a AS (SELECT * FROM b), b AS (SELECT * FROM a) SELECT * FROM b;\n"
      "CREATE VIEW r AS WITH RECURSIVE a AS (SELECT 1 n UNION SELECT n + 1 FROM a WHERE n < 3)\n"
      "  SELECT * FROM a;\n"
      // A CTE of a subquery is not in scope in the next one.
      "CREATE VIEW n AS SELECT (WITH x AS (SELECT 1) SELECT * FROM x) AS q, (SELECT 1 FROM x) AS z;\n"
      // INFORMATION_SCHEMA.VIEWS writes names quoted and qualified.
      "CREATE VIEW `vv` AS select `shop`.`T`.`id` AS `id`,`shop`.`f`(1) AS `y` from `shop`.`T`;\n"
      "CREATE VIEW e AS SELECT 1;\n"
      // A qualified call of a keyword's name; a CTE name in any case.
      "CREATE VIEW k AS WITH C AS (SELECT 1) SELECT crm.replace(a, 'x', 'y'), crm.trim(a) FROM c;\n";
  const auto o = outline_of(text);
  const Schema *sc = schema(o, "");
  ASSERT_NE(nullptr, sc);
  ASSERT_EQ(7u, sc->views.size());
  auto reads = [&](size_t i) {
    std::vector<std::string> out;
    for (const auto &r : sc->views[i].reads.value()) {
      out.push_back(r.kind + " " + (r.schema.empty() ? "" : r.schema + ".") + r.name);
    }
    return out;
  };
  EXPECT_EQ((std::vector<std::string>{"table_or_view base", "function crm.f", "function g",
                                      "function crm.concat", "sequence s", "sequence crm.s2",
                                      "sequence s3", "sequence s4", "table_or_view t",
                                      "table_or_view crm.note", "table_or_view d", "table_or_view u"}),
            reads(0));
  EXPECT_EQ((std::vector<std::string>{"table_or_view b"}), reads(1));
  EXPECT_EQ((std::vector<std::string>{}), reads(2));
  EXPECT_EQ((std::vector<std::string>{"table_or_view x"}), reads(3));
  EXPECT_EQ((std::vector<std::string>{"function shop.f", "table_or_view shop.T"}), reads(4));
  EXPECT_EQ((std::vector<std::string>{}), reads(5));
  EXPECT_EQ((std::vector<std::string>{"function crm.replace", "function crm.trim"}), reads(6));

  const auto json = outline_json(o, text, Line_map(text));
  EXPECT_NE(std::string::npos,
            json.find("\"reads\":[{\"kind\":\"function\",\"schema\":\"shop\",\"name\":\"f\"},"
                      "{\"kind\":\"table_or_view\",\"schema\":\"shop\",\"name\":\"T\"}]"));
}

// Every member of format 1 is written, null when the SQL does not state it.
TEST(Catalog, outline_json_has_the_new_members) {
  const std::string text =
      "CREATE TABLE t (a VARCHAR(9) CHARACTER SET utf8mb4, KEY k (a(3)));\n"
      "CREATE TABLE c (x INT, CONSTRAINT f FOREIGN KEY fi (x) REFERENCES t (a));\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "CREATE PROCEDURE p() SELECT 1;\n"
      "CREATE TRIGGER tr BEFORE INSERT ON t FOR EACH ROW SET @a = 1;\n"
      "CREATE EVENT e ON SCHEDULE AT '2030-01-01' DO SELECT 1;\n"
      "CREATE SEQUENCE s;\n";
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  const auto json = outline_json(build_outline(text, index, pool), text, Line_map(text));
  for (const char *member :
       {"\"type\":\"VARCHAR(9)\"", "\"charset\":\"utf8mb4\"", "\"on_update\":null",
        "\"invisible\":false", "\"generated_storage\":null", "\"generated_row\":null",
        "\"system_versioning\":null",
        "\"key_parts\":[{\"column\":\"a\",\"length\":3,\"order\":null,\"expression\":null}]",
        "\"using\":null", "\"ignored\":false", "\"without_overlaps\":null",
        "\"name\":\"f\",", "\"index_name\":\"fi\"", "\"match\":null", "\"partitioning\":null",
        "\"system_versioning\":false", "\"periods\":[]", "\"algorithm\":null",
        "\"check_option\":null", "\"reads\":[]", "\"body\":\"SELECT 1\"", "\"deterministic\":null",
        "\"data_access\":null", "\"order\":null", "\"update_columns\":[]",
        "\"schedule\":\"AT '2030-01-01'\"", "\"on_completion_preserve\":null", "\"status\":null",
        "\"start\":null", "\"cycle\":null"}) {
    EXPECT_NE(std::string::npos, json.find(member)) << member;
  }
}

// The grammar rule names the fold looks for must exist; a MariaDB update
// that renames one fails here instead of silently dropping objects.
TEST(Catalog, grammar_rules_exist) {
  EXPECT_EQ(std::vector<std::string>{}, catalog_missing_grammar_symbols());
}

// The whole outline of the sakila schema, against the reviewed copy in
// tests/fixtures/outline/.  Regenerate with:
//   mariadb-lsp outline tests/fixtures/sql/sakila-schema.sql > tests/fixtures/outline/sakila-schema.json
// (then drop the "file" member) and review the diff.
TEST(Catalog, sakila_outline_matches_golden_file) {
  const std::string text = test::read_fixture("sakila-schema.sql");
  std::string golden = test::read_fixture("../outline/sakila-schema.json");
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == '\r')) golden.pop_back();
  ASSERT_FALSE(golden.empty());
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  const auto outline = build_outline(text, index, pool);
  EXPECT_EQ(golden, outline_json(outline, text, Line_map(text)));
}

TEST(Catalog, may_change_catalog) {
  EXPECT_TRUE(may_change_catalog("CREATE TABLE t (a INT)"));
  EXPECT_TRUE(may_change_catalog("  /*!40101 drop table t */"));
  EXPECT_TRUE(may_change_catalog("-- c\nUSE db"));
  EXPECT_FALSE(may_change_catalog("INSERT INTO t VALUES (1)"));
  EXPECT_FALSE(may_change_catalog("SELECT 1"));
  EXPECT_FALSE(may_change_catalog("CREATED"));
}

// rest_object_details: a view's CLASS, CRUD options, data mapping (nested
// references, @options, DATATYPE, JSON SCHEMA) and object options, the last
// of a repeated option winning.
TEST(Catalog, rest_view_details) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"
      "CREATE OR REPLACE REST VIEW /actor ON SERVICE /svc SCHEMA /s\n"
      "  AS `sakila`.`actor` CLASS Actor @INSERT @UPDATE @NOCHECK {\n"
      "    actorId: actor_id @KEY @SORTABLE,\n"
      "    \"firstName\": first_name @DATATYPE(\"varchar(45)\"),\n"
      "    last: last_update JSON SCHEMA {\"type\": 'string', \"n\": [1, -2.5, .5, true, null]},\n"
      "    films: sakila.film_actor @UNNEST {\n"
      "      filmId: film_id @KEY,\n"
      "      film: film { title: title }\n"
      "    }\n"
      "  }\n"
      "  ENABLED AUTHENTICATION NOT REQUIRED ITEMS PER PAGE 25 COMMENT 'it''s'\n"
      "  MEDIA TYPE 'application/json' FORMAT FEED OPTIONS {\"a\": 1} MERGE OPTIONS {\"b\": 2}\n"
      "  METADATA {\"x\": []} AUTHENTICATION PROCEDURE `sakila`.`auth` PRIVATE;\n"
      "CREATE REST VIEW /empty ON SERVICE /svc SCHEMA /s AS city {} MEDIA TYPE AUTODETECT;\n"
      "CREATE REST VIEW /plain ON SERVICE /svc SCHEMA /s AS country;\n"
      "CREATE REST VIEW IF NOT EXISTS /plain ON SERVICE /svc SCHEMA /s AS other.country\n"
      "  COMMENT 'ignored';\n"
      "CREATE REST PROCEDURE IF NOT EXISTS /plain ON SERVICE /svc SCHEMA /s AS p;\n");
  // The procedure does nothing: /plain is a view.
  ASSERT_EQ(1u, o.notes.size());
  EXPECT_EQ(Note_kind::rest_object_kind, o.notes[0].kind);
  const auto &objs = o.catalog.rest_services().at(0).schemas.at(0).objects;
  ASSERT_EQ(3u, objs.size());
  const Rest_object &v = objs[0];
  EXPECT_EQ("sakila", v.object_schema);
  EXPECT_EQ("actor", v.object_name);
  EXPECT_EQ("Actor", v.class_name.value_or(""));
  EXPECT_EQ((std::vector<std::string>{"INSERT", "UPDATE", "NOCHECK"}), v.crud);
  ASSERT_TRUE(v.fields);
  ASSERT_EQ(4u, v.fields->size());
  const auto &f = *v.fields;
  EXPECT_EQ("actorId", f[0].name);
  EXPECT_EQ("actor_id", f[0].column.value_or(""));
  EXPECT_EQ((std::vector<std::string>{"KEY", "SORTABLE"}), f[0].options);
  EXPECT_EQ("firstName", f[1].name);
  EXPECT_TRUE(f[1].options.empty());
  EXPECT_EQ("varchar(45)", f[1].datatype.value_or(""));
  EXPECT_EQ("{\"type\":\"string\",\"n\":[1,-2.5,0.5,true,null]}", f[2].json_schema.value_or(""));
  ASSERT_TRUE(f[3].reference);
  EXPECT_FALSE(f[3].column);
  EXPECT_EQ("sakila", f[3].reference->schema.value_or(""));
  EXPECT_EQ("film_actor", f[3].reference->table);
  EXPECT_EQ((std::vector<std::string>{"UNNEST"}), f[3].options);
  const auto &sub = f[3].reference->fields;
  ASSERT_EQ(2u, sub.size());
  ASSERT_TRUE(sub[1].reference);
  EXPECT_FALSE(sub[1].reference->schema);
  EXPECT_EQ("film", sub[1].reference->table);
  EXPECT_EQ(1u, sub[1].reference->fields.size());
  EXPECT_EQ("PRIVATE", v.settings.enabled.value_or(""));
  EXPECT_EQ(std::optional<bool>(false), v.settings.requires_auth);
  EXPECT_EQ(std::optional<uint64_t>(25), v.settings.items_per_page);
  EXPECT_EQ("it's", v.settings.comment.value_or(""));
  EXPECT_EQ("application/json", v.settings.media_type.value_or(""));
  EXPECT_FALSE(v.settings.media_type_autodetect);
  EXPECT_EQ("FEED", v.settings.format.value_or(""));
  EXPECT_EQ("sakila.auth", v.settings.auth_procedure.value_or(""));
  EXPECT_EQ("{\"b\":2}", v.settings.options.value_or(""));
  EXPECT_FALSE(v.settings.merge_options);  // a CREATE takes MERGE OPTIONS as the whole options
  EXPECT_EQ("{\"x\":[]}", v.settings.metadata.value_or(""));

  EXPECT_EQ("", objs[1].object_schema);
  ASSERT_TRUE(objs[1].fields);
  EXPECT_TRUE(objs[1].fields->empty());
  EXPECT_TRUE(objs[1].settings.media_type_autodetect);
  EXPECT_FALSE(objs[1].settings.media_type);

  EXPECT_FALSE(objs[2].fields);           // no { ... }: one field per column
  EXPECT_EQ("country", objs[2].object);   // IF NOT EXISTS kept the earlier definition
  EXPECT_FALSE(objs[2].settings.comment);
}

// rest_object_details: a routine's FORCE, PARAMETERS and RESULTs.
TEST(Catalog, rest_routine_details) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"
      "USE REST SERVICE /svc;\n"
      "USE REST SCHEMA /s;\n"
      "CREATE OR REPLACE REST PROCEDURE /p AS sakila.film_in_stock FORCE\n"
      "  PARAMETERS Params { id: p_film_id @IN, n: p_count @OUT @DATATYPE(INT) }\n"
      "  RESULT Res { filmId: inventory_id }\n"
      "  RESULT { x: y }\n"
      "  DISABLED;\n"
      "CREATE REST FUNCTION /f AS balance PARAMETERS { c: p_customer_id } RESULT R { r: result };\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &objs = o.catalog.rest_services().at(0).schemas.at(0).objects;
  ASSERT_EQ(2u, objs.size());
  const Rest_object &p = objs[0];
  EXPECT_TRUE(p.force);
  EXPECT_FALSE(p.fields);
  ASSERT_TRUE(p.parameters);
  EXPECT_EQ("Params", p.parameters->name.value_or(""));
  ASSERT_EQ(2u, p.parameters->fields.size());
  EXPECT_EQ((std::vector<std::string>{"OUT"}), p.parameters->fields[1].options);
  EXPECT_EQ("INT", p.parameters->fields[1].datatype.value_or(""));
  ASSERT_EQ(2u, p.results.size());
  EXPECT_EQ("Res", p.results[0].name.value_or(""));
  EXPECT_FALSE(p.results[1].name);
  EXPECT_EQ("y", p.results[1].fields.at(0).column.value_or(""));
  EXPECT_EQ("DISABLED", p.settings.enabled.value_or(""));
  const Rest_object &f = objs[1];
  EXPECT_FALSE(f.force);
  ASSERT_TRUE(f.parameters);
  EXPECT_FALSE(f.parameters->name);
  ASSERT_EQ(1u, f.results.size());
  EXPECT_EQ("R", f.results[0].name.value_or(""));
  EXPECT_EQ("", f.object_schema);
  EXPECT_EQ("balance", f.object_name);

  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos,
            json.find("\"object\":\"sakila.film_in_stock\",\"object_schema\":\"sakila\","
                      "\"object_name\":\"film_in_stock\",")) << json;
  EXPECT_NE(std::string::npos,
            json.find("{\"name\":\"n\",\"column\":\"p_count\",\"reference\":null,\"options\":[\"OUT\"],"
                      "\"datatype\":\"INT\",\"json_schema\":null}")) << json;
  EXPECT_NE(std::string::npos,
            json.find("\"enabled\":\"DISABLED\",\"requires_auth\":null,\"items_per_page\":null,"
                      "\"comment\":null,\"media_type\":null,\"media_type_autodetect\":false,"
                      "\"format\":null,\"auth_procedure\":null,\"options\":null,"
                      "\"merge_options\":false,\"metadata\":null,\"force\":true")) << json;
}

// rest_settings: a service's settings; CREATE OR REPLACE writes them anew,
// ALTER changes what it writes, MERGE OPTIONS merges in an ALTER only; a
// service is the same however its developers are written.
TEST(Catalog, rest_service_settings) {
  const auto o = outline_of(
      "CREATE REST SERVICE mike, `anna`@/svc ENABLED PUBLISHED PROTOCOL HTTPS COMMENT 'c'\n"
      "  AUTHENTICATION PATH '/auth' REDIRECTION DEFAULT VALIDATION 'v' PAGE CONTENT 'pc'\n"
      "  OPTIONS {\"a\": 1, \"b\": {\"c\": 2}} METADATA {\"m\": true};\n"
      "ALTER REST SERVICE anna,MIKE@/svc DISABLED MERGE OPTIONS {\"b\": {\"c\": null, \"d\": 3}};\n"
      "CREATE REST SERVICE /r COMMENT 'one' PROTOCOL HTTP;\n"
      "CREATE OR REPLACE REST SERVICE /r MERGE OPTIONS {\"x\": null};\n"
      "CREATE REST VIEW /v ON SERVICE /only SCHEMA /s AS a.t;\n"
      "ALTER REST SERVICE /only MERGE OPTIONS {\"p\": 1};\n"
      "ALTER REST SERVICE /only MERGE OPTIONS {\"q\": null};\n"
      "CREATE REST SERVICE /n;\n"
      "ALTER REST SERVICE /n MERGE OPTIONS {\"g\": 1} MERGE OPTIONS {\"h\": null};\n"
      "CREATE REST SERVICE `a,b`@/k;\n"
      "CREATE REST SERVICE a,b@/k;\n");
  EXPECT_TRUE(o.notes.empty());
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(6u, svcs.size());
  // Options null: MERGE OPTIONS stores them as they are; the last one counts.
  EXPECT_EQ("{\"h\":null}", svcs[3].settings.options.value_or(""));
  EXPECT_FALSE(svcs[3].settings.merge_options);
  EXPECT_EQ((std::vector<std::string>{"a,b"}), svcs[4].name.developers);
  EXPECT_EQ((std::vector<std::string>{"a", "b"}), svcs[5].name.developers);
  const Rest_service &a = svcs[0];
  EXPECT_EQ("mike, `anna`@/svc", a.name.path);
  EXPECT_EQ("/svc", a.name.request_path);
  EXPECT_EQ((std::vector<std::string>{"anna", "mike"}), a.name.developers);
  EXPECT_EQ("DISABLED", a.settings.enabled.value_or(""));
  EXPECT_EQ(std::optional<bool>(true), a.settings.published);
  EXPECT_EQ("HTTPS", a.settings.protocol.value_or(""));
  EXPECT_EQ("c", a.settings.comment.value_or(""));
  EXPECT_EQ("/auth", a.settings.auth_path.value_or(""));
  EXPECT_FALSE(a.settings.auth_redirection);  // DEFAULT
  EXPECT_EQ("v", a.settings.auth_validation.value_or(""));
  EXPECT_EQ("pc", a.settings.auth_page_content.value_or(""));
  EXPECT_EQ("{\"a\":1,\"b\":{\"d\":3}}", a.settings.options.value_or(""));
  EXPECT_FALSE(a.settings.merge_options);
  EXPECT_EQ("{\"m\":true}", a.settings.metadata.value_or(""));

  const Rest_service &r = svcs[1];
  EXPECT_FALSE(r.settings.comment);   // OR REPLACE writes the settings anew
  EXPECT_FALSE(r.settings.protocol);
  EXPECT_EQ("{\"x\":null}", r.settings.options.value_or(""));  // a CREATE does not merge
  EXPECT_FALSE(r.settings.merge_options);

  const Rest_service &only = svcs[2];  // only named: its options are not known
  EXPECT_FALSE(only.declared);
  EXPECT_EQ("{\"p\":1,\"q\":null}", only.settings.options.value_or(""));
  EXPECT_TRUE(only.settings.merge_options);

  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos,
            json.find("\"request_path\":\"/svc\",\"developers\":[\"anna\",\"mike\"],\"enabled\":false,"
                      "\"published\":true,\"protocol\":\"HTTPS\",\"comment\":\"c\",\"auth_path\":\"/auth\","
                      "\"auth_redirection\":null,\"auth_validation\":\"v\",\"auth_page_content\":\"pc\","
                      "\"options\":{\"a\":1,\"b\":{\"d\":3}},\"merge_options\":false,\"metadata\":{\"m\":true},"))
      << json;
}

// rest_settings: a schema's settings, and ALTER REST SCHEMA (rename, FROM,
// settings; a note for one the file does not name).
TEST(Catalog, rest_schema_settings) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "USE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /s FROM sakila PRIVATE AUTHENTICATION REQUIRED ITEMS PER PAGE 10\n"
      "  COMMENT 'sc' OPTIONS {\"k\": 1};\n"
      "ALTER REST SCHEMA /s NEW REQUEST PATH /s2 FROM world ITEMS PER PAGE 20 MERGE OPTIONS {\"l\": 2};\n"
      "CREATE REST VIEW /v AS t;\n"
      "ALTER REST SCHEMA /nope COMMENT 'x';\n");
  ASSERT_EQ(1u, o.notes.size());
  EXPECT_EQ(Note_kind::unknown_rest_schema, o.notes[0].kind);
  const auto &schemas = o.catalog.rest_services().at(0).schemas;
  ASSERT_EQ(1u, schemas.size());
  const Rest_schema &rs = schemas[0];
  EXPECT_EQ("/s2", rs.path);
  EXPECT_EQ("world", rs.schema);
  EXPECT_EQ(1u, rs.objects.size());  // the current schema follows the rename
  EXPECT_EQ("PRIVATE", rs.settings.enabled.value_or(""));
  EXPECT_EQ(std::optional<bool>(true), rs.settings.requires_auth);
  EXPECT_EQ(std::optional<uint64_t>(20), rs.settings.items_per_page);
  EXPECT_EQ("sc", rs.settings.comment.value_or(""));
  EXPECT_EQ("{\"k\":1,\"l\":2}", rs.settings.options.value_or(""));
  EXPECT_FALSE(rs.settings.merge_options);

  const std::string json = outline_json(o, "", Line_map(""));
  EXPECT_NE(std::string::npos,
            json.find("\"enabled\":\"PRIVATE\",\"requires_auth\":true,\"items_per_page\":20,\"comment\":\"sc\","
                      "\"options\":{\"k\":1,\"l\":2},\"merge_options\":false,\"metadata\":null,"))
      << json;
}

// rest_statements: every statement about a REST item, in file order, across
// DROP / re-CREATE and renames; USE REST SERVICE s SCHEMA p is shared; an
// ALTER of auth app links alone is marked; banners above come apart.
TEST(Catalog, rest_statements) {
  const std::string sql =
      "-- MSM Section 180: REST\n"                                    // 0
      "-- -----\n"                                                    // 1
      "DROP REST SERVICE IF EXISTS /svc;\n"                           // 2
      "-- The service\n"                                              // 3
      "CREATE REST SERVICE /svc COMMENT 'one';\n"                     // 4
      "\n"                                                            // 5
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"          // 6
      "USE REST SERVICE /svc SCHEMA /s;\n"                            // 7
      "CREATE REST VIEW /v AS t;\n"                                   // 8
      "ALTER REST SERVICE /svc ADD AUTH APP a REMOVE AUTH APP b;\n"   // 9
      "ALTER REST SERVICE /svc COMMENT 'two';\n"                      // 10
      "# banner\n"                                                    // 11
      "CREATE OR REPLACE REST VIEW /v AS t2;\n"                       // 12
      "ALTER REST SCHEMA /s NEW REQUEST PATH /s2;\n"                  // 13
      "CREATE REST CONTENT SET /cs ON SERVICE /svc;\n"                // 14
      "ALTER REST SERVICE /svc NEW REQUEST PATH /svc2;\n"             // 15
      "CREATE REST VIEW /w ON SERVICE /svc2 SCHEMA /s2 AS t;\n"       // 16
      "DROP REST VIEW /w FROM SERVICE /svc2 SCHEMA /s2;\n";           // 17
  const auto o = outline_of(sql);
  EXPECT_TRUE(o.notes.empty());
  const Catalog &c = o.catalog;
  const auto &svc = c.rest_services().at(0);
  const auto &rs = svc.schemas.at(0);
  const Line_map lines(sql);
  auto line_of = [&](const Rest_statement &s) { return lines.position(sql, s.origin.statement.begin).line; };
  auto summary = [&](const std::vector<Rest_statement> &v) {
    std::vector<std::string> out;
    for (const auto &s : v) {
      out.push_back(s.kind + "@" + std::to_string(line_of(s)) + (s.shared ? " shared" : "") +
                    (s.auth_apps_only ? " auth" : "") + (s.leading_comments ? " lc" : ""));
    }
    return out;
  };
  EXPECT_EQ((std::vector<std::string>{"drop@2", "create@4 lc", "use@7 shared", "alter@9 auth", "alter@10",
                                      "alter@15"}),
            summary(c.rest_statements(svc)));
  EXPECT_EQ((std::vector<std::string>{"create@6", "use@7 shared", "alter@13"}), summary(c.rest_statements(svc, rs)));
  ASSERT_EQ(1u, rs.objects.size());
  EXPECT_EQ((std::vector<std::string>{"create@8", "create@12 lc"}),
            summary(c.rest_statements(svc, rs, rs.objects[0])));
  EXPECT_EQ((std::vector<std::string>{"create@14"}),
            summary(c.rest_content_set_statements(svc, svc.content_sets.at(0))));
  const auto &with_banner = c.rest_statements(svc)[1];
  EXPECT_EQ(3u, lines.position(sql, with_banner.leading_comments->statement.begin).line);

  const std::string json = outline_json(o, sql, lines);
  EXPECT_NE(std::string::npos,
            json.find("{\"kind\":\"use\",\"range\":{\"start\":{\"line\":7,\"character\":0},\"end\":{\"line\":7,"
                      "\"character\":31}},\"selection_range\":{\"start\":{\"line\":7,\"character\":17},"
                      "\"end\":{\"line\":7,\"character\":21}},\"leading_comments\":null,\"shared\":true,"
                      "\"auth_apps_only\":false}"))
      << json;
}

// An executable comment above a REST statement is a statement, not its
// leading comments.
TEST(Catalog, rest_statements_executable_comment_above) {
  const std::string sql =
      "/*!40101 SET NAMES utf8 */;\n"
      "CREATE REST SERVICE /svc;\n";
  const auto o = outline_of(sql);
  const Catalog &c = o.catalog;
  const auto &statements = c.rest_statements(c.rest_services().at(0));
  ASSERT_EQ(1u, statements.size());
  EXPECT_FALSE(statements[0].leading_comments.has_value());
}

// An object is its path, of any kind, as in MariaDB Shell.
TEST(Catalog, rest_object_path_any_kind) {
  const std::string sql =
      "CREATE REST SERVICE /svc;\n"                                 // 0
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"        // 1
      "USE REST SERVICE /svc SCHEMA /s;\n"                          // 2
      "CREATE REST PROCEDURE /v AS p;\n"                            // 3
      "CREATE REST VIEW IF NOT EXISTS /v AS t;\n"                   // 4
      "DROP REST VIEW /v;\n"                                        // 5
      "CREATE REST PROCEDURE IF NOT EXISTS /v AS p2;\n"             // 6
      "CREATE OR REPLACE REST FUNCTION /v AS f;\n";                 // 7
  const auto o = outline_of(sql);
  ASSERT_EQ(2u, o.notes.size());
  EXPECT_EQ(Note_kind::rest_object_kind, o.notes[0].kind);
  EXPECT_FALSE(o.notes[0].applied);
  EXPECT_NE(std::string::npos, o.notes[0].message.find("is a REST PROCEDURE")) << o.notes[0].message;
  EXPECT_EQ(Note_kind::rest_object_kind, o.notes[1].kind);
  const Catalog &c = o.catalog;
  const auto &svc = c.rest_services().at(0);
  const auto &rs = svc.schemas.at(0);
  ASSERT_EQ(1u, rs.objects.size());
  EXPECT_EQ("function", rs.objects[0].kind);
  EXPECT_EQ("f", rs.objects[0].object);
  const Line_map lines(sql);
  std::vector<uint32_t> at;
  for (const auto &s : c.rest_statements(svc, rs, rs.objects[0]))
    at.push_back(lines.position(sql, s.origin.statement.begin).line);
  EXPECT_EQ((std::vector<uint32_t>{3, 4, 5, 6, 7}), at);
}

// A rename onto a path in use fails in MariaDB Shell, with its other clauses.
TEST(Catalog, rest_rename_onto_path_in_use) {
  const std::string sql =
      "CREATE REST SERVICE /a;\n"
      "CREATE REST SERVICE /b;\n"
      "ALTER REST SERVICE /a NEW REQUEST PATH /B COMMENT 'x';\n"
      "CREATE REST SCHEMA /s ON SERVICE /a FROM sakila;\n"
      "CREATE REST SCHEMA /t ON SERVICE /a FROM sakila;\n"
      "ALTER REST SCHEMA /s ON SERVICE /a NEW REQUEST PATH /t;\n"
      "ALTER REST SERVICE /a NEW REQUEST PATH /A;\n";
  const auto o = outline_of(sql);
  ASSERT_EQ(2u, o.notes.size());
  EXPECT_EQ(Note_kind::rest_path_in_use, o.notes[0].kind);
  EXPECT_EQ(Note_kind::rest_path_in_use, o.notes[1].kind);
  const Catalog &c = o.catalog;
  ASSERT_EQ(2u, c.rest_services().size());
  const auto &a = c.rest_services()[0];
  EXPECT_EQ("/A", a.name.path);  // a rename onto its own path, other case, is fine
  EXPECT_FALSE(a.settings.comment.has_value());
  ASSERT_EQ(2u, a.schemas.size());
  EXPECT_EQ("/s", a.schemas[0].path);
  EXPECT_EQ(3u, c.rest_statements(a).size());  // create, the failed alter, the rename
  EXPECT_EQ(2u, c.rest_statements(a, a.schemas[0]).size());
  EXPECT_EQ(1u, c.rest_statements(c.rest_services()[1]).size());
}

// The shell's metadata checks paths against enabled services only.
TEST(Catalog, rest_rename_onto_path_of_disabled_service) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "CREATE REST SERVICE /b DISABLED;\n"
      "ALTER REST SERVICE /a NEW REQUEST PATH /b;\n"
      "CREATE REST SERVICE /c DISABLED;\n"
      "CREATE REST SCHEMA /s ON SERVICE /c FROM sakila;\n"
      "CREATE REST SCHEMA /t ON SERVICE /c FROM sakila;\n"
      "ALTER REST SCHEMA /s ON SERVICE /c NEW REQUEST PATH /t;\n");
  EXPECT_TRUE(o.notes.empty());
  ASSERT_EQ(3u, o.catalog.rest_services().size());
  EXPECT_EQ("/b", o.catalog.rest_services()[0].name.path);
  EXPECT_EQ("/t", o.catalog.rest_services()[2].schemas.at(0).path);
}

// DROP ... IF EXISTS of another kind does nothing in the shell, without failing.
TEST(Catalog, rest_drop_if_exists_other_kind) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"
      "CREATE REST PROCEDURE /v ON SERVICE /svc SCHEMA /s AS p;\n"
      "DROP REST VIEW IF EXISTS /v FROM SERVICE /svc SCHEMA /s;\n");
  ASSERT_EQ(1u, o.notes.size());
  EXPECT_EQ(Note_kind::rest_object_kind, o.notes[0].kind);
  EXPECT_NE(std::string::npos, o.notes[0].message.find("does nothing")) << o.notes[0].message;
  EXPECT_EQ(1u, o.catalog.rest_services().at(0).schemas.at(0).objects.size());
}

// A plain CREATE at a path the file has fails in MariaDB Shell, in an
// enabled service; OR REPLACE replaces, and a service only named is created.
TEST(Catalog, rest_plain_create_onto_path_in_use) {
  const std::string sql =
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n"         // /svc only named
      "CREATE REST SERVICE /svc;\n"                                 // creates it
      "CREATE REST SERVICE /svc COMMENT 'x';\n"                     // fails
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM other;\n"         // fails
      "CREATE REST VIEW /v ON SERVICE /svc SCHEMA /s AS t;\n"
      "CREATE REST PROCEDURE /v ON SERVICE /svc SCHEMA /s AS p;\n"  // fails
      "CREATE REST CONTENT SET /cs ON SERVICE /svc;\n"
      "CREATE REST CONTENT SET /cs ON SERVICE /svc;\n"              // fails
      "CREATE OR REPLACE REST PROCEDURE /v ON SERVICE /svc SCHEMA /s AS p;\n"
      "CREATE REST SERVICE /off DISABLED;\n"
      "CREATE REST SCHEMA /s ON SERVICE /off FROM sakila;\n"
      "CREATE REST SCHEMA /s ON SERVICE /off FROM other;\n";       // disabled: replaces
  const auto o = outline_of(sql);
  ASSERT_EQ(4u, o.notes.size());
  for (const auto &n : o.notes) {
    EXPECT_EQ(Note_kind::rest_path_in_use, n.kind) << n.message;
    EXPECT_FALSE(n.applied);
  }
  EXPECT_NE(std::string::npos, o.notes[2].message.find("REST VIEW /v exists")) << o.notes[2].message;
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(2u, svcs.size());
  EXPECT_FALSE(svcs[0].settings.comment.has_value());
  ASSERT_EQ(1u, svcs[0].schemas.size());
  EXPECT_EQ("sakila", svcs[0].schemas[0].schema);
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());
  EXPECT_EQ("procedure", svcs[0].schemas[0].objects[0].kind);
  EXPECT_EQ(1u, svcs[0].content_sets.size());
  ASSERT_EQ(1u, svcs[1].schemas.size());
  EXPECT_EQ("other", svcs[1].schemas[0].schema);
}

// A rename onto the path of an item dropped earlier does not take its
// statements; a DROP and a new CREATE of one path keep theirs.
TEST(Catalog, rest_rename_onto_dropped_path) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "CREATE REST SCHEMA /x ON SERVICE /a FROM sakila;\n"
      "DROP REST SERVICE /a;\n"
      "CREATE REST SERVICE /b;\n"
      "CREATE REST SCHEMA /y ON SERVICE /b FROM sakila;\n"
      "ALTER REST SERVICE /b NEW REQUEST PATH /a;\n"
      "ALTER REST SCHEMA /y ON SERVICE /a NEW REQUEST PATH /x;\n"
      "DROP REST SERVICE /c;\n"
      "CREATE REST SERVICE /c;\n");
  EXPECT_TRUE(o.notes.empty());
  const Catalog &c = o.catalog;
  ASSERT_EQ(2u, c.rest_services().size());
  const auto &a = c.rest_services()[0];
  EXPECT_EQ("/a", a.name.path);
  std::vector<std::string> kinds;
  for (const auto &s : c.rest_statements(a)) kinds.push_back(s.kind);
  EXPECT_EQ((std::vector<std::string>{"create", "alter"}), kinds);  // /b's, not the old /a's
  ASSERT_EQ(1u, a.schemas.size());
  kinds.clear();
  for (const auto &s : c.rest_statements(a, a.schemas[0])) kinds.push_back(s.kind);
  EXPECT_EQ((std::vector<std::string>{"create", "alter"}), kinds);  // /y's, not the old /a/x's
  EXPECT_EQ(2u, c.rest_statements(c.rest_services()[1]).size());     // /c: drop, create
}

// A rename onto a live service (allowed in a disabled one) merges the
// service's statements, but not those of its schemas dropped earlier.
TEST(Catalog, rest_rename_onto_live_service_with_dropped_schema) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a DISABLED;\n"
      "CREATE REST SCHEMA /x ON SERVICE /a FROM sakila;\n"
      "DROP REST SCHEMA /x FROM SERVICE /a;\n"
      "CREATE REST SERVICE /b;\n"
      "CREATE REST SCHEMA /x ON SERVICE /b FROM sakila;\n"
      "ALTER REST SERVICE /b NEW REQUEST PATH /a;\n");
  EXPECT_TRUE(o.notes.empty());
  const Catalog &c = o.catalog;
  ASSERT_EQ(2u, c.rest_services().size());
  const auto &renamed = c.rest_services()[1];
  EXPECT_EQ("/a", renamed.name.path);
  EXPECT_EQ(3u, c.rest_statements(renamed).size());  // both services' creates, the alter
  ASSERT_EQ(1u, renamed.schemas.size());
  EXPECT_EQ(1u, c.rest_statements(renamed, renamed.schemas[0]).size());  // /b's /x only
}

// A schema renamed onto a live schema (in a disabled service): the schema's
// lists merge, a dropped object's statements under it do not.
TEST(Catalog, rest_rename_onto_live_schema_with_dropped_object) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc DISABLED;\n"
      "USE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /x FROM sakila;\n"
      "CREATE REST VIEW /v ON SCHEMA /x AS t;\n"
      "DROP REST VIEW /v FROM SCHEMA /x;\n"
      "CREATE REST VIEW /w ON SCHEMA /x AS t;\n"
      "CREATE REST SCHEMA /y FROM sakila;\n"
      "CREATE REST VIEW /v ON SCHEMA /y AS t;\n"
      "CREATE REST VIEW /w ON SCHEMA /y AS t;\n"
      "ALTER REST SCHEMA /y NEW REQUEST PATH /x;\n");
  EXPECT_TRUE(o.notes.empty());
  const Catalog &c = o.catalog;
  const auto &svc = c.rest_services().at(0);
  ASSERT_EQ(2u, svc.schemas.size());
  const auto &renamed = svc.schemas[1];
  EXPECT_EQ("/x", renamed.path);
  EXPECT_EQ(3u, c.rest_statements(svc, renamed).size());  // both creates, the alter
  ASSERT_EQ(2u, renamed.objects.size());
  EXPECT_EQ(1u, c.rest_statements(svc, renamed, renamed.objects[0]).size());  // /y's /v only
  EXPECT_EQ(2u, c.rest_statements(svc, renamed, renamed.objects[1]).size());  // live /x's /w and /y's
}

// A block comment whose opening line is not a comment line, or a statement
// continued on a line starting with `*`, is not cut into leading comments.
TEST(Catalog, rest_statements_partial_block_comment_above) {
  const std::string sql =
      "/*\n"
      "  The service\n"
      " */\n"
      "CREATE REST SERVICE /svc;\n"
      "SELECT 2\n"
      " * 3;\n"
      "-- the schema\n"
      "CREATE REST SCHEMA /s ON SERVICE /svc FROM sakila;\n";
  const auto o = outline_of(sql);
  const Catalog &c = o.catalog;
  const auto &svc = c.rest_services().at(0);
  const auto &statements = c.rest_statements(svc);
  ASSERT_EQ(1u, statements.size());
  EXPECT_FALSE(statements[0].leading_comments.has_value());
  const auto &schema = c.rest_statements(svc, svc.schemas.at(0));
  ASSERT_EQ(1u, schema.size());
  ASSERT_TRUE(schema[0].leading_comments.has_value());
  const Line_map lines(sql);
  EXPECT_EQ(6u, lines.position(sql, schema[0].leading_comments->statement.begin).line);
}

// --- column_type (outline feature column_types) ------------------------------

namespace {

// The column_type of t.c in schema db.
std::optional<std::string> column_type_of(const Outline &o, std::string_view t, std::string_view c,
                                          std::string_view db = "db") {
  const Table *tb = table(o, db, t);
  if (tb == nullptr) return "(no table)";
  const Column *col = tb->column(c);
  if (col == nullptr) return "(no column)";
  return col->column_type;
}

}  // namespace

// Every type of tests/fixtures/column_types/mariadb-13.1.json gives what the
// server reported for it, null where it refused the table.
TEST(Column_types, match_the_server) {
  const std::string json = test::read_fixture("../column_types/mariadb-13.1.json");
  rapidjson::Document d;
  d.Parse(json.data(), json.size());
  ASSERT_FALSE(d.HasParseError());
  const auto &types = d["types"].GetArray();
  ASSERT_GT(types.Size(), 300u);
  std::string sql = "CREATE DATABASE db CHARACTER SET utf8mb4;\nUSE db;\n";
  for (rapidjson::SizeType i = 0; i < types.Size(); ++i) {
    sql += "CREATE TABLE p" + std::to_string(i) + " (c " + types[i][0].GetString() + ");\n";
  }
  const auto o = outline_of(sql);
  for (rapidjson::SizeType i = 0; i < types.Size(); ++i) {
    const std::string type = types[i][0].GetString();
    const std::optional<std::string> want =
        types[i][1].IsNull() ? std::nullopt : std::optional<std::string>(types[i][1].GetString());
    const Table *tb = table(o, "db", "p" + std::to_string(i));
    // A table the parser refuses is not in the outline: no column type either.
    const std::optional<std::string> got =
        tb == nullptr || tb->columns.empty() ? std::nullopt : tb->columns[0].column_type;
    EXPECT_EQ(want, got) << type;
  }
}

// TEXT(n) and VARCHAR(n) depend on the charset: the column's, else the
// table's, else the schema's, as it is when the column is added.
TEST(Column_types, charset_in_force) {
  const auto o = outline_of(
      "CREATE DATABASE db CHARACTER SET utf8mb4;\n"
      "USE db;\n"
      "CREATE TABLE t (a TEXT(100), b VARCHAR(20000)) DEFAULT CHARSET latin1;\n"
      "CREATE TABLE u (a TEXT(100)) COLLATE ucs2_bin;\n"
      "CREATE TABLE v (a TEXT(100), b VARCHAR(20000));\n"
      "ALTER TABLE t ADD c TEXT(100) CHARACTER SET utf8mb4, ADD d CHAR(5);\n"
      "CREATE DATABASE other CHARACTER SET latin1;\n"
      "CREATE TABLE other.w (a TEXT(100));\n");
  EXPECT_EQ("tinytext", column_type_of(o, "t", "a"));  // 100 bytes in latin1
  EXPECT_EQ("varchar(20000)", column_type_of(o, "t", "b"));
  EXPECT_EQ("text", column_type_of(o, "t", "c"));
  EXPECT_EQ("char(5)", column_type_of(o, "t", "d"));
  EXPECT_EQ("tinytext", column_type_of(o, "u", "a"));  // 200 bytes in ucs2
  EXPECT_EQ("text", column_type_of(o, "v", "a"));      // 400 bytes in utf8mb4
  EXPECT_EQ(std::nullopt, column_type_of(o, "v", "b"));  // past 16383 characters
  EXPECT_EQ("tinytext", column_type_of(o, "w", "a", "other"));
}

// With no charset in the script, a type whose result depends on it has none.
TEST(Column_types, charset_not_known) {
  const auto o = outline_of(
      "CREATE TABLE db.t (a TEXT(100), b TEXT(1000), c VARCHAR(16383), d VARCHAR(16384),\n"
      "  e TEXT(100) CHARACTER SET latin1, f VARCHAR(5) COLLATE latin1_bin, g ENUM('ä', 'ö'),\n"
      "  h ENUM('a', 'A'), i ENUM('a', 'A') COLLATE latin1_bin);\n");
  EXPECT_EQ(std::nullopt, column_type_of(o, "t", "a"));  // tinytext in latin1, text in utf8mb4
  EXPECT_EQ("text", column_type_of(o, "t", "b"));        // text in any
  EXPECT_EQ("varchar(16383)", column_type_of(o, "t", "c"));
  EXPECT_EQ(std::nullopt, column_type_of(o, "t", "d"));
  EXPECT_EQ("tinytext", column_type_of(o, "t", "e"));
  EXPECT_EQ("varchar(5)", column_type_of(o, "t", "f"));
  // Whether a collation equates non-ASCII values is not checked here.
  EXPECT_EQ(std::nullopt, column_type_of(o, "t", "g"));
  EXPECT_EQ(std::nullopt, column_type_of(o, "t", "h"));  // duplicates under a _ci collation
  EXPECT_EQ("enum('a','A')", column_type_of(o, "t", "i"));
}

// REAL_AS_FLOAT (also in ANSI) makes REAL a float; NO_BACKSLASH_ESCAPES
// keeps a backslash in an ENUM value. As MariaDB 13.1 reports them.
TEST(Column_types, sql_mode) {
  const auto o = outline_of(
      "USE db;\n"
      "SET sql_mode = 'REAL_AS_FLOAT';\n"
      "CREATE TABLE m1 (a REAL, b REAL(10,2), c DOUBLE, d FLOAT8);\n"
      "SET sql_mode = 'ANSI';\n"
      "CREATE TABLE m2 (a REAL);\n"
      "SET sql_mode = 'NO_BACKSLASH_ESCAPES';\n"
      "CREATE TABLE m3 (a ENUM('x\\y', 'it''s'));\n"
      "SET sql_mode = DEFAULT;\n"
      "CREATE TABLE m4 (a REAL, b ENUM('x\\\\y', 'x\\'y'));\n");
  EXPECT_EQ("float", column_type_of(o, "m1", "a"));
  EXPECT_EQ("float(10,2)", column_type_of(o, "m1", "b"));
  EXPECT_EQ("double", column_type_of(o, "m1", "c"));
  EXPECT_EQ("double", column_type_of(o, "m1", "d"));
  EXPECT_EQ("float", column_type_of(o, "m2", "a"));
  EXPECT_EQ("enum('x\\\\y','it''s')", column_type_of(o, "m3", "a"));
  EXPECT_EQ("double", column_type_of(o, "m4", "a"));
  EXPECT_EQ("enum('x\\\\y','x''y')", column_type_of(o, "m4", "b"));
}

// ALTER TABLE ... CONVERT TO CHARACTER SET converts every character column
// (to binary, CHAR(5) is binary(5); latin1 to utf8mb4 widens text to
// mediumtext): their column types are no longer known. Others stay.
TEST(Column_types, convert_to_character_set) {
  const auto o = outline_of(
      "USE db;\n"
      "CREATE TABLE m6 (a CHAR(10), b VARCHAR(100), c TEXT, d TINYTEXT, e ENUM('a'), f LONGTEXT,\n"
      "  g SET('x'), h VARBINARY(10), i INT, j BLOB) DEFAULT CHARSET latin1;\n"
      "ALTER TABLE m6 CONVERT TO CHARACTER SET utf8mb4;\n");
  for (const char *c : {"a", "b", "c", "d", "e", "f", "g"}) {
    EXPECT_EQ(std::nullopt, column_type_of(o, "m6", c)) << c;
  }
  EXPECT_EQ("varbinary(10)", column_type_of(o, "m6", "h"));
  EXPECT_EQ("int(11)", column_type_of(o, "m6", "i"));
  EXPECT_EQ("blob", column_type_of(o, "m6", "j"));
}

// Parameters and RETURNS read as PARAMETERS.DTD_IDENTIFIER does on MariaDB
// 13.1: without DEFAULT, CHARACTER SET and COLLATE; anchored and ROW types
// and UDFs have none.
TEST(Column_types, routines) {
  const auto o = outline_of(
      "CREATE DATABASE db CHARACTER SET utf8mb4;\n"
      "USE db;\n"
      "CREATE PROCEDURE q1(IN a INT, OUT b BOOL, INOUT c CHAR, d TEXT(100), e JSON,\n"
      "  f VARCHAR(5) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin, g ENUM('x','y'), h DECIMAL,\n"
      "  i CHAR(5) CHARACTER SET binary, j INT ZEROFILL, k TEXT COMPRESSED, l LONG,\n"
      "  m DOUBLE PRECISION, n UUID, o VECTOR(3), p TEXT(10), q POINT, t ENUM('a ', 'it''s')) BEGIN END;\n"
      "CREATE PROCEDURE q2(a INT DEFAULT 5, b VARCHAR(10) DEFAULT 'x') BEGIN END;\n"
      "CREATE PROCEDURE q3(a TYPE OF db.p.c, b ROW(x INT)) BEGIN END;\n"
      "CREATE FUNCTION f1() RETURNS BOOL RETURN 1;\n"
      "CREATE FUNCTION f2() RETURNS VARCHAR(10) CHARSET latin1 COLLATE latin1_bin RETURN 'a';\n"
      "CREATE FUNCTION f4() RETURNS TEXT(100) RETURN 'a';\n"
      "CREATE FUNCTION f5() RETURNS TYPE OF db.p.c RETURN 1;\n"
      "CREATE FUNCTION f6() RETURNS JSON RETURN '{}';\n"
      "CREATE FUNCTION u RETURNS STRING SONAME 'u.so';\n");
  const Schema *db = schema(o, "db");
  ASSERT_NE(nullptr, db);
  auto routine = [&](std::string_view name) -> const Routine * {
    for (const auto &r : db->routines) {
      if (r.name == name) return &r;
    }
    return nullptr;
  };
  const Routine *q1 = routine("q1");
  ASSERT_NE(nullptr, q1);
  const std::vector<std::string> want = {
      "int(11)",       "tinyint(1)",    "char(1)",    "text",          "longtext",
      "varchar(5)",    "enum('x','y')", "decimal(10,0)", "binary(5)", "int(10) unsigned zerofill",
      "text /*M!100301 COMPRESSED*/", "mediumtext", "double", "uuid", "vector(3)",
      "tinytext",      "point",         "enum('a','it''s')"};
  ASSERT_EQ(want.size(), q1->parameters.size());
  for (size_t k = 0; k < want.size(); ++k) {
    EXPECT_EQ(want[k], q1->parameters[k].column_type) << q1->parameters[k].name;
  }
  EXPECT_EQ(std::nullopt, q1->returns_column_type);
  const Routine *q2 = routine("q2");
  ASSERT_NE(nullptr, q2);
  ASSERT_EQ(2u, q2->parameters.size());
  EXPECT_EQ("int(11)", q2->parameters[0].column_type);  // no DEFAULT
  EXPECT_EQ("varchar(10)", q2->parameters[1].column_type);
  const Routine *q3 = routine("q3");
  ASSERT_NE(nullptr, q3);
  ASSERT_EQ(2u, q3->parameters.size());
  EXPECT_EQ(std::nullopt, q3->parameters[0].column_type);
  EXPECT_EQ(std::nullopt, q3->parameters[1].column_type);
  EXPECT_EQ("tinyint(1)", routine("f1")->returns_column_type);
  EXPECT_EQ("varchar(10)", routine("f2")->returns_column_type);
  EXPECT_EQ("text", routine("f4")->returns_column_type);
  EXPECT_EQ(std::nullopt, routine("f5")->returns_column_type);
  EXPECT_EQ("longtext", routine("f6")->returns_column_type);
  ASSERT_NE(nullptr, routine("u"));
  EXPECT_EQ(std::nullopt, routine("u")->returns_column_type);
}

// SERIAL and the attributes after the type: a COLLATE among them counts.
TEST(Column_types, serial_and_attributes) {
  const auto o = outline_of(
      "CREATE DATABASE db CHARACTER SET utf8mb4;\n"
      "USE db;\n"
      "CREATE TABLE t (a SERIAL, b TEXT(100) NOT NULL COLLATE latin1_bin, c INT UNSIGNED NOT NULL\n"
      "  AUTO_INCREMENT PRIMARY KEY, d DECIMAL(10,2) DEFAULT 0 COMMENT 'x');\n");
  EXPECT_EQ("bigint(20) unsigned", column_type_of(o, "t", "a"));
  EXPECT_EQ("tinytext", column_type_of(o, "t", "b"));  // latin1, as MariaDB 13.1 reports it
  EXPECT_EQ("int(10) unsigned", column_type_of(o, "t", "c"));
  EXPECT_EQ("decimal(10,2)", column_type_of(o, "t", "d"));
}

}  // namespace mdbls

namespace mdbls {

// A table created without a charset keeps its schema's as it was then; an
// ALTER TABLE's own DEFAULT CHARSET applies to the columns it adds.
TEST(Column_types, table_charset_at_creation) {
  const auto o = outline_of(
      "CREATE DATABASE db CHARACTER SET latin1;\n"
      "USE db;\n"
      "CREATE TABLE r1 (a INT);\n"
      "ALTER DATABASE db CHARACTER SET utf8mb4;\n"
      "ALTER TABLE r1 ADD b TEXT(100);\n"
      "CREATE TABLE r2 (a INT) DEFAULT CHARSET utf8mb4;\n"
      "ALTER TABLE r2 ADD b TEXT(100), DEFAULT CHARSET latin1;\n");
  EXPECT_EQ("tinytext", column_type_of(o, "r1", "b"));
  EXPECT_EQ("tinytext", column_type_of(o, "r2", "b"));
}

// Refused on the server: an unknown CHARACTER SET, 65 SET members, a
// collation named as a charset. Accepted: values an _ai_cs collation tells
// apart (MariaDB 13.1).
TEST(Column_types, refused_members_and_charsets) {
  std::string set = "SET(";
  for (int i = 1; i <= 65; ++i) set += (i > 1 ? ",'" : "'") + std::to_string(i) + "'";
  set += ")";
  const auto o = outline_of(
      "USE db;\n"
      "CREATE TABLE r3 (a VARCHAR(10) CHARACTER SET nosuch, b ENUM('a','\xc3\xa1') COLLATE uca1400_ai_cs,\n"
      "  c " + set + ", d CHAR(2) CHARACTER SET latin1_bin) DEFAULT CHARSET utf8mb4;\n");
  for (const char *c : {"a", "c", "d"}) {
    EXPECT_EQ(std::nullopt, column_type_of(o, "r3", c)) << c;
  }
  EXPECT_EQ("enum('a','\xc3\xa1')", column_type_of(o, "r3", "b"));
}

}  // namespace mdbls
